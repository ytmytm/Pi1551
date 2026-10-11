# Current OLED vs. menu simulator

This is a review of the current Pi1551 OLED path in `src/FileBrowser.cpp`,
`src/ScreenLCD.cpp`, `src/tape_player.*`, `src/options.*` and
`src/InputMappings.*`, compared with `tools/pi1551_oled_tui.py`.

## Information already present in the simulator

| Current OLED / firmware | Simulator status |
| --- | --- |
| Browser file/folder list | Present in **Media browser** |
| Selected long filename scrolling | Now present; selected row pauses and scrolls in both directions |
| Mounted disk / current image | Present on dashboard and in **1551 drive** |
| Track and coarse emulation activity | Present on dashboard |
| TAP counter, percentage and motor state | Present in **Tape player** and dashboard |
| Fliplist/caddy selection | Present as an explicit **Fliplist / disk caddy** page |
| ROM selection | Present as explicit **ROM slots** page |
| Device number | Present as explicit **1551 drive** page |
| Idle sleep/trap concept | Present as a runtime option |
| Computer reset and tape MOTOR input | Present as `r` and `m` |

## Current OLED information not yet modeled accurately

| Current firmware behavior | Gap in simulator / proposed follow-up |
| --- | --- |
| Browse OLED reserves row 0 for tape status when a TAP is loaded | Simulator shows tape as a dashboard row; it does not reduce browser list height dynamically |
| Tape status uses a three-digit counter plus `P` tape-sense and `M` motor flags | Simulator uses percentage and `MOTOR/STOP`; add exact `000 P M` presentation |
| Emulation OLED shows `track temperature` on row 0 and tape status on row 1 | Simulator has a richer dashboard, but not the exact two-row emulation layout |
| Actual status bar contains LED, Motor, Track, DAV, ACK, ST0 and temperature | Simulator compresses this to activity and track; bus signals are missing |
| Browser displays `[folder]`, read-only `<`, and caddy index prefixes | Simulator labels the type separately and does not show read-only/caddy markers in the filename |
| Current firmware scroll rate is `scrollHighlightRate` from `options.txt` | Simulator uses a fixed prototype timing; expose this as a setting later |
| OLED width is derived from the actual font/model (128x32 or 128x64) | Simulator assumes the 8-row concept and a terminal width, with no 4-row mode |
| Device/ROM feedback is shown temporarily by `ShowDeviceAndROM()` | Simulator keeps both values persistently visible, but has no transient confirmation overlay |
| Browser can show SD/USB device roots | Fake SD currently has only one virtual SD root |

## Runtime options

The simulator now exposes the existing `options.txt` concepts on a scrollable
**Runtime options** page, including media defaults (`AutoMountImage`,
`StarFileName`, `AutoBaseName`, `NewDiskType`), drive behavior (`IdleSleep`,
`RAMBOard`, motor/spin-up, reset policy), browser/TCBM flags, sound settings,
ROM/font settings, rotary settings, and OLED/I2C model, address, size,
contrast, dimming and font settings. Values are intentionally simulated and
cycle with Enter; no `options.txt` is written.

The page is longer than one OLED viewport, so the cursor scrolls the option
window while the inner OLED frame stays fixed. In a firmware implementation
these entries should still be grouped into submenus (Drive, Browser/TCBM,
Display, Input and Media) rather than appearing as one flat list.

## Input features not yet modeled

The prototype now makes the main actions explicit in the menu and status
pages. The remaining firmware-only actions are page up/down, home/end, search
prefix, write-protect toggle, new D64 creation, auto-load, LST generation,
tape read/sense toggles, and ROM/device shortcuts using held buttons or
function keys. They should become explicit browser actions before finalizing
the hardware menu.
