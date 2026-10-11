# Pi1551 OLED TUI simulator

Run from the repository root:

```sh
make tui
```

The simulator is independent of the Pi1551 firmware. Its inner frame is the
actual OLED viewport: 128x64 pixels, an 8x8 C64 font, 16 columns and 8 rows.
The right-hand menu tree, message and key legend are terminal-only.

The first view is Browser. Fliplist is the other main view. Both reserve rows
0 and 1 for drive and tape status. Left/right is the encoder everywhere;
Enter is its push; `i` is Insert; `b` is Back; `r` resets the computer; `m`
toggles the live tape motor line; `q` quits. Up/down are intentionally not
navigation controls.

D64/G64 selection starts emulation. PRG/D71/D80/D81/D82 are mounted as
browser-mode containers. Plain `Menu...` means Enter replaces the current
single medium; `Menu... LIST` means Enter adds and mounts a medium. Insert
switches media selection to LIST and toggles the selected medium's membership
without mounting it. The menu row is always available in Browser and Fliplist.
In a one item Fliplist, rotating either direction from the selected image opens
Browser; the `Menu...` row still activates that image when moving right.
Fliplist shows its item count in the menu row, while Browser shows only
`Menu... LIST`. The Status view includes the selected virtual SD source path.

The complete interaction contract is in
[`PI1551-TUI-SPEC.md`](PI1551-TUI-SPEC.md).
