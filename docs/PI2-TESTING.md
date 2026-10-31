# Experimental Pi 2B v1.1 build

This port targets **BCM2836 / four Cortex-A7 cores / ARMv7-A**, initially at
the stock 900 MHz. Hardware compatibility and real-time TCBM deadlines remain
unverified. Pi 3 / Zero 2 W remain the recommended targets and the default build.

The [QEMU icount experiment](PI2-ICOUNT.md) now records instruction demand,
timer jitter and instruction-budget projections for 900/1000/1200 MHz. These
are isolated-core cost scenarios and do not validate physical A7 timing.

## Build and install

```sh
make RASPPI=2 TARGET=kernel-pi2 -j4
make RASPPI=3 TARGET=kernel-pi3 -j4
python3 tests/test_pi_targets.py
python3 tests/test_pi2_qemu.py --rom stock
python3 tests/test_pi2_qemu.py --rom ramboard
```

The two architectures have separate firmware and USPi object directories.
Changing `RASPPI` does not require `make clean`; distinct `TARGET` names also
keep the final images separate. `make` still builds Pi 3 as `kernel.img`.
`make clean RASPPI=2 TARGET=kernel-pi2` cleans only that platform's objects
and the named outputs. Old `src/*.o` / `uspi/lib/*.o` are never linked.

Use a copy of an existing Pi1551 FAT SD card, with its usual `bootcode.bin`,
`start.elf`, `fixup.dat`, ROMs, `options.txt` and `1551/` directory:

1. Copy **kernel-pi2.img as kernel.img** to the card root.
2. Copy **sdcard/config-pi2.txt as config.txt** to the card root.
3. Keep the existing Pi1551 interface/paddle and GPIO wiring.
4. Keep spaces around `=` in `options.txt`, as in the repository template.

The configuration explicitly selects the 32-bit image and its existing linked
address, `0x1f00000`. Start at `arm_freq=900`, `core_freq=250` and
`force_turbo=1`. The `[pi02]` section in the normal configuration applies to
Zero 2 W, **not Pi 2B**. First establish reliable stock-clock operation; then
try 1000 MHz or another frequency already known stable on the particular Pi.
A7 at 1200 MHz is not a tested or guaranteed configuration. Do not change
voltage, SDRAM and CPU clocks together when diagnosing failures.

## Hardware test order

Record board revision, clock settings, power supply, interface/paddle firmware,
ROM, RAMBOard mode, software/image and the exact failure point.

1. Boot at 900 MHz; check HDMI/OLED, directory browsing, buttons/rotary and
   SD-card access. Check USB keyboard/storage if used.
2. In file/browser mode, test directory, KERNAL load/save and the tcbm2sd
   fastloader, including binary files and block read/write. Verify saved data
   by reloading and comparing it; use disposable test images.
3. Mount a D64 and repeat ordinary 1551 directory/load/save. Test both stock
   ROM and RAMBOard ROM, with the matching RAMBOard setting and fastloader.
4. Test disk changes, host RESET and return to browsing. Test TAP playback and
   recording separately if that interface is available.
5. Finally try timing-sensitive software, particularly Carrion's 121 Colors
   / Bitfire, at 900 MHz and then at any stable overclock.

Handshake-driven transfers can wait for a slower peer and are good initial
candidates, but that is not a guarantee: client timeouts, GPIO sampling,
electrical timing and the emulated drive's deadlines still matter. Success in
browser mode also does not establish compatibility in full 1551 emulation.

## Platform audit

`tests/test_pi_targets.py` compares preprocessed code for all **77 firmware and
USB source units** and the exported functions of both linked kernels. Shared
features must match after excluding these intentional differences:

- A7 cache maintenance uses the existing Pi 2 L2 geometry and ACTLR.SMP setup;
  A53 retains its own cache geometry and CPU register access.
- PMU diagnostic event labels use the existing Pi 2 table (still `TODO` labels).
- The Pi 3 firmware virtual-GPIO and touch helpers and their initialization
  are omitted on Pi 2. Touch polling is not used by the current application;
  `SetACTLed` remains a no-op on Pi 2. The external drive LED is retained.

Pi 2 keeps the same multicore arrangement: core 0 UI/IRQs, core 1 emulator and
TCBM commands, core 2 tape, core 3 idle. The old Pi 2 conditional would have
disabled active multicore use; this port enables it explicitly.

The audit also found that `_init_core` assigned the same six banked stacks to
cores 1 and 2. The shared startup now offsets each worker's stack region by
6 MiB. This fixes overlapping live stacks on **both Pi 2 and Pi 3**. Before
that common correction, the rebuilt Pi 3 image was byte-identical to the
pre-port image and all 49 firmware preprocessor outputs matched. With the
correction, only the common assembly startup changes in the Pi 3 source audit.

## Validation and limits

Native suites can select Pi 2 conditions without changing their default:

```sh
PI1551_TEST_RASPPI=2 make test-tcbm2sd-native test-tcbm-gpio-native test-drive1551-native
make test-m6502-native
```

These tests cover protocol behavior, ordered GPIO effects, 6502 cycles, drive
state and RAMBOard memory decoding. They do not measure A7 execution speed.
The drive test also covers a real 40-track D64 when its documented fixture is
available; see [tests/README.md](../tests/README.md).

The full `test-oled-native` suite currently fails to link its FatFS/OLED
fixture on both targets: it lacks `m_TCBM_Commands`, wall-clock methods and
`OledKernelHasRTC` / `LoadRTC` / `SaveRTC` test implementations. The earlier
TUI, GPIO, RTC-decoder and CPU-resume checks run before that failure. This
test-fixture issue is independent of the Pi 2 build.

QEMU uses the **actual kernel.img**, a disposable FAT16 card with the project's
1551 ROMs, and the `raspi2b` Cortex-A7 model. GDB monitors startup and exception
handlers; image selection is injected through the production menu hook.
Both stock-ROM and RAMBOard-ROM runs reached the browser TCBM poll on core 1,
the tape loop on core 2, D64 mounting, ROM fast boot and 1,001 CPU read callbacks
after entering real-time emulation. Worker stack pointers are checked against
their separate regions (near 24 MiB and 18 MiB).
QEMU replaces the boot firmware and physical machine, so it cannot validate
`config.txt`, actual VideoCore startup, cache-coherence timing, GPIO electrical
behavior, an external Plus/4 or a real loader handshake. In particular, QEMU
execution speed does not predict whether 900 or 1200 MHz meets the deadlines.

References: [Raspberry Pi boot configuration](https://www.raspberrypi.com/documentation/computers/config_txt.html),
[QEMU Raspberry Pi model](https://www.qemu.org/docs/master/system/arm/raspi.html).
