# Pi1551 User Manual

This manual covers the Pi1551 firmware behavior. Hardware assembly, cable
orientation, voltage-level details, and cartridge/paddle construction are
documented in the PI1551-hat, PI1551-III, and tcbm2sd hardware projects.

For disk mode, Pi1551 requires a tcbm2sd board revision 1.3 or newer because
the ribbon cable connector is needed. Update the tcbm2sd CPLD firmware/JED file
to version 1.4 or newer for full compatibility with software that uses mirrored
TCBM register addresses, including games such as Corruption and Fish.

## Operating Modes

### Browser Mode

Browser mode is active after boot and whenever no disk image is being emulated.
It exposes the SD card as a file-level device over TCBM and also provides the
HDMI/OLED file picker.

Use browser mode to:

- Load PRG files directly from SD.
- Browse folders on the SD card.
- Select a D64 for 1551 emulation.
- Open D71, D81, D80, or D82 images as read-only file containers.
- Load a TAP file into the tape player.
- Create new D64 or G64 images from the UI.

### 1551 Emulation Mode

Selecting a D64 starts cycle-exact Commodore 1551 emulation. The emulated 1551
has its own 6502, 6523 TPI, DOS ROM, drive mechanics, and disk image state.

D64 is the primary supported emulation format. G64/NIB/NBZ/D71/D81 support is
inherited from Pi1541 disk image code in places, but Pi1551's advertised
cycle-exact 1551 path is D64.

### Tape Player

Selecting a TAP file loads it into the tape player instead of entering disk
emulation. Playback is sent to the C16/C116/Plus/4 tape port through the
Pi1551 interface.

Supported TAP files must use the Plus/4/C16 TAP signature `C16-TAPE-RAW`.
Versions 0, 1, and 2 are accepted. PAL/NTSC timing is taken from the TAP
header.

The tape status appears on the OLED when a tape is loaded. The display shows a
three-digit counter, PLAY state, motor state, and percentage.

## File And Image Behavior

### SD Files

In browser mode, Pi1551 provides file-level access to files and folders on the
SD card. This is the mode used by file browsers and normal `LOAD` operations
that do not require full drive emulation.

### D64 Images

D64 images can be mounted for 1551 emulation. This is the compatibility mode
for software that expects real drive timing or drive-side code execution.

D64 images can also be opened in browser-style contexts when appropriate.

### D71, D81, D80, D82 Images

D71, D81, D80, and D82 images are supported as read-only file containers. You
can browse directories and load files from inside them, but Pi1551 does not
emulate those drive mechanisms as 1551 hardware.

### LST Files

LST files can be used as disk-swap lists. The browser can also create
`autoswap.lst` from selected images.

## Physical Buttons

The default logical button order is:

| Logical button | Default number | Typical Pi1551 hat function |
| --- | ---: | --- |
| Enter | 1 | Select / exit emulation |
| Up | 2 | Move up / previous disk |
| Down | 3 | Move down / next disk |
| Back | 4 | Go back / exit emulation |
| Insert | 5 | Add/select helper |

The firmware stores its built-in button numbers as one-based values, then
converts them internally to zero-based GPIO button indexes.

Button 1 (`Enter`) is also involved in ROM switching. With plain buttons and
no rotary encoder, hold `Enter` and press another button to select ROM slots
1-4 as shown below. With the rotary encoder enabled, hold the rotary
pushbutton for about two seconds to open the ROM selector, release it, rotate
to choose a ROM, then press and release to apply.

### Browser Mode Buttons

| Action | Button |
| --- | --- |
| Move highlight up | Up |
| Move highlight down | Down |
| Select file, enter folder, or mount image | Enter |
| Go to parent folder | Back |
| Add highlighted disk image to the disk caddy | Insert |
| Set device ID 8 | Hold Insert, press Enter |
| Set device ID 9 | Hold Insert, press Up |
| Set device ID 10 | Hold Insert, press Down |
| Select ROM 1 | Hold Enter, press Up |
| Select ROM 2 | Hold Enter, press Down |
| Select ROM 3 | Hold Enter, press Back |
| Select ROM 4 | Hold Enter, press Insert |

