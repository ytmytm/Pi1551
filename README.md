# Pi1551

Pi1551 is a Commodore 1551 drive emulator for the Commodore C16, C116, and
Plus/4 family, authored by Maciej Witkowiak. It is based on Stephen White's
[Pi1541](https://cbm-pi1541.firebaseapp.com/) bare-metal Raspberry Pi project,
but this fork replaces the IEC/1541 target with a TCBM/1551 target.

Pi1551 has two independent jobs. For disk use, it needs a tcbm2sd paddle plus
a ribbon cable to a PI1551-hat or PI1551-III; in that setup the tcbm2sd board
is only the Commodore-side TCBM electrical interface, and its own SD-card 1551
simulator is disabled. Unplug the ribbon cable and the tcbm2sd board works
again as a standalone SD-card device. Separately, Pi1551 can be used as a TAP
player through the tape port only, without the TCBM ribbon cable.

## Main Features

- Cycle-exact Commodore 1551 emulation for D64 and G64 disk images.
- G64-only preservation of SuperDOS 1551 non-standard formatting, including
  per-track density changes that current YaPe/VICE releases do not handle
  correctly when formatting disks.
- File-level SD card access over TCBM, similar in spirit to SD2IEC/tcbm2sd
  browser mode (including direct `LOAD` of `.prg` files without emulation).
- SOLO/LIST media selection, mixed-format fliplists and `.lst` loading.
- `.prg` files can be mounted as read-only virtual one-file disks without
  starting drive emulation; ordinary Commodore `LOAD` remains direct SD access.
- Read-only file-level access to files inside D71, D81, D80, and D82 images.
- TAP playback and recording through the tape port. Matching
  IDX files let you jump directly to individual files on a tape and are created
  or updated automatically when recording a recognized Plus/4 `SAVE`.
- Eight-row I2C OLED browser/menu with rotary encoder, Insert and Back controls.
- Runtime drive, tape, display, encoder and sound settings with safe load/save.
- Multiple 1551 ROM slots and runtime ROM switching.
- 1551 RAM expansion ([1551-RAMBOard](https://github.com/ytmytm/1551-RAMBOard))
  and its ROM patch, including support for [Parobek](https://github.com/ytmytm/plus4-parobek)
  fastloaders.

## Media

### Commodore Users Europe presentation

<a href="https://www.youtube.com/watch?v=4uN6pWvh76o" target="_blank">
 <img src="https://img.youtube.com/vi/4uN6pWvh76o/mqdefault.jpg" alt="Commodore Users Europe presentation about Pi1551" />
 <p><small>Click for video</small></p>
</a>

## Required Hardware

![Pi1551 hardware architecture](docs/tcbm2sd-pi1551-system.png)

Pi1551 needs an interface between the Raspberry Pi and the Commodore TCBM/tape
signals. Use one of the Pi1551 interface projects:

- [PI1551-hat](https://github.com/ytmytm/PI1551-hat)
- [PI1551-III](https://github.com/ytmytm/PI1551-III)

The Raspberry Pi 3 must be powered from its own external USB power supply. Do
not rely on the Commodore, tcbm2sd paddle, or Pi1551 interface board to power
the Raspberry Pi.

That interface connects by ribbon cable to the C16/C116/Plus/4 expansion-port
TCBM cartridge, also known as the paddle:

- [tcbm2sd](https://github.com/ytmytm/plus4-tcbm2sd)

tcbm2sd board revision 1.3 or newer is required because Pi1551 uses the ribbon
cable connector added on those boards. For full compatibility, update the
tcbm2sd CPLD firmware/JED file to version 1.4 or newer. Some software, including
games such as Corruption and Fish, intentionally or accidentally uses mirrored
TCBM register addresses; older CPLD firmware does not handle those accesses
correctly.

Hardware schematics, connectors, level shifting, cable orientation, and board
assembly details are intentionally documented in those hardware repositories,
not here.

## What Changed From Upstream Pi1541

This repository still carries Pi1541's bare-metal Raspberry Pi framework, FAT
filesystem code, screen code, disk image handling, and browser concepts. The
current build, however, is the Pi1551 variant:

- `src/Pi1551.*`, `src/Drive1551.*`, and `src/m6523.*` implement the 1551 CPU,
  drive, and MOS 6523 TPI model.
- `src/tcbm_bus.*` implements the Raspberry Pi GPIO TCBM bus interface.
- `src/tcbm_commands.*` and `src/commands_base.*` implement browser-mode
  file commands over TCBM.
- `src/tape_player.*` adds C16/C116/Plus/4 TAP playback and recording.
- `Makefile` builds the Pi1551 object set by default.

This is not a drop-in replacement for upstream Pi1541 hardware. Drive use
requires a Pi1551 interface and a TCBM paddle.

## Getting Started

Copy `sdcard/` to the root of a FAT32 SD card, add a built or released
`kernel.img`, and put disk images, PRG files, TAP files and optional matching
IDX files under `/1551`. The firmware starts in that folder. A typical card
root contains:

```text
kernel.img
config.txt
options.txt
bootcode.bin
fixup.dat
chargen.bin
dos1551.bin
dos1551-ram.bin
super_dos_1551.rom
super_dos_ram.bin
1551/
```

The [options template](sdcard/options.txt) is the starting configuration.
The [user manual](USER-MANUAL.md) covers hardware setup, first use,
the [OLED menu map](USER-MANUAL.md#menu-reference),
[TAP playback and recording](USER-MANUAL.md#tap-playback-and-recording),
the [option reference](USER-MANUAL.md#optionstxt), and upgrades.

## Building

Pi1551 supports Raspberry Pi 3 family boards only: Raspberry Pi 3A, 3A+, 3B,
and 3B+. See [BUILD.md](BUILD.md) for toolchain installation, CI-friendly
commands, and the canonical `make RASPPI=3` build sequence.

## Documentation

- [User manual](USER-MANUAL.md): setup, controls, configuration, modes.
- [Build guide](BUILD.md): toolchain installation and compilation.
- [Pi1551 review/how-to notes](docs/PI1551-Review-and-HowTo.md): development
  notes, GPIO notes, and lower-level implementation details.
- [Original Pi1541 documentation](https://cbm-pi1541.firebaseapp.com/): useful
  background for Pi1541 concepts, but it mixes software and hardware details
  for upstream Pi1541. For Pi1551 hardware, use the hardware projects above.

## Credits

Pi1551 is based on Stephen White's Pi1541. The 1551/TCBM work, tape support,
and Pi1551 hardware integration build on that foundation.

Thanks to:

- Stephen White for Pi1541.
- The VICE project and Commodore documentation/disassemblies used as reference
  material.
- The authors of the third-party components listed in `3rdPartyFiles.txt`.

Pi1541/Pi1551 is licensed under the GPL. See [LICENSE](LICENSE).
