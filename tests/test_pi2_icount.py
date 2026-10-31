#!/usr/bin/env python3
"""Profile the unchanged Pi 2 kernel under QEMU 6.2 icount cost scenarios.

Downloads the matching upstream plugin API header into tests/.build if absent.
Results describe an isolated emulator core, not calibrated Cortex-A7 timing.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess
import sys
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "tests/.build/pi2-icount"


def markers():
    text = subprocess.check_output(["arm-none-eabi-objdump", "-d", "kernel-pi2.elf",
                                   "--disassemble=_Z11Emulate1551P11FileBrowser"], cwd=ROOT, text=True)
    instructions = []
    for line in text.splitlines():
        match = re.match(r"\s*([0-9a-f]+):\s+[0-9a-f]+\s+(.+)", line)
        if match:
            instructions.append((int(match[1], 16), match[2]))
    candidates = []
    for i in range(len(instructions) - 3):
        address, load = instructions[i]
        compare, branch = instructions[i + 1][1], instructions[i + 2][1]
        if (re.match(r"ldr\s+r3, \[r2, #4\]", load) and compare.startswith("cmp\tr3, r6")
                and re.match(r"beq\s+" + format(address, "x") + r"\b", branch)):
            candidates.append((address, instructions[i + 3][0]))
    if len(candidates) != 1:
        raise SystemExit("Cannot identify unique timer wait in this compiler's kernel; inspect disassembly")
    return candidates[0]


def percentile(values, fraction):
    return sorted(values)[min(len(values) - 1, int((len(values) - 1) * fraction))]


def summarize(path, name, shift):
    rows = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(path.open())]
    result = {"scenario": name, "shift": shift, "ns_per_instruction": 1 << shift, "loops": len(rows)}
    for key in ("work_insns", "wait_insns", "memory_accesses", "mmio_accesses", "work_ns", "period_ns"):
        values = [row[key] for row in rows]
        result[key] = {"min": min(values), "mean": statistics.mean(values),
                       "p99": percentile(values, .99), "max": max(values)}
    result["work_over_1us_percent"] = 100 * sum(r["work_ns"] > 1000 for r in rows) / len(rows)
    result["timer_gaps_over_1us_percent"] = 100 * sum(r["timer_delta_us"] > 1 for r in rows) / len(rows)
    result["timer_delta_us_max"] = max(r["timer_delta_us"] for r in rows)
    result["max_clock_count_difference_ns"] = max(abs(
        r["period_ns"] - (r["work_insns"] + r["wait_insns"]) * (1 << shift)) for r in rows)
    if result["max_clock_count_difference_ns"] > 32 * (1 << shift):
        raise SystemExit("Instruction counts and QEMU timer do not agree")
    result["effective_6502_mhz"] = 2000 / result["period_ns"]["mean"]
    # Three instructions finish the previous timer read/branch path. The
    # observed first-read latency equals (work_insns + 3) * 2**shift exactly.
    if any(r["work_ns"] != (r["work_insns"] + 3) * (1 << shift) for r in rows):
        raise SystemExit("Unexpected synchronization overhead; review marker locations")
    result["required_ipc_at_900mhz_mean"] = (result["work_insns"]["mean"] + 3) / 900
    result["required_ipc_at_900mhz_p99"] = (result["work_insns"]["p99"] + 3) / 900
    result["instruction_only_work_over_1us_percent"] = {
        str(mhz) + "mhz_ipc" + str(ipc):
        100 * sum(r["work_insns"] + 3 > mhz * ipc for r in rows) / len(rows)
        for mhz in (900, 1000, 1200) for ipc in (.5, .75, 1.0, 1.25)
    }
    if any(r["other_core_insns"] for r in rows):
        raise SystemExit("Other cores contaminated measurement")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--loops", type=int, default=50000)
    parser.add_argument("--output", type=Path, default=ROOT / "build/pi2-icount")
    args = parser.parse_args()
    if args.loops < 2000:
        parser.error("Use at least 2000 loops")
    version = subprocess.check_output(["qemu-system-arm", "--version"], text=True).splitlines()[0]
    if "version 6.2." not in version:
        parser.error("This plugin uses the QEMU 6.2 internal clock ABI; another version needs review")
    BUILD.mkdir(parents=True, exist_ok=True)
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    header = BUILD / "qemu-plugin.h"
    url = "https://raw.githubusercontent.com/qemu/qemu/v6.2.0/include/qemu/qemu-plugin.h"
    if not header.exists():
        header.write_bytes(urlopen(url, timeout=30).read())
    plugin = BUILD / "pi2_icount.so"
    drive = subprocess.check_output(["arm-none-eabi-objdump", "-d", "kernel-pi2.elf",
                                    "--disassemble=_ZN5Drive6UpdateEj"], cwd=ROOT, text=True)
    matches = re.findall(r"([0-9a-f]+):\s+[0-9a-f]+\s+ldrb\s+r3, \[r8, #24\]", drive)
    if len(matches) != 1:
        parser.error("Cannot identify active disk-update marker; inspect Drive::Update disassembly")
    subprocess.run(["gcc", "-shared", "-fPIC", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-DDISK_ACTIVE_PC=0x" + matches[0],
                    "-fvisibility=hidden", "-I" + str(BUILD), "tests/pi2_icount_plugin.c",
                    "-o", str(plugin)], cwd=ROOT, check=True)
    (BUILD / "types.cpp").write_text('#include "Pi1551.h"\nPi1551 *profile_type;\n')
    subprocess.run(["arm-none-eabi-g++", "-c", "-g", "-O0", "-fno-eliminate-unused-debug-types",
                    "-mcpu=cortex-a7", "-mfpu=neon-vfpv4", "-mfloat-abi=hard", "-DRPI2=1",
                    "-Isrc", "-Iuspi/include", str(BUILD / "types.cpp"), "-o", str(BUILD / "types.o")],
                   cwd=ROOT, check=True)
    wait, end = markers()
    report = {"qemu": version, "header_url": url,
              "kernel_sha256": hashlib.sha256((ROOT / "kernel-pi2.img").read_bytes()).hexdigest(),
              "wait_pc": hex(wait), "exit_pc": hex(end), "warmup_loops": 120000,
              "isolated_core": 1, "results": []}
    for name, rom, motor, motor_off in (("stock-idle", "stock", False, True),
                                      ("stock-rom", "stock", False, False),
                                      ("stock-read", "stock", True, False),
                                      ("ramboard-read", "ramboard", True, False)):
        for shift in (0, 1, 2):
            print("Profiling", name, "shift", shift, flush=True)
            csv_path = args.output / (name + "-shift" + str(shift) + ".csv")
            command = [sys.executable, "tests/test_pi2_qemu.py", "--timeout", "55", "--rom", rom,
                       "--icount", str(shift), "--profile", str(plugin), str(csv_path),
                       hex(wait), hex(end), str(args.loops)]
            if motor:
                command.append("--motor")
            if motor_off:
                command.append("--motor-off")
            with csv_path.with_suffix(".log").open("w") as log:
                subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
            result = summarize(csv_path, name, shift)
            rows = list(csv.DictReader(csv_path.open()))
            result["active_disk_updates"] = sum(int(row["disk_updates"]) for row in rows)
            if motor and result["active_disk_updates"] != args.loops:
                raise SystemExit("Motor workload did not run disk encoder once per microsecond")
            if motor_off and result["active_disk_updates"]:
                raise SystemExit("Motor-off workload unexpectedly ran disk encoder")
            report["results"].append(result)
            (args.output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
            print("  mean instructions %.1f, p99 %d, max %d; work >1us %.2f%%; %.3f MHz" % (
                result["work_insns"]["mean"], result["work_insns"]["p99"], result["work_insns"]["max"],
                result["work_over_1us_percent"], result["effective_6502_mhz"]), flush=True)
    print("Results:", args.output / "results.json")


if __name__ == "__main__":
    main()