When the rotary encoder is enabled, ROM switching is handled by the rotary ROM
selector instead of the hold-Enter button combinations.

### Emulation Mode Buttons

| Action | Button |
| --- | --- |
| Exit emulation and return to browser | Enter or Back |
| Next disk in caddy/LST | Up |
| Previous disk in caddy/LST | Down |

### Rotary Encoder

Enable with:

```text
RotaryEncoderEnable = 1
```

On the Pi1551 hat, the wiring is fixed:

| Encoder signal | GPIO | Hat button |
| --- | ---: | --- |
| Pushbutton | GPIO27 | SW1 |
| B / DT | GPIO22 | SW2 |
| A / CLK | GPIO17 | SW3 |

Turn the encoder to move through the browser. Press and release the encoder
button to select.

To switch ROMs, hold the rotary pushbutton for about two seconds. The ROM
selector opens. Release, rotate to choose a ROM, then press and release to
apply. In emulation mode the drive CPU is reset and fast-booted after the ROM
switch.

Use `RotaryEncoderInvert = 1` if rotation is backwards.

## USB Keyboard Shortcuts

A USB keyboard is optional but useful for development and setup.

### Browser Mode Keyboard

| Action | Key |
| --- | --- |
| Select | Enter |
| Add to caddy | Insert or Alt+Enter |
| Back | Backspace |
| Finish selected caddy and start emulation | Space |
| Up/down | Up / Down |
| Page up/down | PageUp/PageDown or Left/Right |
| Home/end | Home / End |
| Reboot Raspberry Pi | Left Ctrl+Left Alt+Delete |
| Create new disk image | Alt+N |
| Auto-load configured image | Alt+A |
| Fake reset | Alt+R |
| Toggle SD read-only attribute | Alt+W |
| Create `autoswap.lst` | Alt+L |
| Select ROM slot | F1..F7 |
| Set device ID | F8..F11 set IDs 8..11 |
| Search by first characters | 0..9, A..Z except R and P |
| Toggle tape READ test signal | R |
| Toggle tape SENSE test signal | P |

R and P are reserved for tape test toggles, so they are not used for browser
search.

### Emulation Mode Keyboard

| Action | Key |
| --- | --- |
| Exit emulation | Esc |
| Previous disk | PageUp |
| Next disk | PageDown |
| Auto-load configured image | Alt+A |
| Fake reset | Alt+R |
| Halt debug mode | H |
| Step debug mode | S |
| Run debug mode | R held |
| Reset emulated drive | E |
| Direct disk swap | F1..F10, 1..0, keypad 1..0 |
| Toggle tape READ test signal | R pressed |
| Toggle tape SENSE test signal | P pressed |
| Reboot Raspberry Pi | Left Ctrl+Left Alt+Delete |

## TAP Playback

Selecting a `.tap` file loads it and asserts the tape SENSE line as if PLAY is
pressed. The player then emits tape pulses on the READ line.

`tapeMotorAlwaysOn` controls how playback starts:

- `tapeMotorAlwaysOn = 1`: motor is treated as always active. Playback starts
  immediately after the TAP is loaded. This is meant for machines with an
  original CPU replaced by a 6510, which cannot control the tape MOTOR line.
- `tapeMotorAlwaysOn = 0`: playback follows the tape MOTOR input line.

The tape player is reset when the Commodore reset line is handled. Exiting disk
emulation does not unload an already loaded tape.

## `options.txt`

`options.txt` lives in the SD card root. The parser accepts `//` line comments
and `/* ... */` block comments. Values are parsed as decimal by default, but C
style numeric prefixes such as `0x3c` are accepted for integer values.

Options not present in the file use built-in defaults from `src/options.cpp`.

### Drive And Startup

