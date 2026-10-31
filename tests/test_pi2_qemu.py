#!/usr/bin/env python3
"""Boot the actual Pi 2 kernel on QEMU's four Cortex-A7 CPUs.

This is a boot/ISA smoke test, never a physical TCBM timing measurement.
"""
import argparse
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile

from fat16_fixture import write_card

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel", default="kernel-pi2")
    parser.add_argument("--timeout", type=int, default=45)
    parser.add_argument("--rom", choices=("stock", "ramboard"), default="ramboard")
    args = parser.parse_args()
    kernel = (ROOT / args.kernel).resolve()
    for tool in ("qemu-system-arm", "gdb-multiarch"):
        if not shutil.which(tool):
            parser.error("Missing executable: " + tool)
    for suffix in (".elf", ".img"):
        if not kernel.with_suffix(suffix).is_file():
            parser.error("Build first: make RASPPI=2 TARGET=" + args.kernel)

    # Keep the disposable card on local storage: NFS may not support QEMU locks.
    with tempfile.TemporaryDirectory(prefix="pi1551-pi2-qemu-", dir="/tmp") as directory:
        work = Path(directory)
        options = (("ROM1=" + ("RAM1551.BIN" if args.rom == "ramboard" else "DOS1551.BIN") + "\n") +
                   "ROM2=RAM1551.BIN\nRAMBOard=" + ("1" if args.rom == "ramboard" else "0") + "\n" +
                   "LCDName=none\ni2cScan=0\ni2cRtcAddress=0\n"
                   "SoundEnabled=0\nSoundOnGPIO=1\nShowOptions=0\n"
                   "IgnoreReset=1\nIdleSleep=0\n").replace("=", " = ").encode()
        d64 = bytearray(174848)
        d64[0x16500:0x16503] = bytes([18, 1, 0x41])
        d64[0x16601] = 0xff
        write_card(work / "card.img", {
            "OPTIONS.TXT": options,
            "DOS1551.BIN": (ROOT / "sdcard/dos1551.bin").read_bytes(),
            "RAM1551.BIN": (ROOT / "sdcard/dos1551-ram.bin").read_bytes(),
        }, {"TEST.D64": d64})
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        # -kernel supplies QEMU's boot/mailbox stubs for the secondary CPUs.
        # The second copy supplies the firmware's linked address (config.txt's
        # kernel_address). The low copy's first vector jumps to that address.
        command = ["qemu-system-arm", "-M", "raspi2b", "-kernel", str(kernel.with_suffix(".img")),
                   "-device", "loader,file=" + str(kernel.with_suffix(".img")) +
                   ",addr=0x1f00000,force-raw=on",
                   "-drive", "file=" + str(work / "card.img") + ",format=raw,if=sd,snapshot=on",
                   "-display", "none", "-serial", "null", "-serial", "file:" + str(work / "uart.log"),
                   "-S", "-gdb", "tcp:127.0.0.1:" + str(port)]
        script = work / "boot.gdb"
        script.write_text("""set pagination off
set confirm off
set breakpoint pending off
target remote :PORT
hbreak undefined_instruction_handler
commands
  silent
  echo FAIL undefined instruction\n
  thread apply all bt 4
  quit 1
end
hbreak prefetch_abort_handler
commands
  silent
  echo FAIL prefetch abort\n
  thread apply all bt 4
  quit 1
end
hbreak data_abort_handler
commands
  silent
  echo FAIL data abort\n
  thread apply all bt 4
  quit 1
end
hbreak UpdateScreen
continue
printf "PASS core 0 reached UI, SP=%#x\\n", $sp
delete 4
hbreak emulator
hbreak 'TapePlayer::CoreLoop()'
continue
info threads
printf "WORKER core=%d SP=%#x\\n", $_thread - 1, $sp
if $_thread == 2
  delete 5
else
  delete 6
end
continue
info threads
printf "WORKER core=%d SP=%#x\\n", $_thread - 1, $sp
if $_thread == 2
  delete 5
else
  delete 6
end
hbreak 'TCBM_Commands::SimulateIECUpdate()'
continue
printf "PASS browser TCBM poll, SP=%#x\\n", $sp
info threads
delete 7
set {char[15]} 0x100000 = "/1551/TEST.D64"
call ((void (*)(const char*))'Pi1551Oled::HostSelection(char const*)')(0x100000)
hbreak 'Pi1551Oled::BeginLiveRender()'
continue
printf "PASS mounted D64 and completed 1551 ROM fast boot\\n"
delete 8
hbreak read6502_1551
ignore 9 1000
continue
printf "PASS 1001 ARMv7 CPU reads after entering real-time emulation\\n"
info threads
detach
quit 0
""".replace(":PORT", ":" + str(port)))
        with (work / "qemu.log").open("w") as log:
            process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=log)
            gdb = ["gdb-multiarch", "-q", "-batch", str(kernel.with_suffix(".elf"))]
            try:
                try:
                    result = subprocess.run(gdb + ["-x", str(script)], cwd=ROOT, text=True,
                                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                            timeout=args.timeout)
                except subprocess.TimeoutExpired as error:
                    print((error.stdout or b"").decode(errors="replace"))
                    debug = subprocess.run(gdb + ["-ex", "target remote :" + str(port),
                                           "-ex", "info threads", "-ex", "thread apply all bt 5"],
                                           text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                           timeout=10)
                    print(debug.stdout)
                    raise SystemExit("FAIL QEMU timed out; no timing conclusion can be drawn")
                print(result.stdout)
                if result.returncode or "PASS 1001 ARMv7 CPU reads" not in result.stdout:
                    raise SystemExit("FAIL QEMU boot smoke test")
                stacks = {int(core): int(sp, 16) for core, sp in
                          re.findall(r"WORKER core=(\d) SP=(0x[0-9a-f]+)", result.stdout)}
                if (set(stacks) != {1, 2} or not 0x1700000 <= stacks[1] < 0x1800000
                        or not 0x1100000 <= stacks[2] < 0x1200000):
                    raise SystemExit("FAIL worker stacks overlap or occupy an unexpected region")
                print("PASS worker stacks occupy separate 6 MiB regions")
            finally:
                process.terminate()
                process.wait(timeout=5)
                messages = (work / "qemu.log").read_text()
                print(messages, end="")
        print("PASS actual ARMv7 kernel boot with", args.rom,
              "ROM; GPIO handshakes and real-time deadlines need hardware")


if __name__ == "__main__":
    main()
