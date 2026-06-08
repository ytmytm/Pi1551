### PI1551 support: code review, issues, and build/run instructions (Raspberry Pi 3)

This document summarizes all changes on branch `pi1551` (including uncommitted edits) relative to `origin/master`, highlights potential issues and recommendations, and provides concise instructions to build with PI1551 support and prepare an SD card for a Raspberry Pi 3.

### Scope of changes vs master

- Added core 1551 emulation and TCBM bus layer
  - `src/Drive1551.{h,cpp}`: New 1551 drive heads/motor/track logic
  - `src/Pi1551.{h,cpp}`: 1551 system wrapper (CPU bus functions, TPI hookup)
  - `src/m6523.{h,cpp}`: MOS 6523 TIA/TPI model with CPU port and 100 Hz IRQ source
  - `src/tcbm_bus.{h,cpp}`: Hardware-level TCBM pin mapping, GPIO setup, debounce, LED/sound control, real-time pin I/O, latch of Port A, ACK/DAV/STATUS/DEV handling
  - `src/tcbm_commands.{h,cpp}`: SD2IEC-like command channel implementation over TCBM for directory, file I/O, device switching, new disk creation, error channel
  - `src/commands_base.{h,cpp}`: Base class extracting shared command functionality used by both IEC_Commands (1541) and TCBM_Commands (1551) for directory operations, file I/O, error handling, and common CBM commands
- Integration and UI
  - `src/main.cpp`: Large integration of 1551 path: `Emulate1551`, TCBM flow, LCD/graph updates, options, mount path default to `/1551`, deviceID handling via TPI Port C, fast boot loop adjusted to 2 MHz timing, core loops, reset semantics
  - `src/InputMappings.{h,cpp}`: Input layer with PI1551SUPPORT conditionals for 1551/TCBM and rotary encoder paths (note: not separate InputMappings1551 files)
  - `src/FileBrowser.{h,cpp}`: PI1551 conditionals to show device/ROM etc.
  - `src/ROMs.h`: Adds 1551 ROM image storage
  - `src/options.{h,cpp}`: Adds `ROM1551`, default `dos1551-318008-01.bin`, options mapped to TCBM path
  - `src/iec_commands.{h,cpp}`: Refactored to inherit from `Commands_Base`, extracting shared functionality for reuse by TCBM commands
  - `src/rpi-gpio.{c,h}`: Minor additions for GPIO usage
- Build system
  - `Makefile`: switches object list to 1551/TCBM set (1541/IEC objects are commented out). Uses `Makefile.rules` for RPi model flags; default `RASPPI=3`.
- Workspace and minor edits
  - `Pi1541.code-workspace` added
  - `src/defs.h` defines `PI1551SUPPORT 1` by default

### Potential issues and recommendations

- PI1551SUPPORT globally enabled
  - `src/defs.h` hard-enables `#define PI1551SUPPORT 1`. Recommendation: control via build flag (`-DPI1551SUPPORT`) or a dedicated config header so both 1541 and 1551 builds remain selectable without modifying sources.

- ROM read source in 1551 bus functions
  - In `read6502_1551` (see `Pi1551.cpp:50`) the ROM path returns `roms.Read(address)` for 0xA000+ ranges. 1551 has a dedicated 16 KiB ROM; consider using `roms.Read1551(address)` for clarity and to avoid selecting a 1541 slot accidentally if misconfigured. The `ROMs` class has a `Read1551()` method available (see `ROMs.h:38`).

- Timing and double-rate CPU stepping
  - `Emulate1551` runs `for (int cycle2MHz = 0; cycle2MHz < 2; ++cycle2MHz)` (see `main.cpp:1455`) then refreshes outputs and calls drive update (GCR) once per microsecond. This mirrors a 2 MHz 6502 but I/O running at 1 MHz. Code comment at lines 1474-1475 acknowledges this behavior. Not sure if this breaks some software.
  Note: CPU really runs at 2MHz, see https://www.softwolves.com/arkiv/cbm-hackers/15/15949.html

- Default LCD logo and messages
  - LCD logo defaults still reference 1541 assets. Not critical, but branding could be switched (or made configurable) for PI1551 builds.

