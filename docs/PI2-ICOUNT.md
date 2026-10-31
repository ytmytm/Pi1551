# Pi 2 instruction-budget experiment (QEMU icount)

The 900 MHz target has a narrow and unconfirmed timing margin in full 1551
emulation. Active-disk workloads need about one effective ARM instruction per
CPU cycle on average, with higher short-term requirements. These are cost
scenarios, not a measurement of a physical Cortex-A7.

## Setup

- Kernel SHA256: `0155b49337d35023cff794b051e6f02856d4294aab6645ade5ec9a8a57e845c7` (firmware unchanged).
- QEMU 6.2, `raspi2b`, TCG single-thread, `icount shift=0/1/2,sleep=off`.
- Actual Pi 2 image, normal boot and ROM fast boot, disposable blank D64.
- After boot, UI/tape/idle CPUs are parked in WFI and peripheral IRQs disabled.
  Recorded instruction counts on other CPUs are zero in every measured period.
- 120,000 warm-up loops, then 50,000 recorded loops per scenario and shift:
  **600,000 measured loops in 12 runs**. IdleSleep is disabled.
- The plugin counts guest instructions and memory accesses without injecting
  guest profiling instructions. Actual QEMU virtual-clock timestamps are read
  from the timer MMIO callback, where icount permits clock reads.
- Measurement spans consecutive synchronization completions. Work excludes
  busy waiting; first-timer-read latency includes three more synchronization
  instructions. In every sample, period time equals total instruction count
  times `2**shift` exactly, and work latency equals `(work_insns+3)*2**shift`.
- A marker inside Drive::Update verifies zero active encoder calls per idle
  loop and one active encoder call per loop in every motor-on scenario.

## Instruction demand

Instruction counts below exclude busy waiting and the three synchronization
instructions described above. P99 is the 99th percentile of sampled loops.
Required IPC includes those three instructions and all memory stalls would
have to fit within that effective IPC on real hardware.

| Workload | Mean instructions | P99 | Maximum | Required mean IPC at 900 MHz | Required P99 IPC |
|---|---:|---:|---:|---:|---:|
| Stock ROM, motor forced off | 514.1 | 631 | 636 | 0.575 | 0.704 |
| Stock ROM after boot, motor on | 878.8 | 1008 | 1048 | 0.980 | 1.123 |
| Synthetic polling, motor on, code in low RAM | 885.8 | 1025 | 1077 | 0.988 | 1.142 |
| Synthetic polling, motor on, code in RAMBOard RAM | 903.7 | 1035 | 1058 | 1.007 | 1.153 |

## Actual icount timer results

“Work >1 us” means first-read latency exceeds 1,000 virtual ns. It does not
necessarily mean a whole timer tick was skipped: a slightly long loop can be
followed by a shorter one. “Timer gaps” counts observed timer increments >1 us.
Effective CPU rate assumes the production loop's two emulated CPU steps.

| Workload | ns / ARM instruction | Work >1 us | Timer gaps >1 us | Effective 6502 MHz | Maximum loop period, ns |
|---|---:|---:|---:|---:|---:|
| stock-idle | 1 | 0.00% | 0.00% | 2.000 | 1002 |
| stock-idle | 2 | 58.81% | 3.56% | 1.931 | 1278 |
| stock-idle | 4 | 100.00% | 96.73% | 0.967 | 2556 |
| stock-rom | 1 | 1.90% | 0.00% | 2.000 | 1051 |
| stock-rom | 2 | 100.00% | 76.30% | 1.134 | 2102 |
| stock-rom | 4 | 100.00% | 100.00% | 0.567 | 4204 |
| stock-read | 1 | 5.26% | 0.00% | 2.000 | 1080 |
| stock-read | 2 | 100.00% | 77.65% | 1.125 | 2160 |
| stock-read | 4 | 100.00% | 100.00% | 0.563 | 4320 |
| ramboard-read | 1 | 7.88% | 0.00% | 2.000 | 1061 |
| ramboard-read | 2 | 100.00% | 81.06% | 1.103 | 2122 |
| ramboard-read | 4 | 100.00% | 100.00% | 0.551 | 4244 |

## Interpretation for 900 MHz

At effective IPC=1, an instruction costs about 1.111 ns at 900 MHz, between
icount shifts 0 and 1. At effective IPC=0.75, only 675 instructions fit in one
microsecond. Here is the instruction-only share of work latencies exceeding
that budget; this is an arithmetic projection, **not another QEMU run**:

| Workload | 900 MHz, IPC=0.75 | 900 MHz, IPC=1 | 1,000 MHz, IPC=1 | 1,200 MHz, IPC=1 |
|---|---:|---:|---:|---:|
| stock-idle | 0.00% | 0.00% | 0.00% | 0.00% |
| stock-rom | 100.00% | 28.01% | 1.90% | 0.00% |
| stock-read | 100.00% | 32.85% | 5.26% | 0.00% |
| ramboard-read | 100.00% | 45.69% | 7.88% | 0.00% |

This supports testing the port, but does not establish that stock 900 MHz
sustains cycle timing. The active-disk cases need effective IPC around 1;
RAMBOard RAM execution is slightly more expensive. At lower effective IPC,
sustained lateness is expected in this instruction-budget model. Higher
clocks reduce the requirement, but do not proportionally reduce MMIO delays.

## Limits and reproduction

The motor-off and stock-ROM cases retain the ROM CPU workload. Synthetic
polling is `SEI; loop: LDA $4001; LDA $4002; STA scratch; JMP loop`, with code
at $0300 (stock) or $8000 (RAMBOard), and scratch at $030f or $800f. The latter
exercises the RAMBOard high-RAM read/write decoder. Both force motor on,
density zone 3 and read mode. This is not the actual RAMBOard fastloader.
The card is blank; actual GCR data, disk writes, host handshakes, other opcode
mixes, UI/tape contention and real MMIO/cache latencies require further tests.

QEMU models instruction semantics, not A7 pipelines/cache/bus timing. Fixed
icount charges every instruction equally, including multi-register and VFP
instructions. Thus these scenarios cannot determine physical effective IPC.
A real instruction-per-cycle distribution and GPIO latency measurement would
be needed to calibrate the projection.

```sh
make RASPPI=2 TARGET=kernel-pi2 -j4
python3 tests/test_pi2_icount.py
```

The runner downloads the pinned upstream QEMU 6.2 plugin header to
`tests/.build/pi2-icount/`, compiles a host plugin and a debug-types object,
and writes JSON, per-loop CSVs and GDB logs to `build/pi2-icount/`. It rejects
other QEMU versions because it uses the internal 6.2 clock ABI. Firmware is
unchanged; the debug-types object supplies type descriptions only. The plugin
exits the disposable VM after the requested samples, so GDB's final “Remote
connection closed” message is expected; the runner verifies the CSV count
and VM exit status before accepting the run.

Sources: [QEMU icount model](https://www.qemu.org/docs/master/devel/tcg-icount.html),
[QEMU 6.2 plugin API](https://github.com/qemu/qemu/blob/v6.2.0/include/qemu/qemu-plugin.h).
