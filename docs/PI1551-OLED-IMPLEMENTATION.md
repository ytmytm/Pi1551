# Pi1551 OLED firmware and native tests

The interaction contract is [PI1551-TUI-SPEC.md](PI1551-TUI-SPEC.md).
The terminal reference is `make tui`. The implementation is part of the normal
Pi1551 Raspberry Pi 3 build:

```sh
make -j4
make test-oled-native
make test-tcbm2sd-native
```

No native test copies firmware to a card or runs Raspberry Pi code. Native
tests require GCC/G++, Python 3, AddressSanitizer and UndefinedBehaviorSanitizer.
They build in temporary directories and create disposable SD/TAP/D64 fixtures.

## Code and ownership

| Component | Responsibility |
| --- | --- |
| `src/OledMenu.*` | Production state machine, SOLO/LIST, Browser/Fliplist, menu history, ROM markers, settings actions, 16x8 text frames and prefix-aware scrolling |
| `src/OledSettings.*` | Validate runtime values and merge them into options text while preserving unrelated settings/comments |
| `src/Pi1551Oled.*` | FatFS/kernel adapter, input queue, deferred media changes, OLED frame handoff, persistent settings, automount validation |
| `src/main.cpp` kernel hooks | Emulated and container mount/unmount, drive/ROM/runtime settings, actual status, host selections/resets and reboot preparation |
| `src/tape_player.*` | Independent core2 playback, motor polling, PLAY lifecycle, synchronized eject/seek and VICE counter conversion |
| `src/SSD1306.*`, `src/ScreenLCD.*` | Eight-pixel text rendering, separate triangle/inversion, orientation and I2C transfer |

Core0 alone samples encoder/buttons and drives the OLED. The producer/consumer
input ring retains ordered encoder impulses and button releases. Core1 owns
menu state and all SD/media operations. A short spinlock protects the copied
display frame; I2C does not run while holding it. Core2 continues generating
tape pulses independently. Menu/status updates also run during drive idle
sleep, so cassette motor changes and counter updates remain live.

Mount/unmount requests during emulation leave the microsecond loop before SD
loading or dirty-image persistence. The UI is checkpointed until the operation
completes; failure restores the previous selection and reports an error. A
D64/G64-to-D64/G64 flip preserves CPU/TPI state and uses the mechanism's disk
change sequence. A switch to a non-emulated medium enters the TCBM browser
protocol while the OLED remains in Fliplist. A PRG is exposed through a
read-only generated D64 and a decoded-sector reader, not a running 6502.

SD operations and explicit tape/ROM/settings actions can pause drive execution;
the native suite verifies behavior and ownership boundaries, not GPIO deadlines
or compatibility with every physical loader. HDMI is not the menu target.

## Runtime persistence

Load settings reads `/options.txt` on demand through `Options::LoadRuntimeText`, validates recognized runtime values before
applying them, and uses normal boot defaults for omitted runtime values.
ROM slot filenames, hardware/model choices and other startup-only values are
not changed by this action. Loading does not itself mount media or reset the
drive. Existing runtime state is retained on a read/parse failure.

Save settings rereads the current file and changes only values modified in the
device since boot or the last successful load/save. Values in untouched
settings remain as the user currently wrote them, including external edits.
An existing option keeps its line number, key spelling, indentation, spacing,
inline comments and line ending; only its numeric value is replaced. Missing
changed options are appended only when their current value differs from the
`Options` default. CRLF/LF and the absence of a final newline are preserved;
appending adds the necessary separator using the file's newline style.
Duplicate definitions remain intact; only the last (effective) definition is
changed. A no-op save does not write or rename any files.

When content changes, Save settings writes and synchronizes `/options.txt.tmp`, renames the original
to `/options.txt.bak`, installs the new file, then removes the backup. Failure
before installation keeps/restores the original. Unknown options, ROM slots
and comments are retained. Existing `.tmp`/`.bak` files cause a reported failure rather than
overwriting recovery material. If power fails between renames, recover the
original from `.bak` (there is no boot-time automatic recovery yet).

