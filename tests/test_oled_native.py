#!/usr/bin/env python3
"""Compile production OLED/TAP components and run them without Raspberry Pi."""
from pathlib import Path
import os
import struct
import sys
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def run():
    os.environ["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    subprocess.run([sys.executable, "-B", "tests/test_oled_tui.py"], cwd=ROOT, check=True)
    with tempfile.TemporaryDirectory(prefix="pi1551-oled-") as directory:
        build = Path(directory)
        subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                        "-Wno-int-to-pointer-cast", "-Wno-unused-parameter",
                        "-fshort-enums", "-ffunction-sections", "-fdata-sections",
                        "-Isrc", "-Iuspi/include", "tests/gpio_value_native.c",
                        "src/rpi-gpio.c", "-Wl,--gc-sections", "-o", str(build / "gpio-value")], cwd=ROOT, check=True)
        subprocess.run([str(build / "gpio-value")], cwd=ROOT, check=True)
        flags = ["g++", "-std=c++11", "-Wall", "-Wextra", "-g", "-O1",
                 "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                 "-ffunction-sections", "-fdata-sections", "-Isrc", "-Iuspi/include"]
        subprocess.run(flags + ["-Werror", "-Wno-unused-parameter", "tests/m6502_resume_native.cpp", "src/m6502.cpp",
                               "-o", str(build / "m6502-resume")], cwd=ROOT, check=True)
        subprocess.run([str(build / "m6502-resume")], cwd=ROOT, check=True)
        subprocess.run(flags + ["-O0", "-DPI1551_NATIVE_TEST", "-DRPI3=1", "-Wno-int-to-pointer-cast",
                               "-Wno-type-limits", "-Wno-unused-parameter", "-Wno-reorder", "-fno-sanitize=vptr",
                               "tests/fatfs_diskio_native.cpp", "src/ff.cpp", "src/diskio.cpp",
                               "src/Pi1551Oled.cpp", "src/OledMenu.cpp", "src/OledSettings.cpp",
                               "src/options.cpp", "src/tape_player.cpp", "-Wl,--gc-sections",
                               "-o", str(build / "fatfs-storage")], cwd=ROOT, check=True)
        subprocess.run([str(build / "fatfs-storage")], cwd=ROOT, check=True)
        subprocess.run(flags + ["-Werror", "tests/oled_menu_native.cpp", "src/OledMenu.cpp",
                               "src/OledSettings.cpp", "-o", str(build / "menu")], cwd=ROOT, check=True)
        subprocess.run([str(build / "menu")], cwd=ROOT, check=True)
        subprocess.run(flags + ["tests/sound_native.cpp", "-o", str(build / "sound")], cwd=ROOT, check=True)
        subprocess.run([str(build / "sound")], cwd=ROOT, check=True)
        subprocess.run(flags + ["-DPI1551_NATIVE_TEST", "-DPI1551_NATIVE_GPIO_IRQ_TEST", "-DRPI3=1", "-Wno-int-to-pointer-cast",
                               "-Wno-type-limits", "-Wno-unused-parameter", "tests/tape_native.cpp",
                               "tests/fatfs_posix.cpp", "src/tape_player.cpp", "src/options.cpp", "src/OledMenu.cpp", "src/OledSettings.cpp",
                               "-Wl,--gc-sections", "-o", str(build / "tape")], cwd=ROOT, check=True)
        payload = (b"\0" + (11084 * 8).to_bytes(3,"little")) * 10000
        fixture = build / "seek.tap"
        fixture.write_bytes(b"C16-TAPE-RAW" + bytes([2,2,0,0]) + struct.pack("<I",len(payload)) + payload)
        subprocess.run([str(build / "tape"), str(fixture)], cwd=ROOT, check=True)
        v1_payload = bytes([5, 0, 0x20, 0x03, 0, 7, 9])
        v1_fixture = build / "seek-v1.tap"
        v1_fixture.write_bytes(b"C16-TAPE-RAW" + bytes([1,2,0,0]) + struct.pack("<I",len(v1_payload)) + v1_payload)
        subprocess.run([str(build / "tape"), str(v1_fixture), "v1idx"], cwd=ROOT, check=True)
        v1_record = build / "record-v1.tap"
        v1_record.write_bytes(v1_fixture.read_bytes())
        (build / "record-v1.idx").write_text("0x14 EARLY\n0x15 MIDDLE\n0x19 LATE\n0x1A REPLACED\n")
        subprocess.run([str(build / "tape"), str(v1_record), "v1record"], cwd=ROOT, check=True)
        gap_payload = (b"\0" + (11084 * 8).to_bytes(3,"little")) * 1845
        gap_payload += b"\0" + (1000000 * 8).to_bytes(3,"little")
        gap_fixture = build / "seek-gap-67.tap"
        gap_fixture.write_bytes(b"C16-TAPE-RAW" + bytes([2,2,0,0]) + struct.pack("<I",len(gap_payload)) + gap_payload)
        subprocess.run([str(build / "tape"), str(gap_fixture), "gap67"], cwd=ROOT, check=True)
        long_payload = (b"\0" + (0xFFFFFF).to_bytes(3, "little")) * 350
        long_fixture = build / "long.tap"
        long_fixture.write_bytes(b"C16-TAPE-RAW" + bytes([2,2,0,0]) + struct.pack("<I",len(long_payload)) + long_payload)
        subprocess.run([str(build / "tape"), str(long_fixture), "long"], cwd=ROOT, check=True)
        subprocess.run(["gcc", "-O1", "-g", "-fsanitize=address,undefined", "-ffunction-sections",
                        "-fdata-sections", "-c", "src/lz.c", "-o", str(build / "lz.o")], cwd=ROOT, check=True)
        subprocess.run(flags + ["-DRPI3=1", "-Wno-int-to-pointer-cast", "-Wno-type-limits",
                               "-Wno-unused-parameter", "-Wno-unused-variable",
                               "tests/disk_native.cpp", "tests/fatfs_posix.cpp", "src/DiskImage.cpp",
                               "src/gcr.cpp", "src/prot.cpp", "src/cbm_diskimage.cpp", str(build / "lz.o"), "-Wl,--gc-sections",
                               "-o", str(build / "disk")], cwd=ROOT, check=True)
        subprocess.run([str(build / "disk"), str(build / "write.d64")], cwd=ROOT, check=True)
        subprocess.run(["gcc", "-c", "src/xga_font_data.c", "-o", str(build / "font.o")], cwd=ROOT, check=True)
        subprocess.run(flags + ["-DRPI3=1", "-Wno-int-to-pointer-cast", "tests/oled_display_native.cpp",
                               "src/SSD1306.cpp", str(build / "font.o"), "-o", str(build / "display")], cwd=ROOT, check=True)
        subprocess.run([str(build / "display")], cwd=ROOT, check=True)
        subprocess.run(flags + ["-DRPI3=1", "-Wno-int-to-pointer-cast", "-Wno-type-limits",
                               "-Wno-unused-parameter", "-Wno-reorder", "-Wno-unused-variable",
                               "-fno-sanitize=vptr", "tests/host_files_native.cpp", "tests/fatfs_posix.cpp",
                               "src/commands_base.cpp", "-Wl,--gc-sections", "-o", str(build / "host-files")], cwd=ROOT, check=True)
        subprocess.run([str(build / "host-files"), str(build / "host-save.prg")], cwd=ROOT, check=True)
        subprocess.run(flags + ["-DPI1551_NATIVE_TEST", "-DRPI3=1", "-Wno-int-to-pointer-cast",
                               "-Wno-type-limits", "-Wno-unused-parameter", "-Wno-reorder",
                               "-fno-sanitize=vptr", "tests/oled_kernel_native.cpp", "tests/fatfs_posix.cpp",
                               "src/OledMenu.cpp", "src/OledSettings.cpp", "src/Pi1551Oled.cpp",
                               "src/tape_player.cpp", "src/options.cpp", "src/ScreenLCD.cpp", "src/SSD1306.cpp",
                               str(build / "font.o"), "-Wl,--gc-sections",
                               "-o", str(build / "bridge")], cwd=ROOT, check=True)
        card = build / "sd"
        (card / "1551").mkdir(parents=True)
        (card / "1551" / "sub" / "deep").mkdir(parents=True)
        (card / "1551" / "a.d64").write_bytes(bytes(174848))
        (card / "1551" / "b.d64").write_bytes(bytes(174848))
        (card / "1551" / "bad.d64").write_bytes(b"broken")
        (card / "1551" / "a.d81").write_bytes(bytes(819200))
        (card / "1551" / "a.prg").write_bytes(b"\x01\x10hello")
        (card / "1551" / "a.tap").write_bytes(fixture.read_bytes())
        (card / "1551" / "list.lst").write_text("a.d64\na.d81\na.prg\na.d64\n")
        (card / "1551" / "pair.lst").write_text("a.d64\nb.d64\n")
        (card / "options.txt").write_text("deviceID = 8\nROM2 = do-not-touch.bin\nunknown = keep\n")
        subprocess.run([str(build / "bridge"), str(card)], cwd=ROOT, check=True)
        subprocess.run(flags + ["-O0", "-DRPI3=1", "-Wno-int-to-pointer-cast", "-Wno-reorder",
                               "-Wno-type-limits", "-Wno-unused-parameter", "-fno-sanitize=vptr",
                               "tests/host_cd_native.cpp", "tests/fatfs_posix.cpp", "src/commands_base.cpp",
                               "src/cbm_diskimage.cpp", "src/tcbm2sd_protocol.cpp", "src/DiskImage.cpp",
                               "src/gcr.cpp", "src/prot.cpp", str(build / "lz.o"),
                               "-Wl,--gc-sections", "-o", str(build / "host-cd")], cwd=ROOT, check=True)
        subprocess.run([str(build / "host-cd"), str(card)], cwd=ROOT, check=True)

if __name__ == "__main__":
    run()
