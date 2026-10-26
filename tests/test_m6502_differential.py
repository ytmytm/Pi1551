#!/usr/bin/env python3
"""Compare the production 6502 with the frozen core, including every bus cycle."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    env = dict(os.environ, UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    with tempfile.TemporaryDirectory(prefix="pi1551-cpu-") as directory:
        build = Path(directory)
        for optimization in ("-O1", "-Ofast"):
            flags = ["g++", "-std=c++11", optimization, "-g", "-Wall", "-Wextra", "-Werror",
                     "-Wno-unused-parameter", "-fsanitize=address,undefined",
                     "-fno-omit-frame-pointer", "-Isrc", "-Iuspi/include"]
            subprocess.run(flags + ["-DM6502=ReferenceM6502", "-DInterrupt=ReferenceInterrupt",
                                    "-c", "tests/reference/m6502.cpp", "-o", str(build / "reference.o")],
                           cwd=ROOT, check=True, env=env)
            subprocess.run(flags + ["tests/m6502_differential_native.cpp", "src/m6502.cpp",
                                    str(build / "reference.o"), "-o", str(build / "cpu-differential")],
                           cwd=ROOT, check=True, env=env)
            print("6502 differential test", optimization, flush=True)
            subprocess.run([str(build / "cpu-differential")], cwd=ROOT, check=True, env=env)


if __name__ == "__main__":
    main()
