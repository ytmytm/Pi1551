#!/usr/bin/env python3
"""Compare actual GPIO output code with its frozen reference on the host."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    env = dict(os.environ, UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    with tempfile.TemporaryDirectory(prefix="pi1551-gpio-") as directory:
        for optimization in ("-O1", "-Ofast"):
            executable = str(Path(directory) / "gpio-differential")
            flags = ["g++", "-std=c++11", optimization, "-g", "-Wall", "-Wextra",
                     "-Wno-unused-parameter", "-Wno-int-to-pointer-cast",
                     "-Wno-unused-variable", "-Wno-type-limits", "-Wno-empty-body",
                     "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                     "-ffunction-sections", "-fdata-sections", "-DRPI3=1",
                     "-DPI1551_NATIVE_TCBM_TEST", "-Isrc", "-Iuspi/include"]
            subprocess.run(flags + ["tests/tcbm_gpio_differential_native.cpp",
                                    "src/tcbm_bus.cpp", "src/m6523.cpp", "src/options.cpp",
                                    "-Wl,--gc-sections", "-o", executable],
                           cwd=ROOT, check=True, env=env)
            print("GPIO differential test", optimization, flush=True)
            subprocess.run([executable], cwd=ROOT, check=True, env=env)


if __name__ == "__main__":
    main()
