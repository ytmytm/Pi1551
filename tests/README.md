# Native Pi1551 tests

Run the OLED/menu/component suite without Raspberry Pi:

```sh
make test-oled-native
```

It runs headless TUI checks and compiles the production menu, kernel adapter,
settings parser, head-step audio controller, TAP player, D64/G64/PRG code,
OLED raster, host-file buffering and the real CD parser with ASan/UBSan. GPIO/I2C and
kernel hooks record side effects; a POSIX FatFS shim supports disposable card
fixtures and injected write/sync/rename failures.

A separate integration test uses the real FatFS and diskio on in-memory
FAT16/FAT32 SD sectors (not the POSIX f_sync shim). It invokes the production
menu/adapter Save settings, Load settings and reboot confirmation, then
remounts to verify file bytes and preserved comments. It checks recovery-file
blocking, missing-device reboot refusal, initialization and signed I/O errors.

Mixed host/panel tests cover CD parents, exiting emulated/container media,
SD:/ and 0:/ aliases, changing directory while a submenu is open, LIST/SOLO
and tape retention, stale return histories and failed unmounts. CD parser tests
use real image mounting; only unrelated HDMI/volume-list output is stubbed.

See
[PI1551-OLED-IMPLEMENTATION.md](../docs/PI1551-OLED-IMPLEMENTATION.md) for coverage,
architecture and hardware-test limits.

## Plus/4 cassette interoperability

With the patched headless VICE and `tests/vice/roms.env` from the Parobek
checkout available, run:

```sh
python3 tests/test_vice_tape_interop.py
```

Pass `--vice-root PATH` if the Parobek checkout is elsewhere. The test saves
`hello.prg` to a C16 TAP in VICE, then checks VICE→Pi1551, Pi1551→VICE,
Pi1551→Pi1551 and VICE→VICE. The native bridge runs the production TAP parser,
GPIO playback loop and recorder through simulated GPIO edge interrupts. Before
every VICE load, the test fills the program's memory range with zero and confirms
that it is empty; it then compares all loaded PRG bytes with the reference.
Files and VICE logs are kept in a
temporary directory for the duration of the test.

## TCBM2SD protocol tests

Run from the repository root:

```sh
python3 tests/test_tcbm2sd_protocol.py
```

The test builds `tests/.build/tcbm2sd-native`, a line-oriented stdin/stdout
driver around the same protocol parser, secondary-address decoder, handshake
sequencer, and disk geometry routines used by the Raspberry Pi build.

Example interactive commands:

```text
read 01 08 aa
write de ad be ef
u0 1 55 30 00 28 03 01
talk 70 0 24 0
geometry d81 40 3
block-roundtrip /tmp/test.d81 40 3
```

Expected signal ordering is taken from `tcbm2sd.ino` and the corresponding
6502 clients `fastdir.asm`, `t2s-load.asm`, and `block-rw.asm` in the tcbm2sd
reference tree.
