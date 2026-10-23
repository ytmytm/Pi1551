# Pi1551 User Manual

Pi1551 connects a Raspberry Pi 3 to a Commodore C16, C116 or Plus/4. It can
emulate a 1551 drive from D64/G64 disk images, serve files directly from an SD
card, and play or record C16/Plus/4 TAP files. Tape use does not require the
disk ribbon cable.

This manual covers preparing the SD card and using the device. For board
assembly, power, cable orientation and voltage details, follow the
[PI1551-hat](https://github.com/ytmytm/PI1551-hat),
[PI1551-III](https://github.com/ytmytm/PI1551-III) and
[tcbm2sd](https://github.com/ytmytm/plus4-tcbm2sd) hardware instructions.

## Contents

1. [Getting started](#getting-started)
2. [OLED menu and controls](#oled-menu-and-controls)
3. [SD files and disk images](#sd-files-and-disk-images)
4. [TAP playback and recording](#tap-playback-and-recording)
5. [Menu reference](#menu-reference)
6. [Settings and maintenance](#settings-and-maintenance)
7. [Browser command reference](#browser-command-reference)
8. [`options.txt` reference](#optionstxt)
9. [Upgrading and building](#upgrading-and-building)

## Getting Started

### What you need

- A Raspberry Pi 3A, 3A+, 3B or 3B+, with its own USB power supply.
- A PI1551-hat or PI1551-III interface board and a FAT32 SD card.
- For disk access, a tcbm2sd board revision 1.3 or newer and its ribbon cable.
  CPLD firmware/JED version 1.4 or newer is needed for software that uses
  mirrored TCBM register addresses, such as the games Corruption and Fish.
- For tape access, a Mini-DIN 7 cable connecting the interface board to the
  computer's cassette port. The disk ribbon cable is optional for tape-only use.

### Prepare the SD card

1. Copy the repository's `sdcard/` contents to the root of the FAT32 card.
2. Copy a built or released `kernel.img` to the card root.
3. Put D64/G64 images, PRG files and TAP files in `/1551`. Put matching IDX
   files beside their TAP files. The browser starts in that folder.
4. Set `RotaryEncoderEnable = 1` in `/options.txt` to use the OLED menu.
   Check `LCDName`, `deviceID` and `tapeMotorAlwaysOn` for your hardware. The
   supplied [template](sdcard/options.txt) is a useful starting point.
5. Connect the hardware and power it as described in the hardware projects.

The OLED menu also needs a supported 128×64 SSD1306/SH1106 display configured
with `LCDName`. The normal TCBM device number is 8. Set
`tapeMotorAlwaysOn = 0` for an original computer with a working cassette
MOTOR line; the default `1` is for machines whose CPU has been replaced with
a 6510 that cannot drive that line.

### First use

Pi1551 normally starts in browser mode, serving files from `/1551` directly
to the Commodore. You can load a PRG with `LOAD"filename",8` (or `DLOAD`)
without mounting an image. Send `CD:folder` on command channel 15 to enter an
SD folder, or `CD:image.d64` to mount a disk image from the computer. On the
OLED, you can also open a folder or select a D64/G64 image with the encoder;
selecting an image starts 1551 emulation. Use your configured device number
if it differs from 8. A configured `AutoMountImage` may mount media at startup.

For tape, select a `.tap` or its matching `.idx` on the OLED, then use the
computer's normal cassette `LOAD` command. Selecting the IDX also opens its
position list so you can choose a program before playback.

## OLED Menu And Controls

The 128×64 display has two status rows and six menu rows. Rotate to move the
highlight, click to select, and select `Menu...` from `Browser` or `Fliplist` to
open the device menu. Each submenu begins with `..` to return to its parent.

### Encoder and buttons

Fixed interface-board wiring:

| Signal | GPIO | Hat switch |
| --- | ---: | --- |
| A / CLK | 17 | SW3 |
| B / DT | 22 | SW2 |
| Pushbutton | 27 | SW1 |
| Back | 4 | SW4 |
| Insert | 5 | SW5 |

| Control | Action |
| --- | --- |
| Rotate left/right | Move highlight; in `Fliplist`, highlighting an image mounts it immediately. |
| Encoder click (Enter) | Select an item, toggle/cycle a setting, enter a folder, or mount media. |
| Insert in `Browser` | Add/remove the highlighted supported image or PRG from `LIST`; entering a folder does not add it. |
| Insert in `Fliplist` | Enter `Browser` in `LIST` mode, retaining entries and unmounting the image so you can add more media. |
| Back in a submenu | Return to its parent, as `..` does. |
| Back in `Browser` | Go to the parent SD folder; at SD root it can restore the saved caller/`Fliplist`. |
| Enter on a `Fliplist` image | Unmount and return to its containing SD folder, never inside the image. |
| Back in `Fliplist` | Unmount and enter `Browser`, retaining `LIST` but clearing a temporary `SOLO` image. |

Inverted text is the cursor. On opening a submenu, the highlight starts below
`..`. ROM selection is described under [Settings](#rom).

### Reading the display

The header shows, for example, `8:games` (the last SD folder) or `8: 18.0S`
(track/half-track while emulating, with `S` indicating idle sleep). A long
folder name scrolls independently of the selected row. Selected long media
and ROM filenames scroll with their prefixes fixed.

The tape row shows the counter, percentage, motor and transport flags. Dashes
indicate no loaded TAP; a loaded tape at the beginning shows `000 00%`.
`M` reflects the motor input (or the ALWAYS override). `P` means PLAY is
pressed and `R` means RECORD.

### HDMI And USB Keyboard

HDMI output and the USB keyboard are intended for diagnostics rather than
normal operation. Use the encoder and buttons for the OLED menu.

## SD Files And Disk Images

### Browser mode and 1551 emulation

Browser mode is active after boot and whenever no disk image is being emulated.
It serves SD card files over TCBM and shows the media picker on the OLED.

Selecting a D64 or G64 starts cycle-exact Commodore 1551 emulation. The
emulated 1551 has its own 6502, 6523 TPI, DOS ROM, drive mechanics and disk
image state.

D71, D80, D81, D82 and PRG files use file-level container access.

The SD browser is also the file source for ordinary Commodore `LOAD`
operations. See [Browser commands](#browser-command-reference) for commands
sent from the computer.

### Files on the SD card

From the Commodore, `LOAD "filename",8` (or `DLOAD`) on a `.prg` file reads the
program bytes directly from the SD card. No disk image is created and 1551
emulation is not started.

Directory listings (`LOAD "$",8`) mark entries as follows:

| Listing type | Meaning |
| --- | --- |
| `DIR` | Subfolder, or a mountable disk image (`.d64`, `.d71`, `.d81`, `.d80`, `.d82`, any case) |
| `PRG` | Program file or other non-directory file on SD (including `.prg`, `.tap`, extensionless files such as `boot.t2sd`, and so on) |

### PRG files and virtual disks

Enter on a standalone `.prg` in the OLED browser wraps it in a temporary
one-file D64 in RAM and mounts it as a read-only container. The original SD
file is unchanged and the drive CPU does not enter emulation.

| Action | Result |
| --- | --- |
| Enter on a highlighted `.prg` | Mounts its virtual disk and opens `Fliplist` view, without emulation. |
| Insert on a highlighted `.prg` | Adds/removes it from the mixed-format fliplist; does not mount it. |
| Normal Commodore `LOAD "program",8` in SD `Browser` | Reads the original PRG directly, without creating or mounting a disk. |

Once mounted, the program is the only file on the virtual disk exposed to
the computer. Use normal `LOAD`/`RUN`. T64, NIB and NBZ are not offered as
mountable media in the OLED browser.

### Disk image formats

D64 images can be mounted for 1551 emulation. This is the compatibility mode
for software that expects real drive timing or drive-side code execution.

D71, D81, D80, and D82 images are supported as read-only file containers. You
can browse directories and load files from inside them, but Pi1551 does not
emulate those drive mechanisms as 1551 hardware.

Enter on a nonempty `.lst` loads its fliplist and mounts the first valid
entry. Lines name D64/G64/D71/D80/D81/D82/PRG media, relative to the LST file's
folder or by absolute SD path. The OLED menu does not write or edit LST files.

### Selecting media and making a fliplist

Folders are shown as `[name]`; disk images are not bracketed. Enter on D64/G64
mounts and starts emulation. Enter on D71/D80/D81/D82 or PRG mounts a file
container without emulation. Either action opens `Fliplist` view, even for one
image: container contents are listed to the Commodore, not on the OLED.
Simply browsing an SD folder selects that folder's virtual file-list source;
there is no separate drive-mode switch or Use this dir item.

Enter alone mounts a temporary `SOLO` image. Returning to `Browser` clears that
one-item list, so an unexpected `1>` prefix is not left behind. To prepare a
persistent `LIST`, use Insert: it toggles membership, with `1>`, `2>`, etc.
shown in `Browser`. While building `LIST`, Enter on the final image adds it
if necessary, mounts it and opens `Fliplist`. Enter on an existing member
activates it.
LST selection also creates `LIST`. Mixed D64/G64 and container entries switch
between emulation and file-level access automatically.

In `Fliplist`, `Menu...` remains selectable. Rotate onto an image to mount it
immediately, including the first image after `Menu...`; rotating from the last
image wraps to the first, and rotating left from the first reaches `Menu...`.
Click an image to unmount it and return to its containing SD folder. `LIST`
survives that return; `SOLO` does not. The first two entries under `Menu...`
manage the list:

- `Clear list` removes its saved entries.
- `Browser` clears the list, unmounts the image and returns to SD browsing.

### Disk actions

Under `Menu...` → `Disk`:

- `WProt ON/OFF` changes write protection for writable media. Read-only media
  remain protected.
- `Write image` flushes changes to the mounted image.
- `New image` opens two entries:
  - `Type D64/G64` chooses the new image format.
  - `Create image` creates the next unused `NEWIMAGE1.d64`/`.g64`,
    `NEWIMAGE2.d64`/`.g64`, etc. in the current SD folder. It neither
    overwrites existing files nor mounts the new image automatically.
- `Reset drive` resets only the emulated drive.

## TAP Playback And Recording

Pi1551 plays and records C16/C116/Plus/4 TAP files through the cassette port.
Accepted files use the `C16-TAPE-RAW` signature and TAP version 0, 1 or 2.
PAL/NTSC timing comes from the TAP header. Tape operation is independent of
the browser and disk emulation. A tape can remain loaded while you browse SD
files or use a disk image.

### Tape menu

Under `Menu...` → `Tape`, entries appear in this order when available:

- `1:<filename>` shows the loaded TAP name, or `1:` when no tape is loaded.
- `Play` presses PLAY; `Stop` releases it. Stop playback or recording before
  seeking or ejecting the tape.
- `Go to file...` opens positions from a matching IDX when the tape is stopped.
  Selecting a position seeks to that file's start.
- `Load TAP...` opens a picker for TAP and IDX files when no tape is loaded.
- `Record` is available when the transport is stopped, including when no tape
  is loaded. Its submenu contains:
  - `New TAP` creates a new file in the current SD folder and starts recording.
  - `Current TAP` records at the loaded tape's current position. It appears
    only when a TAP is loaded.
- `Eject TAP` unloads a stopped tape after saving any recording.
- `Rewind` returns a stopped tape to the beginning after saving any recording.
- `Go to counter` lets you set a three-digit target, one step per encoder
  impulse. Click to seek or press Back to cancel.

### Playing a tape

Select a `.tap` file in the OLED browser or, when no tape is loaded, choose
`Menu...` → `Tape` → `Load TAP...`. The file picker accepts both TAP and IDX
files. Selecting a `.tap` loads it; if a same-named `.idx` is beside it,
Pi1551 loads its position list too. Selecting the `.idx` loads the matching
TAP, stops playback and opens the position list immediately. Choose a file
there to seek to its start; then select `Play` and use the computer's cassette
`LOAD` command. If you selected the TAP, choose `Stop` if it started playing,
then `Tape` → `Go to file...` to open the position list.

With `Autoplay ON`, loading a TAP asserts the tape SENSE line as if PLAY is
pressed; with `Autoplay OFF`, select `Play` in the menu. The player then emits
tape pulses on the READ line.

When the computer controls the motor, `M` on the OLED follows the computer's
MOTOR line. During a cassette `LOAD`, the Kernal can display `PRESS PLAY ON
TAPE` while MOTOR is still off: it waits for the tape SENSE line before
turning the motor on. PLAY, REW, and FF all activate this same SENSE signal,
so the computer cannot tell which transport key was pressed. Choosing `Stop`
releases SENSE, but does not immediately cancel the Kernal's active search or
load; MOTOR can remain on until that operation finishes.

MOTOR OFF pauses movement but does not release PLAY, so select `Stop` when you
need to stop the transport. `Motor ON COMP.` follows the computer's MOTOR line;
`Motor ON ALWAYS` ignores it. On an original machine, use `COMP.`.

The OLED tape status shows a three-digit counter, percentage, MOTOR and
PLAY (`P`) or RECORD (`R`). Dashes indicate that no TAP is loaded. A loaded
tape at its beginning shows `000 00%`.

### Recording a program

1. Open `Menu...` → `Tape` → `Record` → `New TAP`. This creates the next unused
   `NEWTAPE1.tap`, `NEWTAPE2.tap`, etc. in the current SD folder and starts
   RECORD. Existing files are not overwritten. `Record` is available even
   when no tape is loaded. To continue on a loaded tape instead, choose
   `Current TAP` at its current position; use `Rewind` or `Go to counter` first if
   you want another position. If autoplay started a loaded TAP, choose `Stop`
   before opening `Record`.
2. On the computer, enter a program and use a cassette command such as
   `SAVE"TEST",1`. RECORD also presses PLAY/SENSE. With `Motor ON COMP.`, Pi1551
   captures WRITE pulses only while the computer runs the MOTOR. The first
   MOTOR OFF for more than 0.5 seconds after captured pulses ends RECORD and
   saves TAP. A brief MOTOR pause after the C16 header does not end RECORD.
   With `Motor ON ALWAYS`, choose `Stop` after SAVE finishes.
3. After recording ends, you can choose `Record` → `Current TAP`
   again to add another program. To load from the beginning, choose `Rewind`,
   then `Play` if needed, and use `LOAD"",1` on the computer. Directly changing
   from RECORD to PLAY requires `Stop` first.

`New TAP` creates an empty TAP file immediately. Pi1551 writes captured pulses
as a version 2 C16 TAP when recording stops. A recognized Plus/4 SAVE header
also creates or updates a matching IDX with the recorded program names; a
recording with no recognized header does not create an IDX. Existing index
entries outside a rewritten area are retained at their adjusted positions.

`Stop`, `Rewind`, a confirmed `Go to counter` and `Eject TAP` save changed
recordings before moving or unloading the tape. If saving fails, the tape
remains loaded and `Rewind` or `Go to counter` leaves its position unchanged.

### Using an IDX position list

Put an [Ultimate-format](https://www.luigidifraia.com/technical-info/) `.idx`
next to a same-named `.tap`, for example `games.idx` and `games.tap`. Each
non-comment line contains an absolute byte offset in the TAP, whitespace,
then a position name: `0x00000015 GAME 2`.
Offsets may be decimal, hexadecimal with `0x`, or octal with a leading `0`.
Lines starting with `;`, `#`, or `'` are comments.
The `Go to file...` option appears below `Play` when a valid IDX is loaded and
the tape is stopped. During `Play` or `Record`, choose `Stop` before seeking.
The paired TAP must be a C16/Plus/4 recording supported by this player.
Offsets must point to the start of a TAP pulse record; one inside a pulse
is rejected. Seeking stops playback. The position list is hidden during
recording and reloaded after the TAP and IDX are saved.

## Menu Reference

The following map lists the available OLED entries. Some entries appear only
when a TAP, disk image, RTC or configured option is available. Values shown
are choices, not further menu levels.

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
   │     └─ TARGET <position>/<max>
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
   │  │  ├─ Minute 00–59
   │  │  ├─ Load from RTC         (only when RTC detected)
   │  │  └─ Save to RTC           (only when RTC detected)
   │  ├─ Load settings
   │  └─ Save settings
   ├─ Status
   │  ├─ ..
   │  ├─ RTC YES/NO
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

## Settings And Maintenance

Open `Menu...` → `Settings` for drive, tape, browser, display, encoder, sound,
and date/time controls. Changes generally take effect immediately; startup
options such as encoder enable, hardware, ROM filenames and font filename
require editing `/options.txt` and rebooting. See the
[`options.txt` reference](#optionstxt) for the full list.

### ROM

- `ROM` lists the configured, valid ROM1–ROM7 slots. The triangle marks the
  active ROM. Click another filename to switch immediately; click the active
  one or press Back to leave. Change slot filenames in `/options.txt` and
  reboot to load different ROM files.

### Drive

- `Device number` selects TCBM device 8 or 9.
- `Idle trap` pauses an idle D64 drive at the supported ROM's TCBM wait loop
  to reduce power use and temperature. `S` after the track number indicates
  that the drive is sleeping. Turn it off for timing diagnostics or an
  unsupported custom ROM.
- `RAMBOard` enables 8 KiB drive RAM expansion. Use the matching
  `dos1551-ram.bin` ROM for software that needs it.
- `Skip spinup` removes the emulated motor spinup delay.
- `Reset REACT/IGNORE` controls how Pi1551 responds to the RESET signal sent
  by the Commodore computer. `REACT` resets the emulated drive CPU and permits
  reset-time automount; `IGNORE` suppresses both drive actions. The separate
  `Reset path` setting can still change the SD browser folder on that signal.

### Tape settings

- `Motor ON ALWAYS/COMP.` determines whether tape playback and recording
  always treat the motor as active or follow the computer's MOTOR line. Use
  `COMP.` on an original C16/C116/Plus/4 with a working MOTOR line.
- `Autoplay ON/OFF` presses PLAY as soon as a TAP is loaded when ON, or loads
  it stopped when OFF.

### Browser settings

- `Auto mount ON/OFF` appears only when `AutoMountImage` names a valid image
  or nonempty LST. It applies at boot and the next `REACT` reset; changing it
  does not mount an image immediately.
- `Reset path ON/OFF` returns the SD `Browser` source to `/1551` on a computer
  RESET when ON, even with `Reset IGNORE`. OFF keeps the current folder. This
  choice alone does not clear a list, unmount a disk or eject/rewind a TAP.
- `Lowercase ON/OFF` controls whether browser-mode filenames are sent to the
  Commodore in lowercase.
- `SD2IEC Cmds. ON/OFF` enables or disables the computer-facing browser file
  service and its command channel. OLED browsing remains available when OFF.
  This setting does not affect a mounted D64/G64 running in 1551 emulation.

### Display

- `Temp ON/OFF` shows or hides the temperature in the header. When ON, it
  reserves four characters on the right; when OFF, the folder name gets the
  full remaining width.
- `Contrast` sets the active OLED brightness. Zero is not available in the
  menu.
- `Dimming ON/OFF` enables or disables dimming after inactivity.
- `Dim time` sets the delay since the last input; `OFF` disables the timer.
- `Dim level` sets the brightness after that delay. Zero switches the OLED off
  until the next input.
- `Orient` displays the OLED normally or rotated 180 degrees.
- `Font` selects the cached ASCII or C64 8×8 font. The C64 font is loaded
  from the 4096-byte `ChargenFont` file at startup; if it cannot be read, both
  choices use ASCII.
- `Scroll` sets the speed of long-name scrolling and shows a scrolling demo.

Display changes take effect on the next refresh, except `Font`, which
switches immediately.

### Encoder

- `Reverse ON/OFF` reverses the next decoded encoder impulse.

### Sound

- `Sound ON/OFF` enables or mutes the head-step sound independently of its mode.
- `Mode SAMPLE/BUZZER` chooses the sample or square-wave buzzer on GPIO13.
- `Freq` sets the buzzer frequency; it appears only in `BUZZER` mode.
- `Duration` sets the buzzer sound length; it appears only in `BUZZER` mode.
  Zero is silent.

Each successful change in the `Sound` menu starts a one-second preview. `SAMPLE`
repeats the click; `BUZZER` uses the selected frequency and `Duration`, capped
at one second. A shorter `Duration` leaves silence for the rest of the preview.

### Date and time

The date and time entries are:

- **Setting the date and time:** Select `Year`, `Month`, `Day`, `Hour` or
  `Minute`, rotate to change its value, then click to apply; Back cancels.
  The local clock survives drive resets but not a device reboot. Once set,
  it supplies timestamps for new SD files. Pi1551 does not infer a timezone.
- `Load from RTC` reads the clock from a detected RTC. This entry appears
  only when an RTC is available.
- `Save to RTC` writes the current local clock to a detected RTC. It appears
  under the same condition.

### Load and save settings

- `Load settings` rereads `/options.txt` and replaces unsaved runtime changes.
  It does not remount media.
- `Save settings` writes changed runtime values while preserving the file's
  order, comments, unrelated settings, whitespace and line endings. If an
  option appears more than once, it updates the last effective occurrence.
  It refreshes `/options-backup.txt` with the pre-save file even when nothing
  changed, and uses `/options.txt.tmp` during the save.

Both actions use the SD card root regardless of the active file device.
Startup options such as hardware, encoder enable, ROM filenames,
`ChargenFont`, `AutoMountImage` filename and `StarFileName` require editing the
file and rebooting.

If a save or load fails, read the scrolling error message. The most recent
`options-backup.txt` contains the options file from just before the last save.
Inspect it on a computer if you need to recover settings.

### Status and reboot

- `Status` displays the RTC state, local date and time, temperature, SD size
  and free space, firmware version, DOS filename, current image and TAP, LED,
  MOTOR, PLAY and track state.
- `Reboot Pi1551` opens a confirmation screen:
  - `Cancel` is selected initially and returns to the menu.
  - `Reboot Pi1551?` flushes writes and restarts the device. It is distinct
    from a RESET sent by the Commodore. A flush, SD sync or unmount error
    prevents the restart and appears on the OLED.

## Browser Command Reference

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
| <code>CD&leftarrow;</code>, `CD_`, `CD:_`, `CD..`, `CD:..` | Leave an image or move to the parent SD directory. `_` represents the Commodore left-arrow character. |
| `CD/` / `CD//` | Select the SD root / the `/1551` starting folder. |
| `MD:name` | Create a subdirectory relative to the current directory. `MD//name` creates it below the `/1551` starting folder; partition prefixes other than zero are not supported. |
| `RD:name` | Remove a subdirectory of the current directory. |
| `CP0` | Select the SD card. |
| `CP1`, `CP2`, ... | Select the corresponding available USB mass-storage device. This is Pi1551's storage-device mapping, not sd2iec's general partition model. |
| `XS:name.lst` | Load the named LST swap list and mount its first valid entry, using the same `Fliplist` behavior as panel selection. |
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

`T-W` and `Settings` → `Date & time` use the same software clock. It survives
drive/bus resets but not a power cycle or reboot. Until it is set, `T-R`
returns `31,SYNTAX ERROR,00,00`, and new SD files get a fixed 2026-11-01
FatFs timestamp. After it is set, FatFs uses the local clock for file
timestamps. If an RTC module answers at `i2cRtcAddress` (default `0x68`) and
its time is valid, Pi1551 loads that time once at boot. `Date & time` then
offers `Load from RTC` and `Save to RTC`. Set `i2cRtcType = ds1307` when using
a DS1307 (default: `ds3231`). `i2cScan = 1` prints addresses on HDMI; it does
not change RTC behavior. The OLED editor uses years 2000–2099; `T-W` accepts
the wider SD2IEC range. Times are local 24-hour values; the firmware does not
infer a timezone.

The same browser command channel also implements file operations used
by SD2IEC-compatible software: `C` (copy/concatenate), `N` (create and mount a
new D64/G64 using the configured `NewDiskType`), `R` (rename), and `S`
(scratch/delete files). `X?` returns the Pi1551 DOS version. Settings changed
by `XI`, `XE`, and `X*` are intentionally volatile. `XW` is not implemented,
so the preferred `XI1` image-as-DIR default cannot be overwritten. Pi1551 also
intentionally does not implement `XR`, `M-R`/`M-W`/`M-E`, `U1`/`U2`,
`B-R`/`B-W`/`B-P`, or raw-device `DI`/`DR`/`DW`. Other sd2iec commands should
be treated as unsupported unless explicitly listed above.

Commodore command-channel navigation updates the OLED as well as the file
source. `CD` into a folder shows `Browser`. `CD` selecting an image mounts it
and shows `Fliplist` using the same `SOLO`/`LIST` rules as panel selection.

`CD..` or <code>CD&leftarrow;</code> (the Commodore left-arrow character, also
written `CD_`) exits a mounted image into its containing SD folder. Send it
again to go up one SD folder. This applies to D64/G64 emulation and to
D71/D80/D81/D82/PRG containers, including images selected from a fliplist.
At SD root, a parent command stays there. `CD/` selects the SD root; `CD//`
selects `/1551`. A host directory change also leaves any open OLED submenu;
Back cannot restore an obsolete view and remount media unexpectedly.

An explicit `LIST` survives the host's exit; a temporary `SOLO` image does not.
TAP stays loaded. A plain directory refresh does not interrupt the current
submenu. Failed image flush/unmount keeps the mounted medium and reports an
error. The panel's folder header and the computer's subsequent directory
listing refer to the same directory after the change.

## `options.txt`

The configuration file is `/options.txt` in the SD card root. The same SD copy
is used at boot and by `Load settings` and `Save settings`, even when a USB
drive is active. It accepts `//` line comments and `/* ... */` block comments.
Numbers are decimal by default; integer values can use C-style prefixes such
as `0x3c`. Missing options use built-in defaults. Start with the
[supplied template](sdcard/options.txt).

### Drive and startup

| Option | Default | Meaning |
| --- | --- | --- |
| `deviceID` | `8` | TCBM device number. |
| `OnResetChangeToStartingFolder` | `1` | Return `Browser` to `/1551` on computer RESET, including when `IgnoreReset = 1`. Menu: `Reset path`. |
| `AutoMountImage` | empty | Startup filename, relative to `/1551` or absolute. Supported image/container/PRG or nonempty `.lst`, mounted at boot and REACT reset when enabled. |
| `autoMountEnabled` | `1` | Runtime `Auto mount` toggle. Missing, empty or invalid `AutoMountImage` acts as OFF and hides the menu item. |
| `StarFileName` | empty | File served for `LOAD"*"` / `DLOAD"*"` style requests. |
| `AutoBaseName` | `autoname` | Prefix for automatically created disk images. |
| `NewDiskType` | `d64` | Initial format for `Disk` → `New image` → `Type`, D64 or G64. |
| `DisableSD2IECCommands` | `0` | `Browser` setting `SD2IEC Cmds.` Stored inverted: `0` enables browser-mode file access and its SD2IEC-style command subset; `1` disables the computer-facing service but not OLED browsing. |
| `LowercaseBrowseModeFilenames` | `0` | Send browser-mode filenames as lowercase. |
| `IgnoreReset` | `0` | Drive `Reset`: 0 = `REACT`, 1 = `IGNORE`. Does not suppress the independent `Reset path` action. |
| `QuickBoot` | `0` | Skip startup delay/logo pause when set to `1`. |
| `ShowOptions` | `0` | Print parsed options on HDMI at startup. |

### ROMs and fonts

| Option | Default | Meaning |
| --- | --- | --- |
| `ROM1` or `ROM` | `dos1551.bin` | Primary ROM slot used by the UI. |
| `ROM2`..`ROM7` | empty | Additional selectable 1551 ROM slots. |
| `ROM1551` | `dos1551-318008-01.bin` | Legacy dedicated 1551 option; not the source of the `Settings` → `ROM` list. |
| `ChargenFont` or `Font` | `chargen` | Startup C64 8x8 font file for HDMI and the OLED C64 slot; exactly 4096 bytes. The supplied template names `chargen.bin`. |

ROM files are searched from the SD root and `/roms` paths used by the firmware.
`Settings` → `ROM` selects an already loaded ROM1..ROM7 slot immediately. Missing
or invalid slots are omitted. There is no longpress or `Browser` ROM-file action.
Change slot filenames in options.txt and reboot to load different ROM files.

### Display

| Option | Default | Meaning |
| --- | --- | --- |
| `LCDName` | empty | Startup OLED model: use `ssd1306_128x64` or `sh1106_128x64` for this eight-row menu. The 128×32 option cannot show the full menu. |
| `LcdLogoName` or `LCDLogoName` | `1541classic` | Built-in logo name or custom raw logo filename. |
| `i2cLcdAddress` | `0x3c` | I2C OLED address. `60` decimal is `0x3c`. |
| `i2cRtcAddress` | `0x68` | I2C RTC address after HDMI/LCD init. `0` skips probe and Load/Save. |
| `i2cRtcType` | `ds3231` | RTC chip for register decode/write: `ds3231` or `ds1307`. |
| `i2cScan` | `0` | Scan I2C and list found addresses on HDMI at startup. Independent of the RTC probe. |
| `i2cLcdFlip` | `0` | Rotate OLED 180 degrees. |
| `i2cLcdOnContrast` | `128` | Active brightness 1..255; menu steps 16,32,...,240,255. Startup zero is clamped; runtime load rejects zero. |
| `oledDimming` | `1` | Runtime `Dimming ON/OFF`. |
| `i2cLcdDimContrast` | `16` | `Dim level` 0..255; menu steps 0,16,32,...,240,255. Zero powers the OLED off until input. |
| `i2cLcdDimTime` | `16` | Seconds since last input before dimming; menu steps include 16; `OFF` disables the timer. |
| `i2cLcdUseCBMChar` | `0` | Runtime `Font`: 0 = `ASCII`, 1 = cached `C64`. Both slots are ASCII if chargen failed to load. |
| `DisplayPNGIcons` | `0` | Show 320x200 PNG files matching disk image names on HDMI. |
| `GraphIEC` | `0` | Show TCBM and tape signal activity on HDMI. |
| `DisplayTemperature` | `0` | Runtime `Temp ON/OFF` in the header; `Status` still shows temperature. |
| `DisplayPC` | `0` | Show emulated 6502 program counter for debugging. |
| `scrollHighlightRate` | `0.10` | Runtime seconds per character step for filenames and folder header, with a `Scroll` demo. |

The LCD-prefixed keys are kept for options.txt compatibility, not separate
LCD hardware settings. Obsolete oledFont/oledScroll entries are ignored and
preserved; use i2cLcdUseCBMChar and scrollHighlightRate instead.

The Pi1551 interface uses I2C bus 1 for the OLED.

### Sound

| Option | Default | Meaning |
| --- | --- | --- |
| `SoundOnGPIO` | `0` | `0`: legacy PWM/DMA sample on GPIO13. `1`: square-wave buzzer on GPIO13. |
| `SoundEnabled` | `1` | Runtime `Sound ON/OFF`, independent of the `SAMPLE`/`BUZZER` mode. |
| `SoundOnGPIODuration` | `1000` | Buzzer duration in milliseconds when `SoundOnGPIO = 1`. |
| `SoundOnGPIOFreq` | `1200` | Buzzer frequency in Hz when `SoundOnGPIO = 1`. |

For Pi1551 hardware with a piezo or buzzer on the SOUND pin, use
`SoundOnGPIO = 1`. GPIO13 is fixed; there is no selectable buzzer GPIO.
All four runtime Sound items trigger the one-second preview described above.

### Tape

| Option | Default | Meaning |
| --- | --- | --- |
| `tapeMotorAlwaysOn` | `1` | `1`: ignore MOTOR input and play immediately, for machines with a 6510 CPU replacement that cannot control MOTOR. `0`: follow MOTOR input on original machines. |
| `tapeAutoplay` | `1` | `1`: press PLAY automatically when a TAP is loaded. `0`: load TAP stopped; use `Tape` → `Play` to start playback. |

### Drive timing and expansion

| Option | Default | Meaning |
| --- | --- | --- |
| `RAMBOard` | `0` | Enable 8 KiB drive RAM expansion at `$8000`. Use with `dos1551-ram.bin`; this comes from the [1551-RAMBOard](https://github.com/ytmytm/1551-RAMBOard) project and is supported by [Parobek](https://github.com/ytmytm/plus4-parobek) fastloaders. |
| `skipMotorSpinUpDelay` | `0` | Skip 1551 motor spin-up delay when set to `1`. |
| `IdleSleep` | `1` | Pause idle D64 emulation at the supplied 1551 ROMs' TCBM wait loop to reduce Pi power use and temperature. TCBM/RESET/UI wake polling continues at 100 Hz; OLED shows an `S` after the track number while asleep. Set to `0` for timing diagnostics or unsupported custom ROMs. |

### Rotary encoder

| Option | Default | Meaning |
| --- | --- | --- |
| `RotaryEncoderEnable` | `0` | Startup-only enable for the fixed-wiring encoder; not a runtime menu item. |
| `RotaryEncoderInvert` | `0` | Runtime `Encoder` → `Reverse`; changes the next decoded impulse immediately. |

### Example configuration

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
SoundEnabled = 1
SoundOnGPIO = 1
tapeMotorAlwaysOn = 1
skipMotorSpinUpDelay = 1
IdleSleep = 1
QuickBoot = 1
```

The repository template at `sdcard/options.txt` contains a fuller commented
configuration.

## Upgrading And Building

For a normal firmware upgrade:

1. Build or download the new `kernel.img`.
2. Replace `kernel.img` in the SD card root.
3. Keep `options.txt`, ROM files, and `/1551` media in place.

If an update changes required SD support files, copy the changed files from the
repository `sdcard/` directory as well.

See [BUILD.md](BUILD.md) for toolchain installation and the canonical
`make RASPPI=3` build sequence.
