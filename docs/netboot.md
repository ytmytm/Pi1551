### Raspberry Pi 3 network boot (DHCP+TFTP) for PI1551 development

This guide lets you boot the Pi 3 directly over the network so you can iterate on `kernel.img` without touching an SD card. It uses only DHCP + TFTP (no NFS root), which is ideal for this bare-metal firmware.

Prereqs

- A Raspberry Pi 3B/3B+ on wired Ethernet
- A DHCP server you can configure (examples: dnsmasq, ISC dhcpd)
- A TFTP server (examples: dnsmasq built-in TFTP, tftpd-hpa)
- Your workstation/build box has access to update the TFTP root

Overview

On power-up, the Pi 3 (with network boot enabled) uses DHCP to learn the TFTP server and boot filename, then fetches the Raspberry Pi firmware over TFTP (`start.elf`, `fixup.dat`, etc.), reads `config.txt`, and loads your `kernel.img`. You only need to copy new `kernel.img` to the TFTP folder per build.

Step 0 — Enable network boot on the Pi 3 (one-time)

- For Pi 3B (non-plus): you must set an OTP bit once.
  - Boot the Pi from any SD card with Raspberry Pi OS, then run:
    ```bash
    echo program_usb_boot_mode=1 | sudo tee -a /boot/config.txt
    sudo reboot
    vcgencmd otp_dump | grep 17:
    ```
  - Expect to see `17:3020000a`. Remove the line from `/boot/config.txt` afterwards.
- Pi 3B+ ships with netboot enabled by default; you can skip this step.

Step 1 — Prepare the TFTP root

- Create the TFTP directory on your server, e.g. `/tftpboot`.
- Place Raspberry Pi firmware files in the TFTP root. Minimum set typically includes:
  - `bootcode.bin` (optional; Pi 3 can netboot without SD; keep for compatibility)
  - `start.elf`, `fixup.dat`
  - `config.txt`
  - `kernel.img` (from this project: run `make` then take `kernel.img` from repo root)
- Directory layout example:
  ```
  /tftpboot/
    config.txt
    kernel.img
    start.elf
    fixup.dat
    bootcode.bin
  ```
- Optional: Per-device directory
  - The Pi firmware will first try a per-serial directory. You can create one to isolate multiple Pis.
  - Example if your Pi serial is `a1b2c3d4`:
    ```
    /tftpboot/a1b2c3d4/config.txt
    /tftpboot/a1b2c3d4/kernel.img
    /tftpboot/a1b2c3d4/start.elf
    /tftpboot/a1b2c3d4/fixup.dat
    ```
  - If present, DHCP should point to that directory with `filename` = `a1b2c3d4/start.elf` or to the directory (some servers support `filename` as a path prefix). If not using per-serial, keep files in the root and set `filename` to `start.elf`.

Step 2 — Create a minimal config.txt for this project

- For bare-metal Pi1551, a simple `config.txt` works:
  ```
  kernel=kernel.img
  arm_64bit=0
  disable_overscan=1
  gpu_mem=64
  dtoverlay=disable-bt
  enable_uart=1
  core_freq_min=250
  force_turbo=1
  ```
- If you already tuned `config.txt` for SD boot (see docs/PI1551-Review-and-HowTo.md), reuse the same here.

Step 3A — DHCP/TFTP using dnsmasq (simple, recommended)

- Install:
  ```bash
  sudo apt-get install -y dnsmasq
  ```
- Example `/etc/dnsmasq.d/rpi-netboot.conf`:
  ```
  interface=eth0
  bind-interfaces
  dhcp-range=192.168.1.100,192.168.1.200,12h

  # Static IP for the Pi (optional but recommended)
  # Replace MAC below with your Pi's Ethernet MAC (prefix often b8:27:eb or dc:a6:32)
  dhcp-host=b8:27:eb:aa:bb:cc,192.168.1.150

  # Enable TFTP and point to TFTP root
  enable-tftp
  tftp-root=/tftpboot

  # Hand the firmware entry point to the Pi
  # If using a per-serial dir, set filename to <serial>/start.elf
  dhcp-boot=start.elf

  # Optional logging
  log-dhcp
  ```