### Raspberry Pi 3 GPIO 40-pin header (ASCII pinout) with project signals

```
                     +----------------- Raspberry Pi 3 (J8) -----------------+
  3V3           (01) |  o  |                                        (02)  o  | 5V
  GPIO2    SDA1 (03) |  o  |                                        (04)  o  | 5V
  GPIO3    SCL1 (05) |  o  |                                        (06)  o  | GND
  GPIO4     SW4 (07) |  o  |                                        (08)  o  | GPIO14  DIO1
  GND           (09) |  o  |                                        (10)  o  | GPIO15  DIO2
  GPIO17    SW3 (11) |  o  |                                        (12)  o  | GPIO18  DIO3
  GPIO27    SW1 (13) |  o  |                                        (14)  o  | GND
  GPIO22    SW2 (15) |  o  |                                        (16)  o  | GPIO23  DIO4
  3V3           (17) |  o  |                                        (18)  o  | GPIO24  DIO5
  GPIO10    LED (19) |  o  |                                        (20)  o  | GND
  GPIO9     DEV (21) |  o  |                                        (22)  o  | GPIO25  DIO6
  GPIO11  RESET (23) |  o  |                                        (24)  o  | GPIO8   DIO7
  GND           (25) |  o  |                                        (26)  o  | GPIO7   DIO8
  GPIO0  TSENSE (27) |  o  |                                        (28)  o  | ID_SC
  GPIO5     SW5 (29) |  o  |                                        (30)  o  | GND
  GPIO6  TMOTOR (31) |  o  |                                        (32)  o  | GPIO12  ACK (drive) --> DAV (tcbm2sd pin 11)
  GPIO13  SOUND (33) |  o  |                                        (34)  o  | GND
  GPIO19  TREAD (35) |  o  |                                        (36)  o  | GPIO16  STATUS0
  GPIO26 TWRITE (37) |  o  |                                        (38)  o  | GPIO20  DAV (drive) <-- ACK (tcbm2sd pin 13)
  GND           (39) |  o  |                                        (40)  o  | GPIO21  STATUS1
                     +-------------------------------------------------------+
```

Legend

- TCBM bus:
  - `DIO1..DIO8`: GPIO14,15,18,23,24,25,8,7 (bidirectional data bus)
  - `DAV` (input): GPIO20
  - `ACK` (output): GPIO12
  - `STATUS0` (output): GPIO16
  - `STATUS1` (output): GPIO21
  - `DEV` (output): GPIO9
  - `RESET` (input): GPIO11
- I/O:
  - Buttons: `SW1`=GPIO27, `SW2`=GPIO22, `SW3`=GPIO17, `SW4`=GPIO4, `SW5`=GPIO5
  - Rotary encoder (`SW1`, `SW2`, `SW3`)
  - I2C display: `SDA1`=GPIO2, `SCL1`=GPIO3
  - Sound / head-step: GPIO13 (physical pin 33) — see **Head step sound** below
  - LED: GPIO10

### Head step sound (`SoundOnGPIO` in `options.txt`)

The option name is misleading: it does **not** mean “sound on/off”. It selects **how** head-step audio is driven on **GPIO 13** (the `SOUND` line on the Pi1551 hat). Audio does **not** go to HDMI or the Pi’s 3.5 mm jack — Pi1541/Pi1551 firmware is bare-metal and only uses the SoC **PWM peripheral** or **GPIO bit-banging** on that pin.

| `SoundOnGPIO` | What the firmware does | What you hear |
|---------------|------------------------|---------------|
| **0** (default in `options.cpp`) | On head move: `PlaySoundDMA()` — DMA feeds embedded `Sample.bin` into the PWM FIFO (~8-bit mono, comment in code: 44100 Hz). GPIO 13 must be in **ALT0** (PWM1), not digital output. | Short “click” from the sample (legacy Pi1541 path). |
| **1** | On head move: start a square-wave buzzer on GPIO 13 (`OutputSound` toggled in the emulation loop). `SoundOnGPIODuration` / `SoundOnGPIOFreq` apply. GPIO 13 stays a **digital output**. | Continuous buzz while the head steps (~duration ms, ~freq Hz). |