New runtime options written alongside the existing options are:

| Key | Values |
| --- | --- |
| `autoMountEnabled` | 0/1; default 1, applies the configured `AutoMountImage` only at boot/processed host reset |
| `oledDimming` | 0/1; uses the existing dim contrast and timeout |
| `oledSound` | 0/1; enables head-step audio independently of its mode |

Other settings use their existing names (`deviceID`, `idleSleep`, `RAMBOard`,
`ignoreReset`, `tapeMotorAlwaysOn`, `onResetChangeToStartingFolder`,
`skipMotorSpinUpDelay`, `lowercaseBrowseModeFilenames`,
`disableSD2IECCommands`, `i2cLcdOnContrast`, `i2cLcdFlip`,
`rotaryEncoderInvert`, `soundOnGPIOFreq`). SD2IEC ON is stored as
`disableSD2IECCommands = 0`.

Settings/Display also uses `displayTemperature`, `i2cLcdDimTime` and
`i2cLcdDimContrast`. Font maps directly to `i2cLcdUseCBMChar`; Scroll maps to
`scrollHighlightRate` (seconds, represented internally in milliseconds).
`oledFont` and `oledScroll` are obsolete and ignored, not rewritten as active
options. Numeric values and `NewDiskType = d64/g64` are preserved in their
original lines by the settings merger; unchanged float formatting is retained.

Disk/New image opens a format selector and explicit Create image action.
It uses `NewDiskType` as the remembered format, creates a real empty D64 or
G64 under a unique NEWIMAGE number, refuses to overwrite existing files and
never mounts a new image automatically.

The buzzer is fixed to `PIGPIO_OUT_SOUND` (GPIO13, physical pin 33). SOUND
contains enable/disable and SAMPLE/BUZZER mode. `SoundOnGPIO = 0` requests a
PWM/DMA sample, `1` a square-wave buzzer. Only BUZZER offers frequency and
`SoundOnGPIODuration`; duration applies to subsequent head-step triggers.
The production timing controller counts actual microseconds and mutes/cancels
the buzzer on mode/enable changes. The sample buffer is reserved at boot so
switching into SAMPLE does not allocate memory during emulation. Switching
mode or disabling sound stops the existing DMA/PWM output. Audio never changes TCBM GPIO18 or
cassette READ GPIO19. Legacy `oledSoundGPIO` entries are ignored and preserved
as unrelated options when saving; they cannot redirect the output.

An empty, missing or invalid automount image/list is treated as OFF and hides
the option. The mock keeps it visible. Disk size/header checks reject unsupported
media before mounting. `.LST` entries are resolved relative to the list file's
directory, can be absolute paths, and are deduplicated in order. Empty lists
return to SOLO; bad references report a failure. A ROM/BIN reference does not
switch the ROM: ROM changes are exclusively through Settings/ROM.

Before full reboot, buffered host-file writes are completed, files are
synchronized/closed, dirty images and mounted containers are synchronized and
the SD device is flushed. A short/failed write retains the unwritten part for
retry. Failure prevents reboot. The action never asserts host RESET and is
independent of the host-reset IGNORE setting.

## Native coverage and limits

The suite runs production components, not a Python reimplementation:

- Menu sequences cover all seven media types, SOLO cleanup, mixed fliplist
  switching, membership toggles, root/parent returns, immediate activation,
  ROM markers, long-name tails, reset policy and confirmation defaults.
- 20,000 deterministic input events exercise the state graph with invariants
  for mounted medium, Browser unmounting, nonempty Fliplist and frame bounds.
- The firmware adapter runs against a POSIX FatFS card plus observable kernel
  hooks, including deferred-operation rollback, real list/options parsing,
  invalid automount and write/sync/rename failures.
- The real TAP parser/player runs with GPIO stubs, including seek/rewind/eject,
  end-of-tape PLAY and motor behavior without a loaded cassette.
