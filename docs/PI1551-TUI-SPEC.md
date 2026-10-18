# Pi1551 OLED TUI — current protocol and behaviour

This document describes the current implementation of
`tools/pi1551_oled_tui.py` and the matching Pi1551 firmware menu. The simulator
remains the interaction reference; the firmware uses real media, GPIO and
persistent settings. Hardware-specific details and native testing are described
in `docs/PI1551-OLED-IMPLEMENTATION.md`.

## Display model

The simulated OLED is 128x64 pixels with an 8x8 C64-style font: 16 columns by
8 rows. The inner framed area is the complete OLED. The terminal area outside
the frame contains only simulator aids: the menu tree, messages and key legend.

One OLED column is reserved for a selection marker, leaving 15 characters for
normal row content. Long selected names scroll horizontally. The scrolling
width accounts for prefixes: `1:` in the tape menu, the current image type
(`D64 `, `D81 `, `PRG `, etc.), `DOS ` and `TAP ` on Status, and fliplist
prefixes such as `1>`. ROM filenames also scroll in the selector.

## Controls

- Left/right: encoder rotation. This is the only selection/value navigation.
- Enter: encoder push.
- `i`: Insert button.
- `b`: Back button.
- `r`: computer reset.
- `m`: live computer tape MOTOR line.
- `q`: quit the simulator.

Up/down are not navigation controls. `Back` and an explicit `..` item have the
same meaning in menus.

## Startup and persistent status

The simulator starts in Browser at `/1551`, with `Menu...` visible and the
cursor on the first SD entry (`[..]`). Entering Browser unmounts the active
image.

OLED rows 0 and 1 are reserved in the main views and menus:

### Row 0 — drive and temperature

The drive status is left-aligned and the temperature is right-aligned, for
example `8: 18.0      38C`.

- Emulating D64/G64: `8: 18.0` with the current track/half-track.
- Append `S` while the idle trap is actually active: `8: 18.0S`.
- No emulated D64/G64: `8:<last SD folder>`, for example `8:games`
  for `/1551/games`; SD root is `8:/`. The device prefix stays fixed, without a space.
  With temperature ON, four rightmost columns are reserved for ` 38C`;
  OFF gives the folder all remaining columns. Long folder names scroll
  independently of the selected row, at the configured Scroll speed, including
  the complete suffix. Changing directory or header width restarts that scroll.
- The simulated temperature is currently `38C`.

### Row 1 — tape status

The row has a fixed layout:

- no TAP, motor off: `TAPE --- --% --`
- no TAP, motor on: `TAPE --- --% M-`
- TAP loaded, stopped, motor off: `TAPE 000 00% --`
- TAP loaded, stopped, motor on: `TAPE 000 00% M-`
- TAP playing, motor off: `TAPE 000 00% -P`
- TAP playing, motor on: `TAPE 000 00% MP`

`M` is the effective tape motor state: always on in `Motor ON ALWAYS`, or the
live computer input in `Motor ON COMP.`. The `m` key toggles that computer input
even in `ALWAYS` mode, but cannot turn the effective motor off there. `P` means
PLAY is pressed; a loaded TAP can be stopped and played independently. Motor
state is independent of tape presence and disk activity.
The simulated counter is 000..999 and the percentage is derived from it.
The firmware derives the counter from actual TAP timing using the existing
Datasette spool model. At the end it may show `100%`, which still fits the
16-column row; PLAY remains pressed at EOF until Stop or eject.

## Main views

### Browser view

The third OLED row is always selectable. It reads `Menu...` in `SOLO` and
`Menu... LIST` in `LIST`; only the list-building mode needs an explicit
indicator. The list count is omitted in Browser. The five rows below it list
SD entries. The full path is retained internally and shown in the
terminal-only menu tree; the status header shows only its last component.

The currently browsed SD directory is also the virtual file-list source seen
by the computer. Enter and Insert on a directory both enter it and update this
source; entering `[..]` or pressing Back to its parent updates the source too.
This removes any hidden distinction between the visible directory and the
computer's source. The terminal-side menu tree shows the source path; the OLED
does not spend a row on it.

The Browser cursor uses index 0 for `Menu...`; real entries use indices 1 and
up. When Browser is entered or reset, focus starts at index 1 if an entry
exists (normally `..` inside a directory). Therefore left from the first
entry returns to `Menu...`, and right from `Menu...` selects the first entry.