- Restart:
  ```bash
  sudo systemctl restart dnsmasq
  ```

Step 3B — DHCP (ISC dhcpd) + separate TFTP (tftpd-hpa)

- Install:
  ```bash
  sudo apt-get install -y isc-dhcp-server tftpd-hpa
  ```
- `/etc/default/tftpd-hpa`:
  ```
  TFTP_USERNAME="tftp"
  TFTP_DIRECTORY="/tftpboot"
  TFTP_ADDRESS=":69"
  TFTP_OPTIONS="--secure --create"
  ```
- Restart TFTP:
  ```bash
  sudo systemctl restart tftpd-hpa
  ```
- DHCP snippet in `/etc/dhcp/dhcpd.conf`:
  ```
  subnet 192.168.1.0 netmask 255.255.255.0 {
    range 192.168.1.100 192.168.1.200;
    option routers 192.168.1.1;
    option domain-name-servers 192.168.1.1;

    host rpi3 {
      hardware ethernet b8:27:eb:aa:bb:cc; # Pi's MAC
      fixed-address 192.168.1.150;
      next-server 192.168.1.10;            # TFTP server IP
      filename "start.elf";               # or "<serial>/start.elf" if per-serial
    }
  }
  ```
- Restart DHCP:
  ```bash
  sudo systemctl restart isc-dhcp-server
  ```

Step 4 — Development loop

- Build: from repo root
  ```bash
  make RASPPI=3 V=1
  ```
- Copy the new kernel to TFTP root:
  ```bash
  cp /home/maciej/Maciejdev/plus4/Pi1551/Pi1541/kernel.img /tftpboot/kernel.img
  # or to your per-serial folder:
  # cp .../kernel.img /tftpboot/<serial>/kernel.img
  ```
- Power-cycle or reset the Pi; it will fetch the updated `kernel.img` on next boot.

Troubleshooting

- Watch DHCP/TFTP logs:
  - dnsmasq: `sudo journalctl -u dnsmasq -f`
  - tftpd-hpa: `sudo journalctl -u tftpd-hpa -f`
- Packet capture:
  ```bash
  sudo tcpdump -i eth0 port 67 or port 68 or port 69
  ```
- Common issues:
  - Permissions on `/tftpboot` (world-readable). For tftpd-hpa, files must be under the `--secure` directory.
  - Wrong `filename` or missing `start.elf`/`fixup.dat`.
  - Per-serial path mismatch: if DHCP says `<serial>/start.elf`, all firmware files must also exist under that directory.

Notes

- This project does not require NFS/rootfs. Only `kernel.img` and firmware files are fetched over TFTP.
- If you prefer SD for config but netboot for kernel, you can place only `bootcode.bin` on SD and serve the rest via TFTP; however, pure network boot is simpler once OTP is set.

Boot priority and using SD alongside netboot

- SD has priority on Pi 3. If the SD card contains valid Raspberry Pi boot firmware (`start.elf`, `fixup*.dat`, `config.txt`, `kernel*.img`), the Pi will boot from SD and will not netboot.
- To force netboot while keeping the SD inserted for runtime files:
  - Remove or rename those SD boot files (`start.elf`, `fixup*.dat`, `config.txt`, `kernel*.img`).
  - Or leave only `bootcode.bin` on SD; it will fetch the rest via TFTP. This can improve reliability on some Pi 3 units.
- Runtime file I/O in this firmware: once the kernel starts, it mounts the SD card and reads assets from there (e.g. `options.txt`, `/1551`, ROMs, images). If SD is absent, you must switch to USB storage in the UI or modify the code to read elsewhere.