- The real D64 encoder and PRG sector reader are checked byte-for-byte across
  partial/full sector boundaries. Write/sync failures retain dirty state.
- The real OLED raster/transfer code runs against an I2C recorder, checking
  eight rows, row inversion, active triangles and orientation.
- The real host-file buffer checks short-write retry and reboot flushing.
- Headless TUI tests cover display settings, scroll timing, sound mode and
  explicit D64/G64 creation. The production audio controller checks waveform
  frequency, duration, sample requests and muting; DMA register timing itself
  still requires hardware testing.
- The existing 17 TCBM protocol tests remain a separate regression suite.

The native suite does not emulate complete 6502 instruction execution, a
physical encoder, hardware I2C/GPIO latency, SD-card power-loss behavior or
actual watchdog restart. Those require a focused final hardware smoke test.

OLED contrast changes send only command 0x81 and its brightness byte for both
SSD1306 and SH1106; VCOMH remains as initialized by the driver. Native I2C tests
check this separation and the actual firmware dimming path at the configured
timeout, waking on UI input, plus Dimming OFF and Dim time 0/OFF.
Dim level 0 sends DISPLAY OFF (0xAE); input wakes it with DISPLAY ON (0xAF).
Active contrast is clamped to at least 1, and runtime settings load rejects 0.
ASCII and missing-chargen fallback use the existing native CGA 8x8 font rather
than dropping alternate scanlines from VGA 8x16. C64 chargen remains native
8x8. Initialization uses page mode 0x02 for SSD1306 and omits the unsupported
addressing-mode command on SH1106. Both retain 64-line multiplex/COM geometry.
Encoder pins remain fixed, with runtime inversion applied to decoded rotation
events; button events and the decoder's in-progress quadrature state are unchanged.

The display initially prepares ASCII fallback slots; startup prepares both
256-glyph 8x8 slots again after CheckOptions loads chargen (the panel itself
is constructed earlier): built-in ASCII and ASCII-indexed C64 (mapped into the chargen's
lower/upper bank). Runtime Font switches the slot used by PlotMenu on the next
frame, independent of HDMI state and without SD reads. Missing/unreadable or
wrong-sized chargen leaves two identical ASCII slots; valid size is 4096 bytes.
Native raster tests switch both ways using the shipped chargen.bin on SSD1306
and SH1106, then clear the global CBMFont pointer to verify slot independence.

Reset path is independent of drive Reset REACT/IGNORE. Core0 samples the raw
active-low RESET alongside buttons and sends one path-only event per assertion
when IGNORE and Reset path ON are selected. REACT keeps the existing reset
protocol path, which also invokes the same browser-path operation. A path-only
event in emulation queues a transition; directory I/O runs outside the real-time
loop and the CPU resumes without being reset. It does not unmount media,
clear the list, rewind tape or invoke automount. Holding RESET produces no
repeated path resets. Native tests cover both policies, held inputs and deferral.
The browser header shows only the final SD directory component with an
independent scrolling clock, preserving the device prefix and optional four
columns for temperature. Disabling Temp extends its width to all 16 columns.

Sound changes request a one-second PCM preview from the portable
Pi1551SoundPreview generator. SAMPLE loops the click; BUZZER is synthesized
at the configured frequency for min(Duration,1000) ms. OFF/zero duration are
silent. An ~88 KiB buffer is reserved at startup and reused, with the same PWM
clock/range as ordinary audio (19.2 MHz / 2 / 436 on the Pi3 target).
Generating and starting the DMA buffer is deferred outside emulation; no
one-second busy wait is used. The reusable payload and control block are
cleaned from CPU caches and published with a synchronization barrier before
DMA starts. Service restores the GPIO mode after the window, and any
subsequent settings apply cancels the preview first.
Normal head-step triggers/output are suppressed while preview DMA owns SOUND.
Native tests verify generated samples, frequency, duration, mute, menu requests
and deferred adapter effects; DMA timing and audible quality require hardware.