| Option | Default | Meaning |
| --- | --- | --- |
| `deviceID` | `8` | TCBM device number. |
| `OnResetChangeToStartingFolder` | `1` | Return browser folder to `/1551` after reset. |
| `AutoMountImage` | empty | Auto-mount a filename from `/1551` after boot/reset. Also accepts `.lst`. |
| `StarFileName` | empty | File served for `LOAD"*"` / `DLOAD"*"` style requests. |
| `AutoBaseName` | `autoname` | Prefix for automatically created disk images. |
| `NewDiskType` | `d64` | Type created by Alt+N. Valid values: `d64`, `g64`. |
| `DisableSD2IECCommands` | `0` | Disable browser-mode SD2IEC-style commands when set to `1`. |
| `LowercaseBrowseModeFilenames` | `0` | Send browser-mode filenames as lowercase. |
| `IgnoreReset` | `0` | Ignore host reset pulses when set to `1`. |
| `QuickBoot` | `0` | Skip startup delay/logo pause when set to `1`. |
| `ShowOptions` | `0` | Print parsed options on HDMI at startup. |

### ROMs And Fonts

| Option | Default | Meaning |
| --- | --- | --- |
| `ROM1` or `ROM` | `dos1551.bin` | Primary ROM slot used by the UI. |
| `ROM2`..`ROM7` | empty | Additional selectable 1551 ROM slots. |
| `ROM1551` | `dos1551-318008-01.bin` | Legacy dedicated 1551 ROM option. If `ROM1` is empty, it can supply slot 1. |
| `ChargenFont` or `Font` | `chargen` | 8-bit character ROM/font file for HDMI text. |

ROM files are searched from the SD root and `/roms` paths used by the firmware.
ROM slots can be switched at runtime. With the rotary encoder enabled, hold the
rotary pushbutton for about two seconds, release it, rotate to pick a ROM, and
press/release to apply. Without rotary, browser-mode button shortcuts can
select ROM slots 1-4 by holding `Enter` and pressing `Up`, `Down`, `Back`, or
`Insert`. USB keyboard shortcuts can select ROM slots with `F1`..`F7`.

### Display

| Option | Default | Meaning |
| --- | --- | --- |
| `LCDName` | empty | OLED model. Valid values: `ssd1306_128x64`, `ssd1306_128x32`, `sh1106_128x64`. |
| `LcdLogoName` or `LCDLogoName` | `1541classic` | Built-in logo name or custom raw logo filename. |
| `i2cLcdAddress` | `0x3c` | I2C OLED address. `60` decimal is `0x3c`. |
| `i2cScan` | `0` | Scan I2C and display found addresses at startup. |
| `i2cLcdFlip` | `0` | Rotate OLED 180 degrees. |
| `i2cLcdOnContrast` | `127` | OLED active contrast. |
| `i2cLcdDimContrast` | unset/0 | OLED dimmed contrast. |
| `i2cLcdDimTime` | unset/0 | Time before OLED dimming. |
| `i2cLcdUseCBMChar` | `0` | Use CBM font on OLED. |
| `DisplayPNGIcons` | `0` | Show 320x200 PNG files matching disk image names on HDMI. |
| `GraphIEC` | `0` | Show TCBM and tape signal activity on HDMI. |
| `DisplayTemperature` | `0` | Show Raspberry Pi CPU temperature. |
| `DisplayPC` | `0` | Show emulated 6502 program counter for debugging. |
| `scrollHighlightRate` | `0.07` | Long filename scroll rate in seconds. |

The Pi1551 hat always uses I2C bus 1 for the OLED. `i2cBusMaster` exists in
the code but is forced to `1` after parsing.

### Sound

| Option | Default | Meaning |
| --- | --- | --- |
| `SoundOnGPIO` | `0` | `0`: legacy PWM/DMA sample on GPIO13. `1`: square-wave buzzer on GPIO13. |
| `SoundOnGPIODuration` | `1000` | Buzzer duration in milliseconds when `SoundOnGPIO = 1`. |
| `SoundOnGPIOFreq` | `1200` | Buzzer frequency in Hz when `SoundOnGPIO = 1`. |

