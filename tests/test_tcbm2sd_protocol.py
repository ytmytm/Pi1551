#!/usr/bin/env python3
"""Native protocol regression tests derived from tcbm2sd.ino and 6502 clients."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "tests" / ".build"
CLI = BUILD / "tcbm2sd-native"
DISKIMAGE_CLI = BUILD / "diskimage-native-probe"

# $0762-$07EF from the actual Turbo Outrun tcbmfast loader, starting
# immediately after TCBM_SendDOSCommand returns from sending U0.
TURBO_OUTRUN_FAST_CLIENT = """
a9 00 9d c3 fe 9d c2 fe bd c2 fe 30 fb bd c0 fe 85 9e bd c1 fe a8 a9 40 9d
c2 fe 98 29 03 f0 12 a9 40 9d c2 fe a9 ff 9d c3 fe ae f3 07 ac f3 07 38 60
bd c2 fe 10 fb bd c0 fe 85 9f bd c1 fe a8 a9 00 9d c2 fe 98 29 03 d0 d6 a0
00 bd c2 fe 30 fb bd c0 fe 91 9e c8 bd c1 fe 85 b7 a9 40 9d c2 fe a5 b7 29
03 d0 22 bd c2 fe 10 fb bd c0 fe 91 9e c8 bd c1 fe 85 b7 a9 00 9d c2 fe a5
b7 29 03 d0 07 98 d0 c7 e6 9f d0 c3 20 82 07 18 60
"""

# $9275-$92CA from the GEOS 1551 driver, immediately after its U0 block-read
# command returns. This is the actual 256-byte fast receive loop.
GEOS_FAST_BLOCK_READ_CLIENT = """
a9 00 8d f3 fe 8d f0 fe 8d f2 fe a8 ad f2 fe 30 fb ad f0 fe 91 0a c8 ae f1
fe a9 40 8d f2 fe 8a 29 03 d0 1b ad f2 fe 10 fb ad f0 fe 91 0a c8 ae f1 fe
a9 00 8d f2 fe 8a 29 03 d0 03 98 d0 cd ad f2 fe 10 fb a9 00 8d f0 fe a9 ff
8d f3 fe a9 40 8d f2 fe a2 00 60
"""


def build_cli():
    BUILD.mkdir(exist_ok=True)
    subprocess.run(
        [
            "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
            "-Wno-type-limits", "-O2", "-ffunction-sections", "-fdata-sections",
            "-Isrc", "-Iuspi/include", "tests/tcbm2sd_native.cpp",
            "tests/fatfs_posix.cpp", "src/tcbm2sd_protocol.cpp", "src/cbm_diskimage.cpp",
            "src/m6502.cpp",
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
    subprocess.run(
        [
            "gcc", "-O2", "-ffunction-sections", "-fdata-sections",
            "-Isrc", "-Iuspi/include", "-c", "src/lz.c",
            "-o", str(BUILD / "lz-native.o"),
        ],
        cwd=ROOT,
        check=True,
    )
    subprocess.run(
        [
            "g++", "-std=c++11", "-O2", "-ffunction-sections", "-fdata-sections",
            "-Isrc", "-Iuspi/include", "tests/diskimage_native_probe.cpp",
            "tests/fatfs_posix.cpp", "src/DiskImage.cpp", "src/gcr.cpp", "src/prot.cpp",
            str(BUILD / "lz-native.o"), "-Wl,--gc-sections", "-o", str(DISKIMAGE_CLI),
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

        # Turbo Outrun uses two-character lower-case PETSCII names such as
        # "mh". The Arduino reference normalises these to upper-case PETSCII.
        [result] = run_cli("u0 1 55 30 1f 6d 68")
        self.assertEqual(result, "type=filename track=0 sector=0 count=0 device=0 filename=MH")

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

    def test_emulation_only_intercepts_supported_read_side_u0_requests(self):
        filename, track_sector, block_read, block_write, set_device, unknown = run_cli(
            "emulation-u0 1 55 30 1f 47 41 4d 45",
            "emulation-u0 1 55 30 3f 12 00",
            "emulation-u0 1 55 30 00 12 00 01",
            "emulation-u0 1 55 30 02 12 00 01",
            "emulation-u0 1 55 30 3e 08",
            "emulation-u0 1 55 30 55",
        )
        self.assertEqual(filename, "intercept=1 type=filename")
        self.assertEqual(track_sector, "intercept=1 type=track-sector")
        self.assertEqual(block_read, "intercept=1 type=block-read")
        self.assertEqual(block_write, "intercept=1 type=block-write")
        self.assertEqual(set_device, "intercept=0 type=set-device")
        self.assertEqual(unknown, "intercept=0 type=invalid")

    def test_ui_uj_status_identifies_tcbm2sd_fast_protocol(self):
        [status] = run_cli("status73")
        self.assertEqual(status, "73,PI1551 V01.25 (TCBM2SD COMPAT),00,00\\r")
        self.assertIn("TCBM2SD", status)

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

    def test_callback_block_read_uses_current_decoded_sectors_and_crosses_tracks(self):
        [result] = run_cli("callback-block-read 17 20 2")
        self.assertEqual(
            result,
            "ok=1 bytes=512 reads=17/20,18/0 first=123 last=125",
        )

    def test_callback_block_write_updates_the_live_image(self):
        self.assertEqual(
            run_cli("callback-block-write"),
            ["ok=1 bytes=256"],
        )

    def test_fast_file_load_walks_directory_and_file_through_sector_callback(self):
        self.assertEqual(
            run_cli("callback-file-read"),
            ["ok=1 data=0108aa55"],
        )

    def test_live_diskimage_decodes_every_sector_and_accepts_coherent_writeback(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "live.d64"
            path.write_bytes(bytes((i * 29 + 7) & 0xff for i in range(174848)))
            decoded = subprocess.run(
                [str(DISKIMAGE_CLI), str(path)], cwd=ROOT,
                text=True, capture_output=True, check=True,
            )
            written = subprocess.run(
                [str(DISKIMAGE_CLI), str(path), "write"], cwd=ROOT,
                text=True, capture_output=True, check=True,
            )
            self.assertEqual(decoded.stdout.strip(), "ok=1 sectors=683 mismatches=0")
            self.assertEqual(written.stdout.strip(), "write-ok=1")

    def test_all_shipped_1551_roms_share_the_fast_talk_return_epilogue(self):
        roms = [
            (ROOT / "sdcard" / "dos1551.bin", 0xC000),
            (ROOT / "sdcard" / "super_dos_1551.rom", 0xC000),
            (ROOT / "sdcard" / "dos1551-ram.bin", 0x8000),
            (ROOT / "sdcard" / "super_dos_ram.bin", 0x8000),
        ]
        reference = None
        for path, load_address in roms:
            data = path.read_bytes()
            start = 0xC0D6 - load_address
            end = 0xC14E - load_address
            epilogue = data[start:end]
            self.assertEqual(
                epilogue[0xC143 - 0xC0D6:0xC14E - 0xC0D6],
                bytes.fromhex("ad024010fb29fc8d024060"),
                f"{path.name}: unexpected $C143 fast TALK return epilogue",
            )
            if reference is None:
                reference = epilogue
            else:
                self.assertEqual(epilogue, reference, path.name)

    def test_all_shipped_1551_roms_share_post_parser_u0_trap_point(self):
        roms = [
            (ROOT / "sdcard" / "dos1551.bin", 0xC000),
            (ROOT / "sdcard" / "super_dos_1551.rom", 0xC000),
            (ROOT / "sdcard" / "dos1551-ram.bin", 0x8000),
            (ROOT / "sdcard" / "super_dos_ram.bin", 0x8000),
        ]
        reference = None
        for path, load_address in roms:
            data = path.read_bytes()
            start = 0xC230 - load_address
            end = 0xC286 - load_address
            dispatcher = data[start:end]
            self.assertEqual(
                dispatcher[0xC24A - 0xC230:0xC24F - 0xC230],
                bytes.fromhex("209dc3b1a4"),
                f"{path.name}: unexpected $C24A command-parser return",
            )
            if reference is None:
                reference = dispatcher
            else:
                self.assertEqual(dispatcher, reference, path.name)

    def test_fast_talk_handoff_epilogue_unwinds_the_emulated_jsr(self):
        self.assertEqual(
            run_cli("handoff-unwind"),
            ["ok=1 sp-before=253 sp-after=253 pc=203"],
        )

    def test_actual_turbo_outrun_client_completes_full_duplex_fast_read(self):
        loader = " ".join(TURBO_OUTRUN_FAST_CLIENT.split())
        self.assertEqual(
            run_cli(f"client-fast-read {loader}"),
            ["ok=1 bytes=7 contention=0 ack=1 dav=1 pc=205"],
        )

    def test_actual_geos_client_completes_delayed_256_byte_block_read(self):
        loader = " ".join(GEOS_FAST_BLOCK_READ_CLIENT.split())
        self.assertEqual(
            run_cli(f"client-geos-block-read {loader}"),
            ["ok=1 bytes=256 contention=0 ack=1 dav=1 pc=20b"],
        )


if __name__ == "__main__":
    unittest.main()
