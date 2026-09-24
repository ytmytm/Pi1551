#!/usr/bin/env python3
"""Native protocol regression tests derived from tcbm2sd.ino and 6502 clients."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "tests" / ".build"
CLI = BUILD / "tcbm2sd-native"


def build_cli():
    BUILD.mkdir(exist_ok=True)
    subprocess.run(
        [
            "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
            "-Wno-type-limits", "-O2", "-ffunction-sections", "-fdata-sections",
            "-Isrc", "-Iuspi/include", "tests/tcbm2sd_native.cpp",
            "tests/fatfs_posix.cpp", "src/tcbm2sd_protocol.cpp", "src/cbm_diskimage.cpp",
            "-Wl,--gc-sections", "-o", str(CLI),
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            "g++", "-std=c++11", "-DRPI3=1", "-Wno-type-limits",
            "-Wno-int-to-pointer-cast",
            "-Isrc", "-Iuspi/include", "-c", "src/tcbm_commands.cpp",
            "-o", str(BUILD / "tcbm_commands.o"),
        ],
        cwd=ROOT,
        check=True,
    )


def run_cli(*commands):
    result = subprocess.run(
        [str(CLI)], cwd=ROOT, input="\n".join(commands) + "\n",
        text=True, capture_output=True, check=True,
    )
    return result.stdout.splitlines()


class ProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build_cli()

    def test_fast_read_matches_arduino_and_6502_edge_order(self):
        [trace] = run_cli("read 01 08 aa")
        self.assertEqual(
            trace,
            "ack=1 wait-dav=0 dir=out"
            " | status=0 data=01 ack=0 wait-dav=1"
            " | status=0 data=08 ack=1 wait-dav=0"
            " | status=3 data=aa ack=0 wait-dav=1"
            " | final-status=3 dir=in ack=1 wait-dav=1 status=0",
        )

    def test_fast_write_waits_and_reads_before_acknowledging(self):
        [trace] = run_cli("write de ad be ef")
        self.assertEqual(
            trace,
            "ack=1 dir=in"
            " | wait-dav=0 read=de status=0 ack=0"
            " | wait-dav=1 read=ad status=0 ack=1"
            " | wait-dav=0 read=be status=0 ack=0"
            " | wait-dav=1 read=ef status=3 ack=1"
            " | final-status=3 dir=in ack=1 wait-dav=1 status=0",
        )
        self.assertNotIn("ack=0 wait-dav=0 read=de", trace)

    def test_u0_filename_mode_and_petscii_normalisation(self):
        [result] = run_cli("u0 0 55 30 5f 30 3a 67 61 6d 65 2e 70 72 67 0d")
        self.assertEqual(result, "type=filename track=0 sector=0 count=0 device=0 filename=GAME.PRG")

    def test_u0_track_sector_requires_mounted_image(self):
        outside, inside = run_cli("u0 0 55 30 3f 28 03", "u0 1 55 30 3f 28 03")
        self.assertTrue(outside.startswith("type=invalid"))
        self.assertEqual(inside, "type=track-sector track=40 sector=3 count=0 device=0 filename=")

    def test_u0_single_sector_read_and_write(self):
        read, write = run_cli("u0 1 55 30 00 28 03 01", "u0 1 55 30 02 28 03 01")
        self.assertEqual(read, "type=block-read track=40 sector=3 count=1 device=0 filename=")
        self.assertEqual(write, "type=block-write track=40 sector=3 count=1 device=0 filename=")

    def test_block_modes_are_exact_unlike_masked_fastload_modes(self):
        [result] = run_cli("u0 1 55 30 42 28 03 01")
        self.assertTrue(result.startswith("type=invalid"))

    def test_zero_length_block_transfer_is_rejected(self):
        [result] = run_cli("u0 1 55 30 00 28 03 00")
        self.assertTrue(result.startswith("type=invalid"))

    def test_special_secondary_starts_fast_file_and_fast_directory(self):
        file_result, dir_result, standard_result = run_cli(
            "talk 70 0 50 0", "talk 70 0 24 0", "talk 60 0 50 0",
        )
        self.assertEqual(file_result, "transfer=file fast=1")
        self.assertEqual(dir_result, "transfer=directory fast=1")
        self.assertEqual(standard_result, "transfer=file fast=0")

    def test_u0_pending_request_wins_on_fast_secondary(self):
        fast, standard = run_cli("talk 70 1 00 0", "talk 60 1 00 0")
        self.assertEqual(fast, "transfer=pending-u0 fast=1")
        self.assertEqual(standard, "transfer=directory fast=0")

    def test_u0_binary_device_number(self):
        [result] = run_cli("u0 0 55 30 3e 08")
        self.assertEqual(result, "type=set-device track=0 sector=0 count=0 device=8 filename=")

    def test_ui_uj_status_identifies_tcbm2sd_fast_protocol(self):
        [status] = run_cli("status73")
        self.assertEqual(status, "73,PI1551 V01.25 (TCBM2SD COMPAT),00,00\\r")
        self.assertIn("TCBM2SD", status)

    def test_d64_idle_sleep_requires_stock_rom_wait_and_idle_drive(self):
        [result] = run_cli("idle-sleep")
        self.assertEqual(
            result,
            "ready=1 disabled=0 non-d64=0 wrong-pc=0 motor=0 swap=0 job=0 pending=0 channel=0 talk=0 listen=0 irq=0"
            " wake-none=0 wake-data=1 wake-dav=1 wake-reset=1 wake-ui=1"
            " cmd-idle=0 cmd-81=1 bus-same=0 bus-dav=1",
        )

    def test_all_shipped_1551_roms_share_idle_tpi_wait_loop(self):
        signatures = (
            # Primary TALK/LISTEN state in $5b/$5c, including UNTALK $5f.
            (0xC0A8, bytes.fromhex(
                "865b98c95ff00d855b865ca920857c857d4c33c1a900f07598c93fd00a"
                "a9ff8595a200865cf0ed855c865b4cb3c0"
            )),
            (0xEABD, bytes.fromhex(
                "e661d00320dceaad004010032022c0ad5502f0ec"
            )),
            # LEADC scans 15 channel markers: $022b + X, X=$0e..$00.
            (0xEADC, bytes.fromhex(
                "a90e8563a9008562a663bd2b02c9fff002e662c66310f1"
            )),
        )
        roms = [
            (ROOT / "sdcard" / "dos1551.bin", 0xC000),
            (ROOT / "sdcard" / "super_dos_1551.rom", 0xC000),
            (ROOT / "sdcard" / "dos1551-ram.bin", 0x8000),
            (ROOT / "sdcard" / "super_dos_ram.bin", 0x8000),
        ]
        for path, load_address in roms:
            data = path.read_bytes()
            for address, signature in signatures:
                start = address - load_address
                self.assertEqual(data[start:start + len(signature)], signature, path.name)

    def test_reference_image_geometries_and_side_boundaries(self):
        results = run_cli(
            "geometry d71 36 0", "geometry d71 70 16",
            "geometry d81 80 39", "geometry d80 77 22",
            "geometry d82 78 0", "geometry d82 154 22",
        )
        self.assertEqual(results[0], "valid=1 tracks=70 sectors=21 block=683 bytes=349696")
        self.assertEqual(results[1], "valid=1 tracks=70 sectors=17 block=1365 bytes=349696")
        self.assertEqual(results[2], "valid=1 tracks=80 sectors=40 block=3199 bytes=819200")
        self.assertEqual(results[3], "valid=1 tracks=77 sectors=23 block=2082 bytes=533248")
        self.assertEqual(results[4], "valid=1 tracks=154 sectors=29 block=2083 bytes=1066496")
        self.assertEqual(results[5], "valid=1 tracks=154 sectors=23 block=4165 bytes=1066496")

    def test_invalid_tracks_and_sectors_are_rejected(self):
        results = run_cli(
            "geometry d71 0 0", "geometry d71 71 0",
            "geometry d81 1 40", "geometry d80 77 23", "geometry d82 154 23",
        )
        for result in results:
            self.assertTrue(result.startswith("valid=0"), result)

    def test_single_sector_write_and_read_in_all_requested_image_types(self):
        image_sizes = {
            "d71": (349696, 36, 0),
            "d81": (819200, 40, 3),
            "d80": (533248, 77, 22),
            "d82": (1066496, 78, 0),
        }
        with tempfile.TemporaryDirectory() as directory:
            commands = []
            for name, (size, track, sector) in image_sizes.items():
                path = Path(directory) / f"test.{name}"
                with path.open("wb") as image:
                    image.truncate(size)
                commands.append(f"block-roundtrip {path} {track} {sector}")
            self.assertEqual(run_cli(*commands), ["ok=1 bytes=256"] * len(commands))

    def test_block_io_rejects_sector_outside_image_geometry(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.d81"
            with path.open("wb") as image:
                image.truncate(819200)
            self.assertEqual(run_cli(f"block-roundtrip {path} 80 40"), ["ok=0 bytes=0"])


if __name__ == "__main__":
    unittest.main()