Labels follow these rules:

- real SD directories are shown in brackets, e.g. `[roms]`;
- disk images and browser containers are not bracketed;
- any disk image or browser container already in the fliplist gets a prefix
  such as `1>` or `2>`;
- `[ .. ]` is represented as `[..]` when a parent directory exists.

Enter on a Browser entry does the following:

- `Menu...`: open the Device menu, keeping Browser as its parent;
- directory: enter the directory and use it as the virtual file-list source;
- `[..]`: go to the parent directory and use it as the source;
- D64/G64 and PRG/D71/D80/D81/D82: apply the current `SOLO`/`LIST` rule,
  mount the selected medium and show Fliplist. D64/G64 starts emulation;
  PRG/D71/D80/D81/D82 becomes the active browser-mode container. Enter does
  not open a container's contents on the OLED;
- `.lst`: load the listed disks as the fliplist and show Fliplist; an empty
  list leaves Browser open, reports that it is empty and selects `SOLO`;
- TAP: load the independent tape and remain in Browser;
- ROM/BIN file: ordinary Browser file; it has no special action. ROM changes
  are available only through `Settings → ROM`.

Insert in Browser is contextual:

- D64/G64 or PRG/D71/D80/D81/D82: switch to `LIST`, toggle its membership in
  the fliplist without changing the mounted medium, and remain in Browser;
- directory: enter it and use it as Browser source, just like Enter;
- TAP: load it;
- `Menu...`: open the Device menu.

Media selection has two visible modes:

| Mode | Enter on a disk image or container | Insert on a disk image or container |
| --- | --- | --- |
| `SOLO` | Replace the fliplist with the selected medium, mount it and show Fliplist. The list has exactly one item. | Switch to `LIST`, preserve any current SOLO item, toggle the selected medium in the list without mounting it and remain in Browser. |
| `LIST` | Add the selected medium if absent, mount it and show Fliplist. Existing entries are preserved. | Toggle the selected medium in the list without changing the mounted medium and remain in Browser. |

Thus Insert can collect the earlier images and Enter on the last image both
adds it and enters Fliplist. Loading an `.lst` file also selects `LIST` mode.
Selecting `Browser` from the Device menu clears the fliplist and returns to
`SOLO`.

An image with a numbered Browser prefix is already in the fliplist. Insert on
that image removes it and renumbers later entries. Insert on an image without
a prefix adds it. Removing the currently mounted image only removes its list
entry; it remains mounted until another medium is selected or Browser is
opened.

Removing the last entry from `LIST` returns to `SOLO`. This prevents a later
Enter on a single image from creating a persistent one-item fliplist. Enter
from an empty `.lst` likewise starts in `SOLO`.

Every transition from Fliplist to Browser unmounts the active medium and clears
the active image status. In `SOLO`, it also clears the temporary one-item list,
so Browser does not show an isolated `1>` prefix. In `LIST`, the list remains
available, but no image is mounted while browsing. Back from Browser restores
the prior Fliplist selection and mounts that image again. If the saved cursor
was on `Menu...`, it stays there while the most recently active listed image
is remounted. If Insert is then
used from a SOLO Browser, switching to `LIST` starts a list from the newly
selected medium; the previously unmounted SOLO image is not implicitly added.

There is deliberately no `[Use this dir]` entry. Enter and Insert provide the
directory operations without spending an OLED row on a synthetic item.

### Computer CD commands and panel synchronization

Computer commands and panel navigation share one mounted-medium state and
one SD directory source. CD into an SD folder selects Browser, unmounts any
active medium after flushing it and discards obsolete submenu/Back targets.
CD selecting media uses the same SOLO/LIST rules as panel Enter and updates
the SD source to the containing directory. D64/G64 starts emulation;
D71/D80/D81/D82/PRG remains a mounted file container in Fliplist.

CD.. and CD<- (the Commodore left-arrow/PETSCII underscore form) leave a
mounted image for its containing SD directory, not that directory's parent.
The next parent command climbs one SD level. At SD root it stays at root.
CD/ goes to the volume root; CD// goes to the /1551 starting folder.
An external exit clears temporary SOLO media but retains an explicit LIST;
it does not eject TAP. Old UI history cannot remount an image behind the
computer's back. A flush failure retains the mounted image and reports an
error rather than advertising a successful unmount.

