# Native Pi1551 tests

## 6502 CPU differential test

Run `make test-m6502-native` to compare the production core with the frozen
pre-optimization `tests/reference/m6502.cpp` and `.h`. Both `-O1` and `-Ofast`
builds use ASan/UBSan. Each build executes 65,536 scenarios covering all 256
opcodes and status-byte patterns, decimal arithmetic, IRQ pulses, SO and
resets, with 4,194,304 CPU cycles compared. After each cycle the test compares
registers, transient data state, interrupt masks, SYNC and the ordered bus
reads/writes, including accesses whose results the CPU discards. Final RAM
contents must also match. The test pairs every named reference handler with
its production adapter, so it also verifies that the exact same microcycle
and opcode handlers are selected after each cycle despite the different
pointer representations. Separate exhaustive checks cover all 16,777,216
N/Z input/status combinations and all 4,096 interrupt-mask combinations.
This preserves the reference core's behavior with the production IRQ-only
configuration; it is not an independent test of NMOS 6502 conformance.

## 1551 drive update differential test

Run `make test-drive1551-native` on a host with GCC/G++. This builds the current
`Drive1551.cpp` and the frozen pre-optimization implementation in
`tests/reference/` as separate classes. It executes 60,000 calls to the real
`Drive::Update(16)` in 600 seeded scenarios, covering read/write transitions,
random track contents, bit-position wraparound and flux timing. After every
call it compares encoder state, TPI inputs and every byte of the disk track.
Both `-O1` and the firmware's `-Ofast` build use ASan and UBSan. The test
establishes agreement for those traces; it does not measure performance on
the Raspberry Pi.

The same target checks the production CPU read decoder against the frozen
decoder in `tests/reference/read6502_1551.inc`: every 16-bit address, all seven
ROM slots, both RAMBOard modes, 16K/32K and unknown ROM sizes, and four TIA
direction patterns. All 14,680,064 reads must match in returned data, GPIO
sampling calls and TIA input side effects. Only physical GPIO sampling is
stubbed; ROM/RAM decoding and the TIA implementation are production code.

When `~/Maciejdev/plus4/kicad-tcbm2sd/sdcard-tcbm2sd-pi1551/1551/demos/`
`Carrion's-121-colors.d64` exists, the `-Ofast` run also converts it to GCR
using `DiskImage::OpenD64`, reads every bit of all 40 tracks through both
`Drive::Update(16)` implementations, and compares each resulting drive/TPI
state. The test also reports the number of `rand()` calls during that read.
It writes the decoded D64 and a version with changed payload bytes via
`DiskImage::WriteD64` to temporary files and compares every output byte with
the data supplied for writing. Set `PI1551_TEST_D64=/path/to/image.d64` to use
another image. The source D64 is only read; outputs live in the test's
temporary directory.

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
