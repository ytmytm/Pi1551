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
OLED media picker. The current menu targets the OLED, not the legacy HDMI UI.

Use browser mode to:

- Load PRG files directly from SD.
- Browse folders on the SD card.
- Select a D64 or G64 for 1551 emulation.
- Open D71, D81, D80, or D82 images as read-only file containers.
- Load a TAP file into the tape player.
- Record a new or loaded TAP from a C16/C116/Plus/4 cassette port.
- Create new D64 or G64 images from the UI.

### 1551 Emulation Mode

Selecting a D64 or G64 starts cycle-exact Commodore 1551 emulation. The emulated 1551
has its own 6502, 6523 TPI, DOS ROM, drive mechanics, and disk image state.

D64 and G64 are the emulation formats offered by the current OLED menu.
D71/D80/D81/D82 and PRG use file-level container access, not drive emulation.

### Tape Player

Selecting a TAP file loads it into the tape player instead of entering disk
emulation. Playback and recording use the C16/C116/Plus/4 tape port through
the Pi1551 interface. Recording uses the WRITE input (GPIO26, pin 37).

Supported TAP files must use the Plus/4/C16 TAP signature `C16-TAPE-RAW`.
Versions 0, 1, and 2 are accepted. PAL/NTSC timing is taken from the TAP
header.

The tape status is always visible on the OLED. It shows a three-digit counter,
motor state, percentage and PLAY (`P`) or RECORD (`R`), or dashes when no TAP
is loaded.

## File And Image Behavior

### SD Files

In browser mode, Pi1551 provides file-level access to files and folders on the
SD card. This is the mode used by file browsers and normal `LOAD` operations
that do not require full drive emulation.

From the Commodore, `LOAD "filename",8` (or `DLOAD`) on a `.prg` file reads the
program bytes directly from the SD card. No disk image is created and 1551
emulation is not started.

Directory listings (`LOAD "$",8`) mark entries as follows:

| Listing type | Meaning |
| --- | --- |
| `DIR` | Subfolder, or a mountable disk image (`.d64`, `.d71`, `.d81`, `.d80`, `.d82`, any case) |
| `PRG` | Program file or other non-directory file on SD (including `.prg`, `.tap`, extensionless files such as `boot.t2sd`, and so on) |

### PRG Files And Virtual Disks

Enter on a standalone `.prg` in the OLED browser wraps it in a temporary
one-file D64 in RAM and mounts it as a read-only container. The original SD
file is unchanged and the drive CPU does not enter emulation.

| Action | Result |
| --- | --- |
| Enter on a highlighted `.prg` | Mounts its virtual disk and opens Fliplist view, without emulation. |
| Insert on a highlighted `.prg` | Adds/removes it from the mixed-format fliplist; does not mount it. |
| Normal Commodore `LOAD "program",8` in SD Browser | Reads the original PRG directly, without creating or mounting a disk. |

Once mounted, the program is the only file on the virtual disk exposed to
the computer. Use normal `LOAD`/`RUN`. T64/NIB/NBZ are not offered as mountable
media by this OLED menu merely because inherited code recognizes them.

### D64 Images

D64 images can be mounted for 1551 emulation. This is the compatibility mode
for software that expects real drive timing or drive-side code execution.

D64 images can also be opened in browser-style contexts when appropriate.

### D71, D81, D80, D82 Images

D71, D81, D80, and D82 images are supported as read-only file containers. You
can browse directories and load files from inside them, but Pi1551 does not
emulate those drive mechanisms as 1551 hardware.

### LST Files

Enter on a nonempty `.lst` loads its fliplist and mounts the first valid
entry. Lines name D64/G64/D71/D80/D81/D82/PRG media, relative to the LST file's
folder or by absolute SD path. The OLED menu does not write or edit LST files.

## OLED Menu And Controls

The current front-panel UI targets 128x64 SSD1306/SH1106 displays: eight rows
of 16 characters. The first two rows remain status rows; the next six are
menu/list content. A selectable Menu... row is always available in Browser
and Fliplist. There is no hidden longpress menu or longpress ROM selector.

### Encoder And Buttons

Enable the encoder at startup with RotaryEncoderEnable = 1. Fixed hat wiring:

| Signal | GPIO | Hat switch |
| --- | ---: | --- |
| A / CLK | 17 | SW3 |
| B / DT | 22 | SW2 |
| Pushbutton | 27 | SW1 |
| Back | 4 | SW4 |
| Insert | 5 | SW5 |