Refresh without navigation updates the directory listing while preserving
the current submenu. Refresh of an already mounted container does not mount
it again or duplicate its fliplist entry. FatFS SD:/ and 0:/ prefixes are
normalized to the panel's / paths. The kernel consumes a ROM-trapped parent
command before restarting the TCBM state machine, so it cannot be lost or
applied twice. Host callbacks run outside the emulation microsecond loop.
The native tests cover these interactions; the TUI does not yet expose an
interactive Commodore command-channel console.

### Fliplist view

The first content row is always selectable: `Menu...` in `SOLO` and
`Menu... LIST N` in `LIST`, where `N` is the number of images (counts above 99
are shown as `Menu... LIST99+` to fit 15 content columns). The count appears only in Fliplist, not in Browser. Up to
five images are shown below the menu row. The active image has a `▶` marker;
the selected image row is inverted. This five-row window follows the cursor so
every fliplist entry can be seen and selected.

- Enter on `Menu...`: open the Device menu.
- Enter on any selected fliplist image opens Browser and unmounts the active
  image. In `LIST` it preserves the fliplist; in `SOLO` it clears the temporary
  one-item list. It opens the SD directory containing the selected medium and
  focuses that medium there, for emulated images and non-emulated containers alike.
- Left/right mounting a non-emulated fliplist entry (D71/D80/D81/D82/PRG)
  keeps the UI in Fliplist. The computer sees the mounted source as a
  directory-like medium. Enter is the separate action that returns to its
  containing Browser directory.
- Right from `Menu...`: activate and mount the first image immediately. This
  applies to both emulated and browser-mode containers.
- Left from the first image: return to `Menu...`.
- Every left/right move that lands on an image immediately activates that
  image. There is no deferred activation or special handling based on which
  image was active before entering Fliplist.
- Left/right between images: change the active medium. At the list
  ends, image selection wraps, except for the explicit first-image-to-menu
  transition above.
- With no fliplist, left/right opens Browser.
- This navigation is unchanged for a one-image fliplist: left from the image
  moves to `Menu...`; right from `Menu...` returns to and activates the image.
  Enter on the image opens Browser.
- Insert switches to `LIST` and opens Browser, preserving its entry in the
  list while unmounting the medium.

Back from Browser at the SD root returns to its caller when that view remains
valid: Tape after `Load TAP...`, or a non-empty Fliplist. Otherwise it stays in
Browser. Back within Browser subdirectories goes to the parent directory.
Opening Browser from Fliplist remembers the Fliplist cursor and restores it on
return. Selecting `Browser` from the Device menu starts a fresh Browser root
and clears stale navigation history. ROM/BIN files in Browser remain ordinary
files; Back and the encoder do not select or apply them. ROM changes are made
only through `Settings → ROM`.

Selecting Browser from the Device menu clears the fliplist and active disk,
then selects `SOLO`. This is the explicit full reset of both list and mounted
medium. Any other transition from Fliplist to Browser also unmounts the active
medium; only the list-preservation behavior differs between `SOLO` and `LIST`.
Fliplist entries may be either emulated D64/G64 images or non-emulated Browser
containers; switching between them changes the active medium accordingly.

## Device menu

The Device menu contains `..`, then `Clear list` only while the fliplist has
one or more entries, followed by `Browser`, `Tape`, `Disk`, `Settings`, `Status`
and `Reboot Pi1551`. `Clear list` empties the fliplist, selects `SOLO`, unmounts the active
medium and leaves the menu open. Its parent becomes Browser, so Back cannot
return to an empty Fliplist. The cursor moves to `Browser` after the option
disappears. `Browser` continues to clear the list and open the SD Browser.

The menu is reached from `Menu...` in Browser or Fliplist. Its current order is:

```
..
Browser
Tape
Disk
Settings
Status
Reboot Pi1551
```

Entering a menu places focus on the second item, below `..`. `Browser` clears
the fliplist and active disk, selects `SOLO`, and enters Browser. `..` returns
to the parent view.

`Reboot Pi1551` opens a separate confirmation menu: `..`, `Cancel`,
`Reboot Pi1551?`. Focus starts on `Cancel`, so a second accidental Enter
cannot restart the device. Enter on `Cancel`, Enter on `..`, or the Back button
returns to the Device menu without changing state. Only Enter on
`Reboot Pi1551?` performs the restart. This is a restart of the Pi1551 itself,
not `Disk → Reset drive` and not a RESET signal to the computer. It is not
suppressed by the `Reset IGNORE` host-reset setting.

