# Building Pi1551

Pi1551 is bare-metal Raspberry Pi firmware. It builds a `kernel.img` file that
is copied to the SD card root; it is not a host application.

## Hardware compatibility

For the best emulation compatibility, use a Raspberry Pi 3:

- Raspberry Pi 3A
- Raspberry Pi 3A+
- Raspberry Pi 3B
- Raspberry Pi 3B+

The 3A/3A+ are the smallest recommended boards. Raspberry Pi Zero 2 W has
also been tested and works with the same build, but its CPU performance is
insufficient for full emulation compatibility. Much software works; demanding
software such as demos using the Bitfire fastloader does not. Zero 2 W is a
fallback until a Pi 3 is available.

Use the same `kernel.img` and `sdcard/config.txt` on Pi 3 and Zero 2 W. The
build still uses `RASPPI=3` for both boards; there is no separate Zero 2 W
target. First-generation Pi Zero and Raspberry Pi 1/2/4/5 targets are not
supported by this firmware.

## Toolchain Install

The build needs GNU Make and an ARM bare-metal cross toolchain whose programs
are named `arm-none-eabi-gcc`, `arm-none-eabi-g++`, `arm-none-eabi-ar`, and
`arm-none-eabi-objcopy`.

On Debian or Ubuntu:

```sh
sudo apt-get update
sudo apt-get install make gcc-arm-none-eabi binutils-arm-none-eabi \
  libnewlib-arm-none-eabi libstdc++-arm-none-eabi-dev \
  libstdc++-arm-none-eabi-newlib
```

On macOS with Homebrew, install the cross compiler. Apple Command Line Tools
`make` is sufficient; Homebrew `make` is optional and is usually invoked as
`gmake` unless you add its `gnubin` directory to `PATH`.

```sh
brew install arm-none-eabi-gcc
```

If you install Arm GNU Toolchain manually, add its `bin` directory to `PATH`.
For example:

```sh
export PATH="/Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin:$PATH"
```

Verify the toolchain from the repository root:

```sh
arm-none-eabi-gcc --version
make --version
```

## Compile

From the repository root:

```sh
make clean
make
```

The default build is already `RASPPI=3`. Passing `make RASPPI=3` is equivalent
and may be useful in automation, including for Zero 2 W. Passing any other
`RASPPI` value is an error.

The build produces these root-level artifacts:

```text
kernel.img
kernel.elf
kernel.map
```

Copy `kernel.img` to the SD card root to install or upgrade the firmware on
either board. Keep the repository's `sdcard/config.txt` with it.

## Agent Build Checklist

For AI agents and CI scripts, the canonical build sequence is:

```sh
command -v arm-none-eabi-gcc
command -v arm-none-eabi-g++
command -v arm-none-eabi-objcopy
make clean
make RASPPI=3
test -f kernel.img
```

Do not try to run `kernel.img` on the build host.