| Control | Action |
| --- | --- |
| Rotate left/right | Move highlight; in Fliplist, highlighting an image mounts it immediately. |
| Encoder click (Enter) | Select an item, toggle/cycle a setting, enter a folder, or mount media. |
| Insert in Browser | Add/remove the highlighted supported image or PRG from LIST; entering a folder does not add it. |
| Insert in Fliplist | Enter Browser in LIST mode, retaining entries and unmounting the image so you can add more media. |
| Back in a submenu | Return to its parent, like selecting the first item, .. . |
| Back in Browser | Go to the parent SD folder; at SD root it can restore the saved caller/Fliplist. |
| Enter on a Fliplist image | Unmount and return to its containing SD folder, never inside the image. |
| Back in Fliplist | Unmount and enter Browser, retaining LIST but clearing a temporary SOLO image. |

Each submenu starts with ..; entry initially highlights the next item.
Inverted text is the cursor; the ROM triangle marks the active ROM, not the
cursor. Clicking another ROM switches immediately. Clicking the already
active ROM or Back exits. Only Settings -> ROM changes ROMs; files named
.bin/.rom or folders named roms have no special selection action in Browser.

The old hold-button combinations and longpress ROM selector are not the
controls of this OLED UI. The [HDMI and USB keyboard section](#hdmi-and-usb-keyboard)
explains the supported startup display and current input limitations.
A USB keyboard is not a substitute for the encoder menu. For desktop testing,
run make tui: left/right rotate, Enter clicks, i is Insert, b is Back,
r simulates computer RESET, m toggles computer MOTOR, and q quits.

### Complete Menu Tree

Values shown below are choices, not extra menu levels. A submenu's .. always
returns to its parent. Status entries and the loaded TAP filename are informational.

~~~text
Browser / Fliplist
└─ Menu...                         (LIST and count appear when applicable)
   ├─ ..
   ├─ Clear list                   (only when there are entries)
   ├─ Browser                      (clear list and unmount)
   ├─ Tape
   │  ├─ ..
   │  ├─ 1:<loaded TAP filename>    (or just 1: when empty; scrolls)
   │  ├─ Play / Stop                (only when TAP is loaded; toggles with state)
   │  ├─ Go to file...              (matching IDX loaded and tape stopped)
   │  │  ├─ ..
   │  │  └─ indexed tape positions
   │  ├─ Load TAP...                (only when no TAP is loaded)
   │  ├─ Record                     (no TAP or loaded TAP stopped)
   │  │  ├─ ..
   │  │  ├─ New TAP                  (create in the current SD folder)
   │  │  └─ Current TAP              (only when a TAP is loaded)
   │  ├─ Eject TAP                  (only when loaded TAP is stopped)
   │  ├─ Rewind                     (only when loaded TAP is stopped)
   │  └─ Go to counter              (only when loaded TAP is stopped)
   │     ├─ ..
   │     └─ TARGET 000..999
   ├─ Disk
   │  ├─ ..
   │  ├─ WProt ON/OFF
   │  ├─ Write image
   │  ├─ New image
   │  │  ├─ ..
   │  │  ├─ Type D64/G64
   │  │  └─ Create image
   │  └─ Reset drive
   ├─ Settings
   │  ├─ ..
   │  ├─ ROM
   │  │  ├─ ..
   │  │  └─ Configured ROM1..ROM7 filenames, active ROM marked by triangle
   │  ├─ Drive
   │  │  ├─ ..
   │  │  ├─ Device number 8/9
   │  │  ├─ Idle trap ON/OFF
   │  │  ├─ RAMBOard ON/OFF
   │  │  ├─ Skip spinup ON/OFF
   │  │  └─ Reset REACT/IGNORE
   │  ├─ Tape
   │  │  ├─ ..
   │  │  ├─ Motor ON ALWAYS/COMP.
   │  │  └─ Autoplay ON/OFF
   │  ├─ Browser
   │  │  ├─ ..
   │  │  ├─ Auto mount ON/OFF        (valid AutoMountImage required)
   │  │  ├─ Reset path ON/OFF
   │  │  ├─ Lowercase ON/OFF
   │  │  └─ SD2IEC Cmds. ON/OFF
   │  ├─ Display
   │  │  ├─ ..
   │  │  ├─ Temp ON/OFF
   │  │  ├─ Contrast 16,32,...,240,255
   │  │  ├─ Dimming ON/OFF
   │  │  ├─ Dim time OFF/5/10/30/60/120/300s
   │  │  ├─ Dim level 0,16,32,...,240,255
   │  │  ├─ Orient normal/flip
   │  │  ├─ Font ASCII/C64
   │  │  └─ Scroll 0.05/0.10/0.20/0.35/0.50/1.00s + scrolling demo
   │  ├─ Encoder
   │  │  ├─ ..
   │  │  └─ Reverse ON/OFF
   │  ├─ Sound
   │  │  ├─ ..
   │  │  ├─ Sound ON/OFF
   │  │  ├─ Mode SAMPLE/BUZZER
   │  │  ├─ Freq 1200/1600/2000 Hz   (BUZZER only)
   │  │  └─ Duration 0/50/100/250/500/1000/2000/5000ms (BUZZER only)
   │  ├─ Date & time
   │  │  ├─ ..
   │  │  ├─ Year 2000–2099
   │  │  ├─ Month 01–12
   │  │  ├─ Day 01–28/29/30/31
   │  │  ├─ Hour 00–23
   │  │  └─ Minute 00–59
   │  ├─ Load settings
   │  └─ Save settings
   ├─ Status
   │  ├─ ..
   │  ├─ DATE YYYY-MM-DD          (local clock, or unset)
   │  ├─ TIME HH:MM:SS           (local clock, or unset)
   │  ├─ TEMP, SD size, FREE space, FW version
   │  ├─ DOS filename, current image filename, TAP filename
   │  └─ LED, MOTOR, PLAY, TRACK (S while idle)
   └─ Reboot Pi1551
      ├─ ..
      ├─ Cancel                    (initial selection)
      └─ Reboot Pi1551?
~~~

In Date & time, click a field, rotate to change it, and click again to set the
local clock; Back cancels the edit. Status shows that same local clock.

### Browser, SOLO And LIST

Folders are shown as [name]; disk images are not bracketed. Enter on D64/G64
mounts and starts emulation. Enter on D71/D80/D81/D82 or PRG mounts a file
container without emulation. Either action opens Fliplist view, even for one
image: container contents are listed to the Commodore, not on the OLED.
Simply browsing an SD folder selects that folder's virtual file-list source;
there is no separate drive-mode switch or Use this dir item.

Enter alone mounts a temporary SOLO image. Returning to Browser clears that
one-item list, so an unexpected 1> prefix is not left behind. To prepare a
persistent LIST, use Insert: it toggles membership, with 1>, 2>, etc. shown in
Browser. While building LIST, Enter on the final image adds it if necessary,
mounts it and opens Fliplist. Enter on an existing member activates it.
LST selection also creates LIST. Mixed D64/G64 and container entries switch
between emulation and file-level access automatically.

In Fliplist, Menu... remains selectable. Rotate onto an image to mount it
immediately, including the first image after Menu...; rotating from the last
image wraps to the first, and rotating left from the first reaches Menu... .
Enter on an image returns to the SD folder containing it and unmounts it.
LIST survives that return; SOLO does not. Menu -> Browser deliberately clears
the list and unmounts, while Clear list explicitly clears it without being a
separate list-editing menu.

The header shows, for example, 8:games (the last SD folder) or 8: 18.0S
(track/half-track while emulating). A long folder name scrolls independently
of the selected row. Temp ON reserves four rightmost characters for a space
and temperature; Temp OFF gives the directory the full remaining width.
Selected long media/ROM filenames scroll with their prefixes fixed.

Tape status always shows TAPE, counter, percent and motor/transport flags. No TAP is
TAPE --- --% M- or TAPE --- --% --; a loaded tape at the beginning has
000 00%, not missing-file dashes. MOTOR reflects the computer input (or
ALWAYS override) even without a tape. PLAY may be pressed automatically on
load, but Stop, Rewind and Go to counter release it; RECORD shows `R` and also
presses PLAY. The MOTOR flag is refreshed with the OLED status; it is not
gated by whether a TAP is loaded.

### SD2IEC-Style Browser Commands And The Panel

`SD2IEC Cmds. ON` enables the computer-facing browser-mode file service:
directory listings, direct file `LOAD`/`SAVE`, and its command channel.
`OFF` disables that service while leaving OLED browsing available. The name
means compatibility with a useful subset of the SD2IEC/CMD command vocabulary;
Pi1551 does not claim to implement the complete sd2iec firmware command set.
The setting has no effect while a D64 or G64 is running in cycle-exact 1551
emulation.

The following SD2IEC/CMD-style directory and device commands are implemented
in browser mode:

| Command | Pi1551 behavior |
| --- | --- |
| `CD:path` | Change SD directory, or select a supported image/container. |
| `CD_`, `CD:_`, `CD..`, `CD:..` | Leave an image or move to the parent SD directory. `_` represents the Commodore left-arrow character. |
| `CD/` / `CD//` | Select the SD root / the `/1551` starting folder. |
| `MD:name` | Create a subdirectory relative to the current directory. `MD//name` creates it below the `/1551` starting folder; partition prefixes other than zero are not supported. |
| `RD:name` | Remove a subdirectory of the current directory. |
| `CP0` | Select the SD card. |
| `CP1`, `CP2`, ... | Select the corresponding available USB mass-storage device. This is Pi1551's storage-device mapping, not sd2iec's general partition model. |
| `XS:name.lst` | Load the named LST swap list and mount its first valid entry, using the same Fliplist behavior as panel selection. |
| `XS` | Disable and clear the current swap list without deleting its LST file. |
| `P` + channel byte + up to four little-endian offset bytes | Seek an open regular file (or a file inside a mounted container) to a 0-based 32-bit byte position. Omitted high bytes are zero. |
| `XI0`, `XI1`, `XI2` | Show mountable images as PRG, as DIR, or twice (PRG and DIR). Pi1551 deliberately defaults to `XI1` (DIR); the setting lasts until reboot and `XW` is not supported. |
| `$:pattern=type` | Filter browser-directory listings with `*` and `?`. Types `P`, `S`, `U`, `R`, and directory types `B`/`D` are accepted; `=H` includes hidden FAT entries. |
| `$=T[:pattern[=type,L,N]]` | Return FAT timestamps in CMD short form, or long form with `L`; `N` applies the other filters but uses the normal listing format. Date-range predicates are not implemented. |
| `$=P[:pattern]` | List Pi1551 storage targets as partitions: `SD`, then `USB01`, `USB02`, ...; every entry is reported as `FAT`. |
| `G-P` + optional binary partition byte | Return the 30-byte CMD-compatible partition information record plus carriage return. With no byte or `$ff`, report the current target; Pi1551 partition 0 is SD and 1+ are USB targets. |
| `X*+`, `X*-` | Enable modern 1581-style matching of text after `*`, or use classic matching that ignores everything after the first `*`. Default: `X*+`. |
| `XE+`, `XE-` | Enable or disable hiding `.PRG`, `.SEQ`, `.USR`, and `.REL` extensions in listings and filename matching; the extension becomes the Commodore file type. Default: `XE-`. |
| `X` | Report volatile extended settings on the error channel (`I` image mode, `E` extension hiding, `*` wildcard mode, and device number). |
| `T-RA` / `T-WA...` | Read/write `SUN. 01/20/08 01:23:45 PM` ASCII time. |
| `T-RI` / `T-WI...` | Read/write the supported ISO form `2008-01-20T13:23:45`; reads append the weekday. |
| `T-RD` / `T-WD...` | Read/write the nine-byte decimal format (weekday, year since 1900, month, day, 12-hour time, AM/PM, CR). |
| `T-RB` / `T-WB...` | Read/write the equivalent BCD format, with a modulo-100 year. |

For example, `LOAD"$=P",8` followed by `LIST` shows `SD` and any attached
`USB01`, `USB02`, ... targets as `FAT` entries; it is not the ordinary file
listing. Use the configured device number if it is not 8. Directory filters
such as `LOAD"$:*.PRG",8` are likewise part of the `LOAD` filename, not
commands sent to channel 15.

USB mass-storage devices are detected and mounted during startup. Connect a
USB drive before boot; plugging one in later does not add it. After boot, SD is
the active device when no USB drive was detected. When at least one USB drive
was detected, `USB01` is active by default. `CP0` switches back to SD, while
`CP1`, `CP2`, ... select the corresponding mounted USB drives. The selected
device is the default for ordinary file and directory operations until another
`CP` command changes it.

`T-W` and Settings → Date & time use the same software clock, which advances from
the Raspberry Pi system timer (`TimerGetTicks`). It survives drive/bus resets but
not a power cycle or reboot. Until it is set, `T-R` returns `31,SYNTAX ERROR,00,00`
and new SD files get a fixed 2026-11-01 FatFs stamp. After it is set, FatFs uses
the wall clock for create/modify times. If an I2C RTC answers at `i2cRtcAddress`
(default `0x68`) after HDMI/LCD init and its time is valid, that value is loaded
into the wall clock once at boot. The OLED editor uses years 2000–2099; `T-W`
accepts the wider SD2IEC range. Times are local 24-hour values; the firmware
does not infer a timezone.

The same browser command channel also implements normal file operations used
by SD2IEC-compatible software: `C` (copy/concatenate), `N` (create and mount a
new D64/G64 using the configured `NewDiskType`), `R` (rename), and `S`
(scratch/delete files). `X?` returns the Pi1551 DOS version. Settings changed
by `XI`, `XE`, and `X*` are intentionally volatile. `XW` is not implemented,
so the preferred `XI1` image-as-DIR default cannot be overwritten. Pi1551 also
intentionally does not implement `XR`, `M-R`/`M-W`/`M-E`, `U1`/`U2`,
`B-R`/`B-W`/`B-P`, or raw-device `DI`/`DR`/`DW`. Other sd2iec commands should
be treated as unsupported unless explicitly listed above.

For the complete upstream command language, consult the
[sd2iec README](https://github.com/thierer/sd2iec) and the expanded
[C64 OS SD2IEC User's Manual](https://c64os.com/post/sd2iecdocumentation).
Commands described there should be treated as unsupported unless they are
explicitly listed above.

Commodore command-channel navigation updates the OLED as well as the file
source. CD into a folder shows Browser. CD selecting an image mounts it and
shows Fliplist using the same SOLO/LIST rules as panel selection.

CD.. or CD followed by the Commodore left-arrow character (often written
CD<- or CD_) exits a mounted image into its containing SD folder. Send it
again to go up one SD folder. This applies to D64/G64 emulation and to
D71/D80/D81/D82/PRG containers, including images selected from a fliplist.
At SD root, a parent command stays there. CD/ selects the SD root; CD//
selects /1551. A host directory change also leaves any open OLED submenu;
Back cannot restore an obsolete view and remount media unexpectedly.

An explicit LIST survives the host's exit; a temporary SOLO image does not.
TAP stays loaded. A plain directory refresh does not interrupt the current
submenu. Failed image flush/unmount keeps the mounted medium and reports an
error. The panel's folder header and the computer's subsequent directory
listing refer to the same directory after the change.

### Runtime Settings And Actions

Drive settings affect the device number, idle trap, RAM expansion, spinup
delay and drive RESET handling. Reset REACT resets the drive CPU and permits
reset-time automount. Reset IGNORE suppresses those drive actions, but Reset
path ON still returns the SD Browser source to /1551 on computer RESET.
Reset path OFF keeps the folder. Neither path choice alone clears a list,
unmounts the disk or ejects/rewinds TAP. Auto mount only appears when the
startup-configured AutoMountImage is a valid supported image or nonempty LST;
missing, empty or invalid files behave as OFF. The toggle applies at boot and
the next REACT reset, not as an immediate mount command.

Motor ON ALWAYS ignores computer MOTOR for playback and recording; COMP.
follows it. Use COMP. for an original C16/C116/Plus/4 with a working MOTOR
line.
Autoplay ON presses PLAY as soon as a TAP is loaded; OFF loads it stopped.
In the Tape menu, the loaded TAP offers `Play` while stopped and `Stop` while
playing or recording, between the filename and `Record`. With no TAP,
`Load TAP...` opens a tape-filtered browser and `Record` remains available.
With a loaded TAP, choose `Stop` before Record, Eject, Rewind or either Go to
command. `Record` opens `New TAP` and `Current TAP`. Go to counter changes
TARGET by exactly one per encoder impulse; click seeks and Back cancels.
Eject unloads the stopped tape. MOTOR OFF pauses movement but does not release
PLAY, so the Stop step is still needed after the computer switches MOTOR off.

Put an Ultimate-format `.idx` next to a same-named `.tap`, for example
`games.idx` and `games.tap`. Each non-comment line contains an absolute byte
offset in the TAP, whitespace, then a position name: `0x00000015 GAME 2`.
Offsets may be decimal, hexadecimal with `0x`, or octal with a leading `0`.
Lines starting with `;`, `#`, or `'` are comments.
Selecting an IDX in the tape browser loads and stops its matching TAP, then opens the
position list directly. Selecting the TAP loads an adjacent matching IDX
automatically; use Tape > `Go to file...` to open the position list.
The option appears immediately below Play when a valid IDX is loaded and the
tape is stopped. During Play or Record, choose Stop before seeking.
IDX is independent of the computer, but the paired TAP must be a C16/Plus/4
recording supported by this player. Offsets must
point to the start of a TAP pulse record; an offset inside an extended pulse
is rejected. Seeking stops playback. The position list is hidden during
recording and reloaded after the TAP and IDX are saved.

**Try it in the desktop TUI:** run `make tui`, open `/1551/tap`, and select
`Winter Games.idx`. The simulator loads `Winter Games.tap` and shows the
sample positions INTRO, EVENT 1, and EVENT 2. Selecting a position stops
playback and displays its TAP offset. Back in Tape, `Go to file...` is directly
below Play/Stop. Selecting `Winter Games.tap` also loads its IDX automatically;
open `Go to file...` from Tape to see the positions.
Use Left/Right to move, Enter to select, and `b` to go back. This virtual SD
card and its index offsets are examples;
`make tui` does not read TAP/IDX files from your computer or SD card. To test
your own files, copy a matching `.tap`/`.idx` pair to the device's SD card
and use the hardware menu.

WProt is write protection; read-only media stays protected. Write image
flushes the mounted medium. New image first lets you choose D64/G64, then
Create image creates an unused NEWIMAGE1.d64/g64, NEWIMAGE2.d64/g64, etc. in
the current SD folder, without overwriting or automatically mounting it.
Reset drive resets only the emulated drive. Reboot Pi1551 is a confirmed full
device restart after flushing writes, not a computer RESET.

Display changes apply on the next refresh. Contrast never offers zero;
custom valid options.txt values remain usable until you change them.
Dimming uses Dim time seconds since input; OFF/zero disables the timer.
Dim level 0 switches the OLED off after the timeout, and input wakes it.
Font switches cached ASCII/C64 8x8 slots immediately: startup loads the
4096-byte ChargenFont file; missing, unreadable or wrong-sized files make
both slots ASCII. Scroll is seconds per character step and includes a demo
after the fixed value. Reverse affects the next encoder impulse.

Sound ON/OFF is independent of SAMPLE/BUZZER; both use fixed GPIO13.
Freq and Duration appear only in BUZZER mode. Each successful Sound-menu
change starts a one-second preview window without blocking disk/tape/UI:
SAMPLE loops the click, BUZZER uses its frequency and configured Duration
(capped to one second); shorter Duration leaves silence for the rest,
and OFF or zero Duration is silent. New changes replace the preview.

Load settings rereads `/options.txt` from the SD card on demand and applies
runtime options, replacing unsaved changes. Startup-only fields (hardware/encoder enable,
ROM filenames, ChargenFont, AutoMountImage filename, StarFileName) require
editing the file and restarting; ROM selection is from already loaded slots.
Load does not reset, remount or save. Save settings updates only values changed
since boot or the last successful load/save; it retains user order, existing
comments, unrelated settings, whitespace and line endings. It edits the last
effective occurrence of a duplicate option, and appends absent changed options
with a blank line and an explanatory comment. Every Save action refreshes
`options-backup.txt` with the exact file contents from immediately before that
save, including when settings have not changed. No `.bak` file is used. Failed
save/load reports an error and preserves pending settings. Disk/image writes
and settings saving are separate actions. Load settings and Save settings always use the SD
card root, regardless of the currently active device selected at boot or with
`CP`. The temporary `options.txt.tmp` and `options-backup.txt` files are also
created on SD. Save settings replaces the previous `options-backup.txt` with
the options file that existed before the save.

Read the full scrolling failure message: it now identifies the failed step,
such as Sync temp failed (with a FatFS result code), Invalid options.txt or
options.txt.tmp exists. A previous `options-backup.txt` is replaced on each
save. Inspect the backup on a PC if you need to recover earlier settings. Reboot errors
distinguish host/image/media flush, SD sync and unmount failures; the device
does not restart when those checks fail.

See [the interaction specification](docs/PI1551-TUI-SPEC.md) for exact state
transitions and [the implementation notes](docs/PI1551-OLED-IMPLEMENTATION.md)
for native test coverage and hardware limitations.

## HDMI And USB Keyboard

HDMI displays startup information: the boot/logo screen, font/ROM loading,
and diagnostics enabled by ShowOptions or i2cScan. ChargenFont is also used
by HDMI text.

The current menu is controlled by the encoder, Insert and Back, not by a USB
keyboard or an interactive HDMI browser. This also applies without an attached
OLED; disconnecting it or removing LCDName does not enable keyboard control.
Use Settings -> ROM to select a ROM and Menu -> Reboot Pi1551 to restart.

The keyboard controls of make tui are only for the desktop simulator; they
are not USB keyboard shortcuts on the Raspberry Pi.

## TAP Playback And Recording

Selecting a `.tap` file loads it. With Autoplay ON, Pi1551 asserts the tape
SENSE line as if PLAY is pressed; with Autoplay OFF, select Play in the menu.
The player then emits tape pulses on the READ line.

When the computer controls the motor, `M` on the OLED follows the computer's
MOTOR line. During a cassette `LOAD`, the Kernal can display `PRESS PLAY ON
TAPE` while MOTOR is still off: it waits for the tape SENSE line before
turning the motor on. PLAY, REW, and FF all activate this same SENSE signal,
so the computer cannot tell which transport key was pressed. Choosing Stop
releases SENSE, but does not immediately cancel the Kernal's active search or
load; MOTOR can remain on until that operation finishes.

`tapeMotorAlwaysOn` controls how playback starts:

- `tapeMotorAlwaysOn = 1`: motor is treated as always active. Playback starts
  immediately after the TAP is loaded. This is meant for machines with an
  original CPU replaced by a 6510, which cannot control the tape MOTOR line.
- `tapeMotorAlwaysOn = 0`: playback follows the tape MOTOR input line.

The tape player is reset when the Commodore reset line is handled. Exiting disk
emulation does not unload an already loaded tape.

### Recording a program

1. Open `Menu...` → `Tape` → `Record` → `New TAP`. This creates the next unused
   `NEWTAPE1.tap`, `NEWTAPE2.tap`, etc. in the current SD folder and starts
   RECORD. Existing files are not overwritten. `Record` is available even
   when no tape is loaded. To continue on a loaded tape instead, choose
   `Current TAP` at its current position; use Rewind or Go to counter first if
   you want another position. If autoplay started a loaded TAP, choose Stop
   before opening Record.
2. On the computer, enter a program and use a cassette command such as
   `SAVE"TEST",1`. RECORD also presses PLAY/SENSE. With `Motor ON COMP.`, Pi1551
   captures WRITE pulses only while the computer runs the MOTOR. The first
   MOTOR OFF for more than 0.5 seconds after captured pulses ends RECORD and
   saves TAP. A brief MOTOR pause after the C16 header does not end RECORD.
   With `Motor ON ALWAYS`, choose `Stop` after SAVE finishes.
3. After recording ends, you can choose `Record` → `Current TAP`
   again to add another program. To load from the beginning, choose `Rewind`,
   then `Play` if needed, and use `LOAD"",1` on the computer. Directly changing
   from RECORD to PLAY requires Stop first.

`New TAP` creates a valid empty TAP file immediately. Captured pulses are
buffered in Pi memory and written as version 2 C16 TAP to SD/USB when recording
stops. After a successful save, Pi1551 decodes standard Plus/4 KERNAL headers
from the recorded pulses, checks the repeated header, and creates or updates a
matching IDX with the file names. Existing entries outside the overwritten
region are kept at their recalculated byte offsets; entries in the overwritten
region are replaced by decoded names. Recordings with no recognized header do
not create an IDX. This decoding and IDX write run outside the GPIO interrupt.
Only a tape changed during RECORD is written again: ordinary Play, Stop,
Rewind and seek
do not rewrite an unchanged TAP. `Stop`, `Rewind`, a confirmed `Go to counter`
and `Eject TAP` finish RECORD and save changed data before moving or unloading
the tape. If saving fails, Rewind and Go to counter leave the position in place
so the save can be retried. Eject also retains the tape after a failed save.

## `options.txt`

`options.txt` lives in the SD card root. Boot loading, the OLED Load settings
and Save settings actions, and the save recovery files always use this SD copy,
even when a USB drive is the active file device. The parser accepts `//` line
comments and `/* ... */` block comments. Values are parsed as decimal by
default, but C style numeric prefixes such as `0x3c` are accepted for integer
values.

Options not present in the file use built-in defaults from `src/options.cpp`.

### Drive And Startup

| Option | Default | Meaning |
| --- | --- | --- |
| `deviceID` | `8` | TCBM device number. |
| `OnResetChangeToStartingFolder` | `1` | Return Browser to `/1551` on computer RESET, including when `IgnoreReset = 1`. Menu: Reset path. |
| `AutoMountImage` | empty | Startup filename, relative to `/1551` or absolute. Supported image/container/PRG or nonempty `.lst`, mounted at boot and REACT reset when enabled. |
| `autoMountEnabled` | `1` | Runtime Auto mount toggle. Missing, empty or invalid AutoMountImage acts as OFF and hides the menu item. |
| `StarFileName` | empty | File served for `LOAD"*"` / `DLOAD"*"` style requests. |
| `AutoBaseName` | `autoname` | Prefix for automatically created disk images. |
| `NewDiskType` | `d64` | Initial format for Disk -> New image -> Type, D64 or G64. |
| `DisableSD2IECCommands` | `0` | Browser setting `SD2IEC Cmds.` Stored inverted: `0` enables browser-mode file access and its SD2IEC-style command subset; `1` disables the computer-facing service but not OLED browsing. |
| `LowercaseBrowseModeFilenames` | `0` | Send browser-mode filenames as lowercase. |
| `IgnoreReset` | `0` | Drive Reset: 0 = REACT, 1 = IGNORE. Does not suppress the independent Reset path action. |
| `QuickBoot` | `0` | Skip startup delay/logo pause when set to `1`. |
| `ShowOptions` | `0` | Print parsed options on HDMI at startup. |

### ROMs And Fonts

| Option | Default | Meaning |
| --- | --- | --- |
| `ROM1` or `ROM` | `dos1551.bin` | Primary ROM slot used by the UI. |
| `ROM2`..`ROM7` | empty | Additional selectable 1551 ROM slots. |
| `ROM1551` | `dos1551-318008-01.bin` | Legacy dedicated 1551 option; not the source of the Settings -> ROM list. |
| `ChargenFont` or `Font` | `chargen` | Startup C64 8x8 font file for HDMI and the OLED C64 slot; exactly 4096 bytes. The supplied template names `chargen.bin`. |

ROM files are searched from the SD root and `/roms` paths used by the firmware.
Settings -> ROM selects an already loaded ROM1..ROM7 slot immediately. Missing
or invalid slots are omitted. There is no longpress or Browser ROM-file action.
Change slot filenames in options.txt and reboot to load different ROM files.

### Display

| Option | Default | Meaning |
| --- | --- | --- |
| `LCDName` | empty | Startup OLED model: use `ssd1306_128x64` or `sh1106_128x64` for this eight-row menu. The inherited parser also recognizes `ssd1306_128x32`, which cannot show all eight rows. |
| `LcdLogoName` or `LCDLogoName` | `1541classic` | Built-in logo name or custom raw logo filename. |
| `i2cLcdAddress` | `0x3c` | I2C OLED address. `60` decimal is `0x3c`. |
| `i2cScan` | `0` | Scan I2C and display found addresses at startup. |
| `i2cLcdFlip` | `0` | Rotate OLED 180 degrees. |
| `i2cLcdOnContrast` | `128` | Active brightness 1..255; menu steps 16,32,...,240,255. Startup zero is clamped; runtime load rejects zero. |
| `oledDimming` | `1` | Runtime Dimming ON/OFF. |
| `i2cLcdDimContrast` | `16` | Dim level 0..255; menu steps 0,16,32,...,240,255. Zero powers the OLED off until input. |
| `i2cLcdDimTime` | `16` | Seconds since last input before dimming; menu steps include 16; 0/OFF disables the timer. |
| `i2cLcdUseCBMChar` | `0` | Runtime Font: 0 = ASCII, 1 = cached C64. Both slots are ASCII if chargen failed to load. |
| `DisplayPNGIcons` | `0` | Show 320x200 PNG files matching disk image names on HDMI. |
| `GraphIEC` | `0` | Show TCBM and tape signal activity on HDMI. |
| `DisplayTemperature` | `0` | Runtime Temp ON/OFF in the header; Status still shows temperature. |
| `DisplayPC` | `0` | Show emulated 6502 program counter for debugging. |
| `scrollHighlightRate` | `0.10` | Runtime seconds per character step for filenames and folder header, with a Scroll demo. |

The LCD-prefixed keys are kept for options.txt compatibility, not separate
LCD hardware settings. Obsolete oledFont/oledScroll entries are ignored and
preserved; use i2cLcdUseCBMChar and scrollHighlightRate instead.

The Pi1551 hat always uses I2C bus 1 for the OLED. `i2cBusMaster` exists in
the code but is forced to `1` after parsing.

### Sound

| Option | Default | Meaning |
| --- | --- | --- |
| `SoundOnGPIO` | `0` | `0`: legacy PWM/DMA sample on GPIO13. `1`: square-wave buzzer on GPIO13. |
| `oledSound` | `1` | Runtime Sound ON/OFF, independent of the SAMPLE/BUZZER mode. |
| `SoundOnGPIODuration` | `1000` | Buzzer duration in milliseconds when `SoundOnGPIO = 1`. |
| `SoundOnGPIOFreq` | `1200` | Buzzer frequency in Hz when `SoundOnGPIO = 1`. |

For Pi1551 hardware with a piezo or buzzer on the SOUND pin, use
`SoundOnGPIO = 1`. GPIO13 is fixed; there is no selectable buzzer GPIO.
All four runtime Sound items trigger the one-second preview described above.

### Tape

| Option | Default | Meaning |
| --- | --- | --- |
| `tapeMotorAlwaysOn` | `1` | `1`: ignore MOTOR input and play immediately, for machines with a 6510 CPU replacement that cannot control MOTOR. `0`: follow MOTOR input on original machines. |
| `tapeAutoplay` | `1` | `1`: press PLAY automatically when a TAP is loaded. `0`: load TAP stopped; use Tape → Play to start playback. |

### Drive Timing And Expansion

| Option | Default | Meaning |
| --- | --- | --- |
| `RAMBOard` | `0` | Enable 8 KiB drive RAM expansion at `$8000`. Use with `dos1551-ram.bin`; this comes from the [1551-RAMBOard](https://github.com/ytmytm/1551-RAMBOard) project and is supported by [Parobek](https://github.com/ytmytm/plus4-parobek) fastloaders. |
| `skipMotorSpinUpDelay` | `0` | Skip 1551 motor spin-up delay when set to `1`. |
| `IdleSleep` | `1` | Pause idle D64 emulation at the supplied 1551 ROMs' TCBM wait loop to reduce Pi power use and temperature. TCBM/RESET/UI wake polling continues at 100 Hz; OLED shows an `S` after the track number while asleep. Set to `0` for timing diagnostics or unsupported custom ROMs. |

### Rotary

| Option | Default | Meaning |
| --- | --- | --- |
| `RotaryEncoderEnable` | `0` | Startup-only enable for the fixed-wiring encoder; not a runtime menu item. |
| `RotaryEncoderInvert` | `0` | Runtime Encoder -> Reverse; changes the next decoded impulse immediately. |

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
ChargenFont = chargen.bin
OnResetChangeToStartingFolder = 1
StarFileName = boot.t2sd
RAMBOard = 1
LCDName = ssd1306_128x64
i2cLcdAddress = 60
RotaryEncoderEnable = 1
i2cLcdUseCBMChar = 1
DisplayTemperature = 1
oledDimming = 1
i2cLcdDimTime = 30
i2cLcdDimContrast = 0
oledSound = 1
SoundOnGPIO = 1
tapeMotorAlwaysOn = 1
skipMotorSpinUpDelay = 1
IdleSleep = 1
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