In the TUI mock, confirmation returns to the initial Browser view and resets
volatile state, including the mounted medium, fliplist, tape, computer MOTOR
input, counters and navigation. It also restores the mock's initial settings,
representing a reload from the fake options file; the mock has no persistent
settings storage and does not perform a hardware reset. The target firmware
must safely finish pending image and SD writes, close or synchronize files,
then invoke its existing full-Pi reboot path. After boot it rereads settings
and ROM configuration and applies normal auto-mount rules; it must not assert
RESET to the host computer as part of this menu action. If writes cannot be
safely completed, the reboot must not be reported as safe or successful.

## Tape menu

The current order is:

```
..
1:<loaded TAP filename>
Play or Stop (only when loaded)
Load TAP... (only when empty)
Eject TAP (only when loaded)
Rewind (only when loaded)
Go to counter (only when loaded)
```

The filename row shows exactly `1:` when no TAP is loaded. When a TAP is
loaded it shows its name after the fixed `1:` prefix and scrolls. The menu
shows `Play` or `Stop` according to PLAY state. With no TAP it shows
`Load TAP...`; eject, rewind and seek only appear while a TAP is loaded. The
counter and motor are not duplicated here because they are always visible in
OLED row 1.

- `Load TAP...`: unmounts the active disk/container and opens the SD Browser
  filtered to TAP files, starting at the SD root. In `LIST`, entries are kept
  for a later return to Fliplist; a temporary `SOLO` entry is cleared. Back
  returns to Tape; choosing a TAP loads it and returns to Tape;
- `Play` / `Stop`: presses or releases PLAY while keeping the loaded TAP;
- `Eject TAP`: removes the TAP and releases PLAY, without changing motor state;
- `Rewind`: sets counter to 000 when a TAP is loaded; otherwise reports
  `No TAP loaded`;
- `Go to counter`: opens a numeric editor only when a TAP is loaded;
  otherwise reports `No TAP loaded`. Left/right changes the target
  000..999, Enter seeks to it, and Back/`..` cancels.

## Disk menu

```
..
WProt ON/OFF
Write image
New image
Reset drive
```

`WProt` is abbreviated so its ON/OFF value fits the OLED. `Write image` and
`Reset drive` are simulated actions. `Reset drive` is a local action and still
works when host RESET is set to `IGNORE`; it does not change the Browser path.
`New image` opens a submenu with `..`, `Type D64/G64` and `Create image`.
Focus starts on Type. Clicking Type toggles the format using `NewDiskType`;
no file is created until Create image is selected. Creation uses the current
Browser directory and the next unused `NEWIMAGE1.d64`, `NEWIMAGE2.d64`, etc.
(or `.g64`). It does not mount the new medium or alter the fliplist. Back and
`..` return to Disk without creating anything. The format remains the current
default and is persisted only when Save settings is used.

## Settings

Settings are grouped and contain only values that can be changed with an
encoder. There are no free-text fields.

```
..
ROM
Drive
Tape
Browser
Display
Encoder
Sound
Load settings
Save settings
```

Submenus:

- Drive: device number, idle trap, RAMBOard, `Skip spinup` ON/OFF and `Reset`
  `REACT/IGNORE`; `REACT` means host reset pulses are honoured. These labels keep
  both the option and its value visible on the 16-character OLED row;
- Tape: `Motor ON` selects `ALWAYS` or `COMP.`, and `Autoplay` selects ON/OFF.
  `ALWAYS` keeps MOTOR active; `COMP.` follows the computer MOTOR input.
  `Autoplay ON` presses PLAY as a TAP is loaded; OFF loads the TAP stopped;
- Browser: visible items are `Auto mount`, `Reset path`, `Lowercase` and
  `SD2IEC Cmds.`; `Auto mount ON` enables the configured `AutoMountImage` (which may
  be an image or an `.LST`), while `OFF` disables automatic mounting;
  `Reset path ON` returns Browser to the `/1551` starting folder after a
  computer reset, including `Reset IGNORE`; `OFF` preserves the current Browser folder;