For Pi1551 hardware with a piezo or buzzer on the SOUND pin, use
`SoundOnGPIO = 1`.

### Tape

| Option | Default | Meaning |
| --- | --- | --- |
| `tapeMotorAlwaysOn` | `1` | `1`: ignore MOTOR input and play immediately, for machines with a 6510 CPU replacement that cannot control MOTOR. `0`: follow MOTOR input on original machines. |

### Drive Timing And Expansion

| Option | Default | Meaning |
| --- | --- | --- |
| `RAMBOard` | `0` | Enable 8 KiB drive RAM expansion at `$8000`. Use with `dos1551-ram.bin`; this comes from the [1551-RAMBOard](https://github.com/ytmytm/1551-RAMBOard) project and is supported by [Parobek](https://github.com/ytmytm/plus4-parobek) fastloaders. |
| `skipMotorSpinUpDelay` | `0` | Skip 1551 motor spin-up delay when set to `1`. |

### Rotary

| Option | Default | Meaning |
| --- | --- | --- |
| `RotaryEncoderEnable` | `0` | Enable KY-040 rotary navigation. |
| `RotaryEncoderInvert` | `0` | Reverse rotary direction. |

### Inherited Fields That Are Not User Settings

The source tree still contains some inherited Pi1541 fields and getters. They
are listed here to prevent confusion when reading the code, but they are not
valid `options.txt` settings in the current Pi1551 parser unless noted above.

| Option | Default | Note |
| --- | --- | --- |
| `ROM8` | empty | Present as storage in the code but not parsed. |
| `ROM1581` | `1581-rom.318045-02.bin` | Getter exists for inherited 1581 code paths, but it is not parsed. |
| `RAMBOard`/`extraRAM` split | `extraRAM = 0` | `extraRAM` exists in code but is not parsed. |
| `splitIECLines` | `1` | Inherited IEC option; not parsed for Pi1551. |
| `invertIECInputs` | `0` | Inherited IEC option; not parsed for Pi1551. |
| `invertIECOutputs` | `1` | Inherited IEC option; not parsed for Pi1551. |
| `supportUARTInput` | `0` | UART input code is disabled. |
| `autoBootFB128` / `C128BootSectorName` | unset | Inherited C128 browser support; not parsed here. |
| `screenWidth` / `screenHeight` | `1024` / `768` | Built-in HDMI dimensions; not parsed here. |
| `KeyboardBrowseLCDScreen` | `0` | Getter exists but current parser does not accept this option. |
| `buttonEnter`, `buttonUp`, `buttonDown`, `buttonBack`, `buttonInsert` | `1..5` | Defaults exist, but current parser does not accept remapping options. |

## Minimal Example

```text
deviceID = 8
ROM1 = dos1551.bin
ROM2 = dos1551-ram.bin
ROM3 = super_dos_1551.rom
ChargenFont = chargen
OnResetChangeToStartingFolder = 1
StarFileName = boot.t2sd
RAMBOard = 1
LCDName = ssd1306_128x64
i2cLcdAddress = 60
RotaryEncoderEnable = 1
SoundOnGPIO = 1
tapeMotorAlwaysOn = 1
skipMotorSpinUpDelay = 1
QuickBoot = 1
```

The repository template at `sdcard/options.txt` contains a fuller commented
configuration.

## Upgrading

For a normal firmware upgrade:

1. Build or download the new `kernel.img`.
2. Replace `kernel.img` in the SD card root.
3. Keep `options.txt`, ROM files, and `/1551` media in place.

If an update changes required SD support files, copy the changed files from the
repository `sdcard/` directory as well.

## Building Firmware

Pi1551 supports Raspberry Pi 3 family boards only: 3A, 3A+, 3B, and 3B+.
See [BUILD.md](BUILD.md) for toolchain installation and the canonical
`make RASPPI=3` build sequence.
