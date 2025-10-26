### PI1551 support: code review, issues, and build/run instructions (Raspberry Pi 3)

This document summarizes all changes on branch `pi1551` (including uncommitted edits) relative to `origin/master`, highlights potential issues and recommendations, and provides concise instructions to build with PI1551 support and prepare an SD card for a Raspberry Pi 3.

### Scope of changes vs master

- Added core 1551 emulation and TCBM bus layer
  - `src/Drive1551.{h,cpp}`: New 1551 drive heads/motor/track logic
  - `src/Pi1551.{h,cpp}`: 1551 system wrapper (CPU bus functions, TPI hookup)
  - `src/m6523.{h,cpp}`: MOS 6523 TIA/TPI model with CPU port and 100 Hz IRQ source
  - `src/tcbm_bus.{h,cpp}`: Hardware-level TCBM pin mapping, GPIO setup, debounce, LED/sound control, real-time pin I/O, latch of Port A, ACK/DAV/STATUS/DEV handling
  - `src/tcbm_commands.{h,cpp}`: SD2IEC-like command channel implementation over TCBM for directory, file I/O, device switching, new disk creation, error channel
- Integration and UI
  - `src/main.cpp`: Large integration of 1551 path: `Emulate1551`, TCBM flow, LCD/graph updates, options, mount path default to `/1551`, deviceID handling via TPI Port C, fast boot loop adjusted to 2 MHz timing, core loops, reset semantics
  - `src/InputMappings1551.{h,cpp}`: Input layer aligned with 1551/TCBM and rotary encoder paths
  - `src/FileBrowser.{h,cpp}`: PI1551 conditionals to show device/ROM etc.
  - `src/ROMs.h`: Adds 1551 ROM image storage
  - `src/options.{h,cpp}`: Adds `ROM1551`, default `dos1551-318008-01.bin`, options mapped to TCBM path
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
  - In `read6502_1551` the ROM path returns `roms.Read(address)` for 0xA000+ ranges. 1551 has a dedicated 16 KiB ROM; consider using `roms.Read1551(address)` for clarity and to avoid selecting a 1541 slot accidentally if misconfigured.

- Timing and double-rate CPU stepping
  - `Emulate1551` runs `for (int cycle2MHz = 0; cycle2MHz < 2; ++cycle2MHz)` then refreshes outputs and calls drive update (GCR) once per microsecond. This mirrors a 2 MHz 6502 but I/O running at 1 MHz. Not sure if this breaks some software.
  Note: CPU really runs at 2MHz, see https://www.softwolves.com/arkiv/cbm-hackers/15/15949.html

- EOI and command parsing on TCBM
  - `ReadIECSerialPort`/`WriteIECSerialPort` carry `STATUS` with EOI bits, but `Listen` and `Talk` contain TODOs to specialize for command/data phase codes. Consider refactoring to stateful read/write that also verifies busCommandCode framing to harden against edge cases.

- Directory header branding
  - `tcbm_commands.cpp` sets directory header to "PI1541". Consider a variant string like "PI1551" when PI1551 is compiled to avoid user confusion.

- Default LCD logo and messages
  - LCD logo defaults still reference 1541 assets. Not critical, but branding could be switched (or made configurable) for PI1551 builds.

- Hardcoded working directory
  - `main.cpp` `f_chdir("/1551");` under PI1551. Ensure SD card root contains `/1551` and that file browser, auto-mount, and ROM discovery expect this path. Consider falling back to `/` if the folder is missing.

- Device selection and DEV line behavior
  - verify that changing the device number to 9 works in browser and in emulation modes

- GPIO pin allocations and pull-ups
  - `tcbm_bus.h` defines a set of pins and masks; verify collisions with LED, SOUND, and SPI0_RS for your specific HAT/wiring. Ensure no overlap with I2C pins used by LCD if enabled.

