#!/usr/bin/env python3
"""Four-way Plus/4 TAP test using the Parobek headless VICE and Pi1551 GPIO code.

Run explicitly with --vice-root if the Parobek checkout is elsewhere. Each
VICE load starts a fresh machine, fills the PRG memory range with zero, and
compares the bytes loaded from tape with tests/vice/hello.prg.
"""
from __future__ import annotations

import argparse
import importlib.util
import struct
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read_runner(vice_root: Path):
    source = vice_root / "tests/vice/run-hackjunk-headless.py"
    spec = importlib.util.spec_from_file_location("parobek_vice_runner", source)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot import {source}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def tap_header(path: Path, version: int) -> None:
    data = path.read_bytes()
    assert len(data) > 5000, f"TAP too short: {path}"
    assert data[:12] == b"C16-TAPE-RAW" and data[12:14] == bytes((version, 2)), path
    assert struct.unpack_from("<I", data, 16)[0] == len(data) - 20, path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--vice-root", type=Path,
        default=Path("~/Maciejdev/plus4/burstcart/parobek/plus4-burstcart").expanduser(),
    )
    args = parser.parse_args()
    vice_root = args.vice_root.resolve()
    runner = read_runner(vice_root)
    cfg = runner.load_rom_env(vice_root / "tests/vice/roms.env")
    xplus4 = runner.required_file("XPLUS4_TEST", str(vice_root / "vice/build-hackjunk/src/xplus4"))
    kernal = runner.required_file("HOST_KERNAL_STOCK", cfg.get("HOST_KERNAL_STOCK"))
    basic = runner.required_file("HOST_BASIC_STOCK", cfg.get("HOST_BASIC_STOCK"))
    drive_rom = runner.required_file("DRIVE_1551_STOCK", cfg.get("DRIVE_1551_STOCK"))
    disk = runner.required_file("DISK_IMAGE", str(vice_root / "tests/vice/smoke-test.d64"))
    reference = runner.required_file("REFERENCE_PRG", str(vice_root / "tests/vice/hello.prg"))
    prg = reference.read_bytes()
    assert len(prg) >= 3
    start = int.from_bytes(prg[:2], "little")
    expected = prg[2:]
    end = start + len(expected) - 1

    with tempfile.TemporaryDirectory(prefix="pi1551-vice-tape-") as temp:
        work = Path(temp)
        native = work / "tape-interop"
        build = [
            "g++", "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror", "-pthread",
            "-DPI1551_NATIVE_TEST", "-DPI1551_NATIVE_GPIO_IRQ_TEST", "-DRPI3=1", "-Wno-int-to-pointer-cast",
            "-Wno-type-limits", "-Wno-unused-parameter", "-Isrc", "-Iuspi/include",
            "-ffunction-sections", "-fdata-sections", "tests/tape_interop_native.cpp",
            "tests/fatfs_posix.cpp", "src/tape_player.cpp", "src/options.cpp",
            "-Wl,--gc-sections", "-o", str(native),
        ]
        subprocess.run(build, cwd=ROOT, check=True)

        def start_vice(name: str, image: Path, *, recording: bool):
            port = runner.free_port()
            command = [
                str(xplus4), "-default", "+hackjunk6510", "+sound", "-warp", "-console",
                "-kernal", str(kernal), "-basic", str(basic),
                "-dos1551", str(drive_rom), "-drive8type", "1551",
                "-drive8truedrive", "-drive9type", "0",
            ]
            if recording:
                command += ["-8", str(disk)]
            command += [
                "-1", str(image), "-remotemonitor", "-remotemonitoraddress",
                f"ip4://127.0.0.1:{port}",
            ]
            log = (work / f"{name}.log").open("wb")
            process = subprocess.Popen(command, cwd=vice_root, stdout=log, stderr=subprocess.STDOUT)
            return port, process, log

        def close_vice(port: int, process, log) -> None:
            try:
                if process.poll() is None:
                    runner.monitor_query(port, ["quit"], timeout=5)
                    process.wait(timeout=5)
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=5)
                log.close()
            assert process.returncode == 0, f"VICE exited {process.returncode}"

        vice_tap = work / "vice-recorded-v1.tap"
        vice_tap.write_bytes(b"C16-TAPE-RAW" + bytes((1, 2, 0, 0)) + struct.pack("<I", 4) + bytes(4))
        port, process, log = start_vice("vice-save", vice_tap, recording=True)
        try:
            runner.monitor_query(port, ["screen", "x"], timeout=10)
            runner.monitor_query(
                port, ["tapectrl 4", 'keybuf load"hello",8\\nsave"test",1\\n', "x"], timeout=10,
            )
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                screen = runner.monitor_query(port, ["screen", "x"], timeout=10).lower()
                saved = screen.find("saving test")
                if saved >= 0 and screen.find("ready.", saved) > saved:
                    break
                time.sleep(0.1)
            else:
                raise AssertionError("VICE SAVE did not finish")
            # The emulated datasette continues draining its final pulse buffer
            # briefly after BASIC has printed READY. Let the motor settle before
            # pressing STOP, or the TAP can contain only the first copy.
            time.sleep(3)
            runner.monitor_query(port, ["tapectrl 0", "x"], timeout=10)
        finally:
            close_vice(port, process, log)
        tap_header(vice_tap, 1)
        print("VICE saved the reference program to v1 C16 TAP")

        def native_run(mode: str, source: Path, target: Path) -> None:
            subprocess.run([str(native), mode, str(source), str(target)], cwd=ROOT, check=True)
            tap_header(target, 2)

        def load_in_vice(label: str, image: Path) -> None:
            dump = work / f"{label}.bin"
            port, process, log = start_vice(label, image, recording=False)
            try:
                runner.monitor_query(
                    port,
                    [
                        f"fill ${start:04x} ${end:04x} $00",
                        f'bsave "{dump}" 0 ${start:04x} ${end:04x}',
                        "tapectrl 1", 'keybuf load"test",1\\n', "x",
                    ],
                    timeout=10,
                )
                assert dump.read_bytes() == bytes(len(expected)), "PRG memory was not cleared"
                deadline = time.monotonic() + 45
                while time.monotonic() < deadline:
                    runner.monitor_query(
                        port, [f'bsave "{dump}" 0 ${start:04x} ${end:04x}', "x"], timeout=10,
                    )
                    if dump.read_bytes() == expected:
                        print(f"PASS {label}: {len(expected)} PRG bytes matched after clear")
                        break
                    time.sleep(0.1)
                else:
                    raise AssertionError(f"{label}: VICE did not load the reference PRG")
            finally:
                close_vice(port, process, log)

        vice_through_pi = work / "vice-through-pi.tap"
        native_run("play", vice_tap, vice_through_pi)
        load_in_vice("VICE to Pi1551", vice_through_pi)

        pi_tap = work / "pi-recorded.tap"
        native_run("record", vice_tap, pi_tap)
        load_in_vice("Pi1551 to VICE", pi_tap)

        pi_through_pi = work / "pi-through-pi.tap"
        native_run("play", pi_tap, pi_through_pi)
        load_in_vice("Pi1551 to Pi1551", pi_through_pi)

        load_in_vice("VICE to VICE control", vice_tap)


if __name__ == "__main__":
    main()