**Recommended on Pi1551 hardware** (piezo/buzzer wired to GPIO 13): `SoundOnGPIO = 1`.

#### Two different code paths (easy to confuse)

1. **Head direction change** (`Emulate1551` in `main.cpp`): `if (options.SoundOnGPIO())` → start GPIO buzzer counter; `else` → `PlaySoundDMA()`.
2. **Per-microsecond buzzer loop**: `if (options.SoundOnGPIO() && headSoundCounter > 0)` → toggle `TCBM_Bus::OutputSound`. When `SoundOnGPIO = 0`, this block never runs — there is **no** GPIO square wave in that loop (that part is “off”), but `PlaySoundDMA()` may still run from (1).

So `0` means “do not use the GPIO buzzer loop”, not “mute everything” in the original upstream design — unless `PlaySoundDMA()` is also removed or fails.

#### Where the legacy PWM signal appears (Raspberry Pi 3 / BCM2835)

- DMA destination: `PWM_FIF1` (PWM channel 1 FIFO), see `PlaySoundDMA()` and `TCBM_Bus::Initialise()` in `tcbm_bus.h`.
- PWM1 on BCM2835 is available on **GPIO 13** or GPIO 19 when the pin is **ALT0**.
- On Pi1551, GPIO 13 is the hat `SOUND` net; GPIO 18/19 are used for TCBM/tape and are not free for PWM audio.
- **HDMI**: not used for this sound path.

#### Implementation notes (Pi1551 branch)

- `RefreshOuts1551()` must not drive GPIO 13 as a digital line when `SoundOnGPIO = 0`, or it blocks PWM on the same pin.
- With `SoundOnGPIO = 1`, LED and buzzer are pushed every refresh (IEC-style), independent of the TPI early-exit optimisation.

### Tape interface

All signals are unidirectional. All are connected through a NPN transistor with appropriate 10K pullup on collector: to 3.3V for inputs, to 5V (from C16 side, NOT from RPi3) for outputs.
Input signal goes through 10K resistor to base, emitter goes to GND. Collector is also connected to the signal output.
Any NPN works: 2n3904, S8050, BC547

- `TAPE_MOTOR` (input): GPIO6 (pin 31) - active high, controls tape playback
- `TAPE_READ` (output): GPIO19 (pin 35) - tape data output to Plus/4
- `TAPE_WRITE` (input): GPIO26 (pin 37) - reserved for future recording support
- `TAPE_SENSE` (output): GPIO0 (pin 27) - tape sense output to Plus/4
  - Hardware inverts the signal: GPIO HIGH (1) → output LOW (active/PLAY pressed), GPIO LOW (0) → output HIGH (inactive/PLAY released)
  - When SENSE=1 (senseLineState=true): GPIO is set HIGH, hardware inverts to output LOW, computer sees LOW = active state (PLAY pressed)
  - When SENSE=0 (senseLineState=false): GPIO is set LOW, hardware inverts to output HIGH, computer sees HIGH = inactive state (PLAY released)

Note: check in `defs.h` if the support for MOTOR line is enabled or if we assume motor is always active: `TAPE_MOTOR_SUPPORT`

### C16/Plus4 TAPE connector

Mini DIN 7

Looking at the back of computer

```
        u
    7   6   5
    4  ===  3
     2     1
```

```
1 GND
2 5V
3 MOTOR (C= output)
4 READ (C= input)
5 WRITE (C= output)
6 SENSE (C= input)
7 GND
```

Note that the easiest way of soldering a flat cable is to put wires in order around the plug: 2,4,7,6,5,3,1.

### TCBM2SD - TCBM Connector


```
GND      1	2  DEV
DIO1     3	4  DIO2
DIO3     5	6  DIO4
DIO5     7	8  DIO6
DIO7     9	10 DIO8
DAV     11	12 STATUS0
ACK     13	14 STATUS1
/RESET  15	16 ALT_A6/GND
```

##### ACK / DAV

**IMPORTANT**

ACK here is controller's ACK! It is an output and goes to cable pin 13 (drive's DAV input)

DAV here is controller's DAV! It is an input and goes to cable pin 11 (drive's ACK output)

