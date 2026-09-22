# Native TCBM2SD protocol tests

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
