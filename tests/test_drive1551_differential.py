#!/usr/bin/env python3
"""Build both 1551 drive versions and compare complete host-side update traces."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_D64 = (Path.home() / "Maciejdev/plus4/kicad-tcbm2sd/"
               "sdcard-tcbm2sd-pi1551/1551/demos/Carrion's-121-colors.d64")


def main():
    with tempfile.TemporaryDirectory(prefix="pi1551-drive-") as directory:
        build = Path(directory)
        subprocess.run(["gcc", "-O1", "-g", "-fsanitize=address,undefined", "-c",
                        "src/lz.c", "-o", str(build / "lz.o")], cwd=ROOT, check=True)
        for optimization in ("-O1", "-Ofast"):
            flags = ["g++", "-std=c++11", optimization, "-g", "-Wall", "-Wextra",
                     "-Wno-unused-variable", "-Wno-unused-but-set-variable", "-Wno-empty-body",
                     "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                     "-ffunction-sections", "-fdata-sections", "-DRPI3=1",
                     "-Wno-int-to-pointer-cast", "-Wno-type-limits",
                     "-Wno-unused-parameter", "-Isrc", "-Iuspi/include"]
            subprocess.run(flags + ["-c", "src/Drive1551.cpp", "-o", str(build / "current.o")],
                           cwd=ROOT, check=True)
            subprocess.run(flags + ["-DDrive=ReferenceDrive", "-c", "tests/reference/Drive1551.cpp",
                                    "-o", str(build / "reference.o")], cwd=ROOT, check=True)
            subprocess.run(flags + ["tests/pi1551_memory_native.cpp", "src/Pi1551.cpp",
                                    "src/m6523.cpp", "src/options.cpp", str(build / "current.o"),
                                    "-Wl,--gc-sections", "-o", str(build / "memory-differential")],
                           cwd=ROOT, check=True)
            subprocess.run([str(build / "memory-differential")], cwd=ROOT, check=True)
            subprocess.run(flags + ["tests/drive1551_differential_native.cpp", "tests/fatfs_posix.cpp",
                                    "src/DiskImage.cpp", "src/gcr.cpp", "src/prot.cpp", "src/m6523.cpp",
                                    str(build / "current.o"), str(build / "reference.o"),
                                    str(build / "lz.o"), "-Wl,--gc-sections", "-Wl,--wrap=rand",
                                    "-o", str(build / "drive-differential")], cwd=ROOT, check=True)
            print("Differential test", optimization, flush=True)
            args = [str(build / "drive-differential")]
            if optimization == "-Ofast":
                real_d64 = Path(os.environ.get("PI1551_TEST_D64", DEFAULT_D64)).expanduser()
                if real_d64.is_file():
                    args += [str(real_d64), str(build / "roundtrip.d64")]
                else:
                    print("Real D64 unavailable:", real_d64, flush=True)
            subprocess.run(args, cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