ALT_A6/GND must be grounded to disable onboard Arduino, DON'T connect cable to TCBM connector when computer is ON

##### RESET

**IMPORTANT**

`/RESET` CAN'T BE DIRECTLY CONNECTED to TCBM on tcbm2sd 1.3 boards - it's a 5V system /RESET, not 3.3V /RESET_3_3_V from CPLD.

You can cut the trace on tcbm2sd v1.3 and (optionally) add a wire to join pin 15 of TCBM connector with /RESET_3_3V from JP1
(left side). If Pi1551 gets stuck on boot you also have to add a pullup resistor (3.3K or larger) to 3.3V.

Pi1551 hat **MUST** have a jumper for that with instruction that a jumper *MUST* be open for
tcbm2sd 1.3 boards unless there was a manual patch

Pi1551 hat fix option with a diode:

```
                +3.3V
                  |
                  Rp  (10k typ.)
                  |
/RESET_IN  ---|<--+-> /RESET_OUT
               D
```

Rp - 10K
D - BAT54/BAS40

### Build instructions (Raspberry Pi 3, PI1551)

Prereqs (on build host): `arm-none-eabi` toolchain in PATH.

Build steps from repository root `Pi1541/`:

```bash
make clean && make RASPPI=3 V=1`
```

Notes:
- `Makefile` already compiles the PI1551 variant by default: objects include `Drive1551.o`, `Pi1551.o`, `commands_base.o`, `tcbm_bus.o`, `tcbm_commands.o`, `m6523.o`, `InputMappings.o` and exclude IEC/1541 objects (commented out on line 9).
- `src/defs.h` has `#define PI1551SUPPORT 1`. If you prefer toggling via compiler define, remove that line and pass `CPPFLAGS+=-DPI1551SUPPORT`.
- Output artifacts: `kernel.elf`, `kernel.img`, `kernel.lst`, `kernel.map` in project root.

### SD card preparation (Raspberry Pi 3)

On a FAT32-formatted SD card (single partition is fine):

- Place firmware and kernel
  - Copy `kernel.img` from the project root to the SD card root.
  - Provide a minimal `config.txt` suited for Pi3. Example:
    ```
    kernel=kernel.img
    disable_overscan=1
    gpu_mem=64
    arm_64bit=0
    dtoverlay=disable-bt
    enable_uart=1
    core_freq_min=250
    force_turbo=1
    ```

- Create expected directories and files
  - Create `/1551` folder at the SD card root and place disk images there (e.g. `.d64`, `.g64`) and any subfolders.
  - Create `/roms` (optional) for additional 1541/1581 font ROMs if used.
  - Ensure the 1551 DOS ROM file is present at SD root (or `options.txt` path): default name is `dos1551-318008-01.bin`. It is loaded via the `ROM1551` option; if unspecified, the default filename above is tried.
  - Put `options.txt` in SD root. Minimal example for PI1551:
    ```
    splitIECLines = 1 // must be 1 for i2c bus 1
    i2cBusMaster = 1 //SDA - pin 3 SCL - pin 5
    deviceID = 8
    ROM1551 = dos1551-318008-01.bin
    LCDName = ssd1306_128x64
    i2cLcdAddress = 60
    lowerCaseBrowseModeFilenames = 1
    ignoreReset = 0
    ```

- Optional: icons and logos
  - If using LCD logos, you can place a raw logo file referenced by `LCDLogoName`.

Insert the SD card into a Raspberry Pi 3 and power on. The emulator starts, changes working directory to `/1551`, and exposes TCBM-based browse and image mount. Use front-panel buttons/rotary encoder as configured.

### Quick usage notes

- Device switching and browse commands are implemented in `tcbm_commands.cpp` (SD/USB selection, mkdir/rmdir, copy, scratch, rename).
- To auto-mount an image at boot, set `AutoMountImage = <filename>` in `options.txt` (path relative to current folder after boot; for PI1551 default folder is `/1551`).

### Follow-ups recommended

- Make `PI1551SUPPORT` selectable by build-time flag; default to 1541 for backward compatibility.
- Audit ROM fetch in 1551 path to use `ROMs::Read1551` consistently.
- Add runtime fallback if `/1551` folder is missing (create or use `/`).


