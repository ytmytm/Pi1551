.DEFAULT_GOAL := all

OBJS_BASE	= armc-start.o armc-cstartup.o armc-cstubs.o armc-cppstubs.o \
	exception.o main.o rpi-aux.o rpi-i2c.o rpi-mailbox-interface.o rpi-mailbox.o \
	rpi-gpio.o rpi-interrupts.o dmRotary.o cache.o ff.o interrupt.o Keyboard.o performance.o \
	DiskImage.o m6502.o Pi1551.o Drive1551.o \
	commands_base.o tcbm_bus.o tcbm_commands.o tcbm2sd_protocol.o cbm_diskimage.o m6523.o InputMappings.o \
	gcr.o prot.o lz.o emmc.o diskio.o options.o Screen.o SSD1306.o ScreenLCD.o \
	Timer.o FileBrowser.o DiskCaddy.o ROMs.o xga_font_data.o SpinLock.o tape_player.o \
	OledMenu.o OledSettings.o Pi1551Oled.o RtcI2c.o

#	Drive.o Pi1541.o iec_bus.o iec_commands.o m6522.o m8520.o wd177x.o Pi1581.o InputMappings.o

SRCDIR   = src
OBJS    := $(addprefix $(SRCDIR)/, $(OBJS_BASE))

-include $(OBJS:.o=.d)

LIBS     = uspi/libuspi.a
INCLUDE  = -Iuspi/include/

TARGET  ?= kernel

.PHONY: all help test-tcbm2sd-native tui $(LIBS)

all: $(TARGET)

help:
	@printf '%s\n' \
		'Pi1551 targets:' \
		'  make / make all          Build Raspberry Pi 3 firmware ($(TARGET).img).' \
		'  make tui                 Start the interactive OLED TUI simulator.' \
		'  make test-oled-native    Run native menu, media, tape and OLED tests with ASan/UBSan.' \
		'  make test-tcbm2sd-native Run native TCBM protocol regression tests.' \
		'  make clean               Remove firmware and USB-library build artifacts.' \
		'  make help                Show this help.' \
		'' \
		'Build options:' \
		'  -j4                      Compile with four parallel jobs.' \
		'  V=1                      Show full build commands.' \
		'  PREFIX=arm-none-eabi-    Select the cross-compiler prefix (default shown).' \
		'  TARGET=kernel            Select the firmware output basename (default shown).' \
		'  RASPPI=3                 Raspberry Pi 3 is the only supported hardware target.' \
		'' \
		'TUI requires Python 3; native tests require Python 3 and host GCC/G++.'

test-tcbm2sd-native:
	python3 tests/test_tcbm2sd_protocol.py

.PHONY: test-oled-native
test-oled-native:
	python3 tests/test_oled_native.py

tui:
	python3 tools/pi1551_oled_tui.py

$(TARGET): $(OBJS) $(LIBS)
	@echo "  LINK $@"
	$(Q)$(CC) $(CFLAGS) -o $(TARGET).elf -Xlinker -Map=$(TARGET).map -T linker.ld -nostartfiles $(OBJS) $(LIBS) -lstdc++ -lm
	# $(Q)$(PREFIX)objdump -d $(TARGET).elf | $(PREFIX)c++filt > $(TARGET).lst
	$(Q)$(PREFIX)objcopy $(TARGET).elf -O binary $(TARGET).img

uspi/libuspi.a:
	$(MAKE) -C uspi

clean:
	$(Q)$(RM) $(OBJS) $(TARGET).elf $(TARGET).map $(TARGET).lst $(TARGET).img
	$(MAKE) -C uspi clean

include Makefile.rules