- Display: `Temp`, `Contrast`, `Dimming`, `Dim time`, `Dim level`, `Orient`,
  `Font` and `Scroll`. Temp controls only the header; Status still shows TEMP.
  Dim time is in seconds (0/OFF disables automatic dimming), Dim level is
  idle brightness 0..255; zero sends DISPLAY OFF, and any UI input wakes the
  panel. Active Contrast is restricted to 1..255 (startup zero is clamped to 1;
  Load settings rejects zero). The timeout is measured since the last UI
  input and begins dimming when Dim time expires. Labels omit LCD, while the
  existing `LCDName`/`i2cLcd...` configuration keys remain compatible;
- Encoder: the only runtime item is `Reverse ON/OFF`; encoder presence/enabling is a
  hardware/startup option and is not exposed in the runtime menu;
- Sound: `Sound` ON/OFF and `Mode SAMPLE/BUZZER`. Mode maps to `SoundOnGPIO`:
  0/SAMPLE plays the embedded click using PWM/DMA; 1/BUZZER generates a square
  wave. Both use fixed GPIO13 (SOUND). Only BUZZER displays `Freq` in Hz and
  `Duration` in milliseconds; SAMPLE has a fixed clip length;
- ROM: separate ROM selector.

Settings cycle on encoder click, as in the mock; rotation continues navigating
rows. The counter editor is the exception: one impulse changes the target by
one, Enter seeks and Back cancels. Firmware Font selects C64/ASCII, both 8x8;
At startup both font slots are prepared: built-in ASCII and C64 from the
configured ChargenFont/Font file (exactly 4096 bytes). If loading fails or size
is invalid, both slots contain ASCII. Font selection switches the cached slot
on the next frame, without restarting, reading SD or reloading HDMI fonts.
The font filename remains a startup-only setting. Errors and completed
load/save/write actions use a dismissible message menu with `..` first. Enter
on the message or Back returns to the previous screen; long messages scroll.

Font uses the existing `i2cLcdUseCBMChar` (0 ASCII, 1 C64), and Scroll uses
`scrollHighlightRate` in seconds per character step. There are no independent
`oledFont` or `oledScroll` values; obsolete entries are ignored and preserved
as unrelated settings. Scroll cycles 0.05/0.10/0.20/0.35/0.50/1.00 seconds.
The Scroll row keeps its label and speed stationary, followed by a scrolling
`Scrolling preview 0123456789` demo using the selected speed.
Dim time cycles 0/5/10/30/60/120/300 seconds. Contrast cycles
16/32/48/64/80/96/112/128/144/160/176/192/208/224/240/255 (never zero).
Dim level cycles 0/16/32/48/64/80/96/112/128/144/160/176/192/208/224/240/255.
Duration cycles 0/50/100/250/500/1000/2000/5000 ms.
Custom file values within supported ranges remain valid until changed.

Every successful change in Sound (Sound, Mode, Freq, Duration) starts a
one-second preview window with the new settings. SAMPLE loops the loaded
click; BUZZER uses the new frequency for the configured Duration, capped at
one second, with silence for the rest of the window. Duration 0 and Sound OFF
are silent. A new change replaces the preceding preview. It never changes
the stored Duration just for demonstration. No preview runs on menu entry,
Back, unrelated settings or load/save. Firmware plays through autonomous PWM
DMA on the fixed SOUND pin; TUI records the one-second mock preview/message
without emitting terminal bells or trying to reproduce audio. Menu, disk and
tape continue; ordinary head-step audio is suppressed during the preview.

Runtime application: Reverse affects the next decoded encoder step, without
reinitializing GPIO or losing quadrature state. Display options apply on the
next frame (up to 100 ms); idle dimming waits for Dim time. Device number,
RAMBOard, idle trap, host reset policy, tape motor policy and sound settings
are read live or applied by the settings adapter. Lowercase changes the next
directory listing supplied to the computer, not SD filenames on the OLED.
Skip spinup affects the next ROM spinup delay; Reset path applies on any
host reset, independently of the drive policy; Auto mount applies on the
next respected host reset (and at boot). New image Type
affects the next created image, never converts a mounted disk. These
event-dependent settings do not force a reset, remount or file creation.

The buzzer uses the board's fixed SOUND output, GPIO13 (physical pin 33).
GPIO18 belongs to the TCBM data bus and GPIO19 is cassette READ. Neither is
selectable for sound; disk sound and cassette playback operate independently.