- Error message text
  - Some error strings and version banners still say PI1541; consider PI1551-specific wording where shown on directory header and error channel 73.

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
  GPIO10   LED* (19) |  o  |                                        (20)  o  | GND
  GPIO9     DEV (21) |  o  |                                        (22)  o  | GPIO25  DIO6
  GPIO11  RESET (23) |  o  |                                        (24)  o  | GPIO8   DIO7
  GND           (25) |  o  |                                        (26)  o  | GPIO7   DIO8
  ID_SD         (27) |  o  |                                        (28)  o  | ID_SC
  GPIO5     SW5 (29) |  o  |                                        (30)  o  | GND
  GPIO6 SPI0_RS (31) |  o  |                                        (32)  o  | GPIO12  ACK (drive) --> DAV (tcbm2sd pin 11)
  GPIO13  SOUND (33) |  o  |                                        (34)  o  | GND
  GPIO19        (35) |  o  |                                        (36)  o  | GPIO16  STATUS0
  GPIO26        (37) |  o  |                                        (38)  o  | GPIO20  DAV (drive) <-- ACK (tcbm2sd pin 13)
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
  - I2C display: `SDA1`=GPIO2, `SCL1`=GPIO3
  - Sound (PWM): GPIO13
  - SPI0_RS (if used): GPIO6
  - LED: GPIO10

### TCBM2SD - TCBM Connector


```
GND	     1	2	DEV
DIO1	   3	4	DIO2
DIO3	   5	6	DIO4
DIO5	   7	8	DIO6
DIO7	   9	10	DIO8
DAV	    11	12	STATUS0
ACK	    13	14	STATUS1
/RESET	15	16	ALT_A6/GND
```

##### ACK / DAV

* IMPORTANT *

ACK here is controller's ACK! It is an output and goes to cable pin 13 (drive's DAV input)
DAV here is controller's DAV! It is an input and goes to cable pin 11 (drive's ACK output)

/RESET - CAN'T BE CONNECTED on tcbm2sd 1.3 boards - it's 5V system /RESET
cut the wire or trace and patch manually to join with /RESET_3_3V from JP1 (one side of it)

Pi1551 hat *MUST* have a jumper for that with instruction that a jumper *MUST* be open for
tcbm2sd 1.3 boards unless there was a manual patch

Pi1551 hat fix option (TODO: test that on falstad):

```
                +3.3V
                  |
                  Rp  (10k typ.)
                  |
/RESET_IN  ---|<|-+-> /RESET_OUT
               D
```

Rp - 10K
D - BAT54/BAS40
- Katoda diody (|<|) jest po stronie /RESET_IN (wejście 3.3 V/5 V).
- Anoda diody jest w węźle wyjściowym /RESET_out (z pull-upem do 3.3 V).

/RESET_IN = LOW (aktywny) → dioda przewodzi → /RESET_out ≈ 0 V.
/RESET_IN = HIGH (3.3 V lub 5 V) → dioda zaporowo → /RESET_out podciągnięty do 3.3 V przez Rp.


### Build instructions (Raspberry Pi 3, PI1551)

Prereqs (on build host): `arm-none-eabi` toolchain in PATH.

Build steps from repository root `Pi1541/`:

```bash
make clean && make RASPPI=3 USE_DRIVE1551_CLEAN=1 USE_PI1551_CLEAN=1 V=1`
```

Notes:
- `Makefile` already compiles the PI1551 variant by default: objects include `Drive1551.o`, `Pi1551.o`, `tcbm_bus.o`, `tcbm_commands.o`, `m6523.o`, `InputMappings1551.o` and exclude IEC/1541 objects.
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
    deviceID = 8
    ROM1551 = dos1551-318008-01.bin
    LCDName = ssd1306_128x64
    I2CLcdAddress = 60
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
- Validate Port C to GPIO bit mappings for `DEV`, `ACK`, `STATUS0/1` and adjust `RefreshOuts1551` masks accordingly.
- Hook DDRA writes to immediately update GPIO data bus direction to avoid transient mismatches.
- Update directory header string and version reporting to PI1551 branding.
- Add runtime fallback if `/1551` folder is missing (create or use `/`).


