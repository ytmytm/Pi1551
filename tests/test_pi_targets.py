#!/usr/bin/env python3
"""Compare the actual preprocessed firmware for Pi 2 and Pi 3.

Only cache setup, PMU diagnostics and Pi 3 virtual GPIO/touch helpers may differ.
This catches features accidentally hidden behind a platform conditional.
"""
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PI3_DECLARATIONS = (
    "extern void RPI_TouchInit(void);",
    "extern void RPI_UpdateTouch(void);",
    "extern uint32_t RPI_GpioVirtGetAddress(void);",
    "extern void RPI_GpioVirtInit(void);",
    "extern void RPI_GpioVirtSetLed(int value);",
)


def preprocess(pi):
    plan = subprocess.check_output(["make", "-Bn", "V=1", "RASPPI=" + str(pi)],
                                   cwd=ROOT, text=True)
    sources = {}
    for line in plan.splitlines():
        if not line.startswith("arm-none-eabi-") or " -c " not in line:
            continue
        cmd = shlex.split(line)
        source = cmd[-1]
        if not source.startswith(("src/", "lib/")):
            continue
        index = cmd.index("-o")
        del cmd[index:index + 2]
        cmd[cmd.index("-c")] = "-E"
        cmd = [arg for arg in cmd if arg not in ("-MMD", "-MP")]
        cmd.append("-P")
        cwd = ROOT if source.startswith("src/") else ROOT / "uspi"
        key = source if cwd == ROOT else "uspi/" + source
        data = subprocess.check_output(cmd, cwd=cwd).decode("latin1")
        # These declarations leak through rpi-gpio.h into many unrelated units.
        for declaration in PI3_DECLARATIONS:
            data = data.replace(declaration + "\n", "")
        if key == "src/main.cpp":
            data = data.replace(" RPI_GpioVirtInit();\n", "").replace(" RPI_TouchInit();\n", "")
        sources[key] = data
    return sources


def main():
    pi2, pi3 = preprocess(2), preprocess(3)
    if set(pi2) != set(pi3) or len(pi2) < 49:
        raise SystemExit("FAIL platform source lists differ or are incomplete")
    allowed = {"src/cache.c", "src/performance.c", "src/rpi-gpio.c"}
    changed = {name for name in pi2 if pi2[name] != pi3[name]}
    if changed != allowed:
        raise SystemExit("FAIL unexpected preprocessor differences: " + repr(changed ^ allowed))
    for source in ("src/main.cpp", "src/armc-start.S"):
        if "_init_core" not in pi2[source]:
            raise SystemExit("FAIL Pi 2 multicore startup disappeared")
    for pi in (2, 3):
        elf = ROOT / ("kernel-pi" + str(pi) + ".elf")
        attributes = subprocess.check_output(["arm-none-eabi-readelf", "-A", str(elf)], text=True)
        arch = "Tag_CPU_arch: v7\n" if pi == 2 else "Tag_CPU_arch: v8\n"
        if arch not in attributes:
            raise SystemExit("FAIL unexpected kernel ISA: " + str(elf))
    def symbols(pi):
        data = subprocess.check_output(["arm-none-eabi-nm", "-C", "kernel-pi" + str(pi) + ".elf"],
                                       cwd=ROOT, text=True)
        return {line.split(" T ", 1)[1] for line in data.splitlines() if " T " in line}
    sym2, sym3 = symbols(2), symbols(3)
    expected = {"RPI_TouchInit", "RPI_UpdateTouch", "RPI_GpioVirtGetAddress",
                "RPI_GpioVirtInit", "RPI_GpioVirtSetLed"}
    if sym3 - sym2 != expected or sym2 - sym3:
        raise SystemExit("FAIL unexpected exported function difference: " + repr(sym2 ^ sym3))
    print("PASS", len(pi2), "firmware/USB source units: shared features match after excluding")
    print("     Pi 3 virtual GPIO/touch; only cache setup and PMU diagnostics differ")
    print("PASS ARMv7 / ARMv8 ELF attributes and exported functions; multicore retained")


if __name__ == "__main__":
    main()