In the target firmware, `Load settings` rereads the configured options file
and applies its runtime-changeable values, replacing unsaved changes. It must
report a read/parse failure without falsely reporting success; startup-only
hardware options remain boot-time settings. `Save settings` persists the
changed runtime values for future boots. It rereads the current file, preserves
the user's order, key spelling, whitespace, comments and line endings, and
replaces only the value in the last effective definition of a changed option.
Every absent changed option is appended, including default values. Untouched
options (including independent user edits) and duplicate earlier definitions
remain unchanged. A no-op save performs no writes. A write failure must leave the current runtime state intact and be
reported with its failing stage and, where available, FatFS result code.
An existing options.txt.tmp is reported and retained. Each changed save keeps
the previous `/options.txt` as `/options-backup.txt`, replacing any older
backup; recover it manually if power fails during the rename sequence. Reboot reports
host/image/media flush, SD sync or unmount failure and remains blocked until
the storage checks succeed. Neither settings action itself mounts media or resets the device. The TUI
mock only reports these actions using its fake `options-pi1551.txt`; it does
not read or write a file.

`Auto mount` controls whether a configured `AutoMountImage` is applied on boot
or a processed host reset. A valid image or `.LST` is mounted or loaded using
the same `SOLO`/`LIST` rules as a manual selection. If the setting is empty,
the target does not exist, or its type/content is unusable, auto mount behaves
as `OFF`: no automatic selection is attempted, and the ordinary boot/reset
behavior applies. The target firmware hides `Auto mount` from the menu in
those three cases. The TUI mock deliberately keeps the switch visible; it
does not simulate automatic selection on startup or reset.

### ROM selector

The ROM selector lists filenames only, in the order supplied by the `ROM1`,
`ROM2`, … entries in `options.txt`. `..` is first and the first configured ROM
is the initial focus. The active ROM has a separate `▶` marker; the encoder
cursor is shown by row inversion. Clicking a different ROM applies it
immediately. Clicking the already active ROM or selecting `..`/pressing Back
exits. The selected filename scrolls when it exceeds the OLED row. The SD
Browser's `roms` directory is unrelated and never populates this selector.

## Status screen

Status is read-only and scrolls through these entries after the fixed two
status rows:

```
..
TEMP 38C
SD 8GB
FREE 6.4GB
FW 0.3.0
DOS <selected ROM>
<type> <current image name>
TAP <current TAP name>
LED ON/OFF
MOTOR ON/OFF
PLAY ON/OFF
TRACK 18.0S / ---
```

`<type>` is the active medium's short extension (`D64`, `G64`, `D71`, `D80`,
`D81`, `D82`, `PRG`), or `IMG` when none is active. The `DOS`, image and `TAP`
filenames scroll while their prefixes remain fixed. `TRACK` is `---` without
an emulated disk and gets the `S` suffix only when idle trap is active. `MOTOR`
shows the effective tape motor state. The fake SD size, free space, firmware
version and temperature are simulator data. The virtual SD source path is
shown in the terminal-side menu tree, not on the OLED Status screen.

## Fake SD contents and `.lst`

The fake card contains ordinary directories, D64/G64 images, non-emulated
PRG/D71/D80/D81/D82 browser containers, TAP files, ROMs, configuration files,
and:

```
/1551/lists/favorites.lst
/1551/lists/autoswap.lst
```

The list files contain simulated references to disk image nodes. Entering a
list file replaces the current fliplist with those entries, selects the first
image and selects `LIST` mode, even if the file contains only one image. An
empty list does not open an empty Fliplist view.

## Implementation boundaries

The simulator keeps Browser path/source, active media, emulated D64/G64,
fliplist mode, fliplist contents, TAP state, settings and selected ROM as
separate state. Screen history stores the parent screen and its cursor/window
position. Entering Browser unmounts the active image. Selecting D64/G64 starts
emulation; selecting a non-emulated container leaves emulation off while
keeping that container active as the mounted browser medium. Browser list
prefixes stay fixed while the filename scrolls.
The current fake `Auto mount` option is selectable as a setting, but the
simulator does not emulate firmware startup or parse a real `options.txt`.
`Reset REACT/IGNORE` controls whether computer RESET resets the drive CPU
and permits reset-time automount. Independently, `Reset path ON/OFF`
controls only the Browser path on every computer RESET: it returns to `/1551` or
keeps the current path. It does not by itself unmount the active image or clear
the fliplist. A valid enabled `AutoMountImage` may separately select a new
image or list during a REACT reset, not an IGNORE reset.
