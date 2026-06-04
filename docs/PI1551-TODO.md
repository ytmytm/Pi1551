# HAT

- forgot about rotary encoder connection
- hat fix for /RESET confirmed in falstad (1 diode w/o pullup is good enough, but pullup recommended)
- could also add 100R parallel resistors on all the lines to reduce noise
- it would be possible to enable/disable arduino
    - only on safe tcbm2sd > 1.3 without diode because it won't pass the reset back into tcbm2sd
    - control line #16 from RPi - switch all lines to input (it will be pulled up as unconnected) except for reset to restart arduino/system
- TAP support (works) - header for tape recorder? how U2+ does it? how Arduino does it?
    - RPi circuit https://github.com/RhinoDevel/cbmtapepi
    - Tapuino (TAP) https://github.com/sweetlilmre/tapuino
    - Pitap https://gp2x.org/pitap/
    - needs 4 GPIOs, all of them through NPN open-collector with 3.3V/5V pullups

# Browser mode

- browser mode is sometimes faulty
    - must do DIR after each C16 reset, otherwise DLOAD won't work (OPEN15,8,15:CLOSE15 is not enough)
    - there is clearly some wrong state after reset issue
    - see below for a missing test
    - ALL operations must handle timeout or RESET condition
        - this must be done CAREFULLY so that the system doesn't lock up on startup/init
- `StarFileName` doesn't seem to work if we are in another folder (not in /1551/)
    - Always constructs absolute path under DEFAULT_BROWSE_DIR regardless of current directory
    - If starts with '/', trim leading '/' and prepend DEFAULT_BROWSE_DIR (e.g., '/a.prg' -> '/1551/a.prg')
    - If doesn't start with '/', prepend DEFAULT_BROWSE_DIR (e.g., 'a.prg' -> '/1551/a.prg')
    - Examples: 'a.prg' -> '/1551/a.prg', '/games/a.prg' -> '/1551/games/a.prg', 'games/a.prg' -> '/1551/games/a.prg'
        - fix done, not right yet
- fastload / fastdir mode (tcbm2sd `U0` commands) doesn't work
    - it's not recognized by DIRECTORY BROWSER: 'TCBM2SD' must be in the UI/UJ status
        - add it in a nicer way (it's hardcoded in commands_base now)
    - then both BOOT.T2SD and PAROBEK should recognize fastloader (stunt car racer must load in ~6s - definietly less than 10s)
    - they do but the protocol doesn't work from the browser (low priority)
        - LOAD doesn't work but it seems fast dir listing does
- must support SD2IEC option to list disk images as DIR
    - docs: https://c64os.com/post/sd2iecdocumentation
    - there is an extended command to turn it on/off, but we need also option in options.txt
    - then must ALSO support 'CD:<diskimage.d64>' command to start emulation
        - fix incomplete, can't exit the emlation with `SW1`, immediately goes back
    - ? is there anything in SD2IEC docs about automatically creating disk image fliplist (what button 5 does)
        - could get right of buttons 4/5 and do everything with encoder + longpress (add)
- add non-emulation disk-image mode like in Arduino tcbm2sd reference:
    - only for d71/d81/d82/d80
    - arduino does read only, but we can use the diskimage.c library for read/write
        - write support for d64/71/81 was removed when porting to arduino, need to be brought back
        - d82/d80 need this developed according to VICE disk image docs, until then fail with WRITE PROTECT ON
+ (done) add missing DOS1551 traps
    - CD.. or CD<-
    - fastboot (skip some amount of cycles)
    - spin up skip (skip delay when motor starts up)
+ (done) *any* TPI access clears byte ready, not only reads but also writes
+ (done) bitfire loader, it requires that 1551 porta bits on input are pulled high when other end is high-z - but RPi pullups are not sufficient
  (it also requires that status bits can switch direction but that was already handled in tcbm2sd)
  (external pullups to 3.3V on DIO1-8 required - just in case they should be on all lines: ST0/ST1/DAV/ACK)
- 'VDC Challenge' (and some other gfx slideshows) use HYPALOAD7 fastloader, that relies on timing.
   Root cause: phase of `Update(16)` vs 6502 halves + 2nd-half stretch for HYPALOAD7 poll window.
   Fix: `Update(16)` after 1st half only; busy-loop delay after 2nd `Step()`; strict 1MHz SYSTIMER (see Emulate1551).

this branch: timing-exploration: 20260321
- cycle counter emitted on GPIO1 (28)
- still a lot of jitter
- pi1551.Update may take over 400ns (350-450) from measurements
- can interleave that between 2MHz cycles (not at the end)
- bitfire still works, VDC challenge/corruption doesn't (maybe then would with different delay? but the delay makes no sense)
- gpio input/output is updated only when 6502 needs it / does it
- but we're not using cycle timer to keep 50% duty - when disk is spinning we won't be able to keep 50% anyway
- HDMI introduces a lot of jitter
- again (like in pi-tap-support-devel* branches) is seems the only way for stable 1MHz/50% duty is to put pi1551.Update() on separate core and use cycle timer (from RPI2 on RPI3) to keep 50% duty cycle
- still don't understand why in the previous commit, VDC challenge works with drive emulation almost half as slow

# Timings (relative to 83b3e75b577dedec378b96b8fcdebc59185cd355 )

- timing on gpio1 (28) with low just before the cycle2MHz loop starts
  and high just after 2nd round (with housekeeping and sync to 1MHz)
  with extra delay for cycle=1 enabled with busy loop for 100 iterations (VDC challenge works)
  with delay 100 when drive idle
  low: short 13%  high: long 87%   f: 798-811Hz (unstable)
  with delay 100 when drive busy
  low: short 9%  high: long 91%   f: 650Hz (unstable)
  with delay 50 when drive idle
  low: short 16%  high: long 84%   f: 1000Hz (unstable)
  with delay 50 when drive busy
  low: short 11%  high: long 89%   f: 770Hz (unstable)
  without delay (Bitfire works) busy/idle similar
  low: short 15% high: long 85% f:1000Hz (unstable)
  without delay, pi1551 update moved between cycle 1/2 inside loop
  low: short 20% high: long 80% f:1000Hz (unstable) (idle)
  low:  50% high: 50% f:988Hz (unstable) busy (VDC doesn't work, bitfire works)
  with delay 100, pi1551 update moved between cycle 1/2 inside loop (VDC works, corruption too, Bitfire doesn't)
  low 15% high 85%, 800Hz (idle)
  low 33% high 67%, 620Hz (busy)

	RPI_SetGpioPinFunction(RPI_GPIO1, FS_OUTPUT);
	RPI_SetGpioHi(RPI_GPIO1);

		// Timing marker on GPIO1:
		// low = first 2MHz cycle, high = second cycle + housekeeping.
		RPI_SetGpioLo(RPI_GPIO1);
		for (int cycle2MHz = 0; cycle2MHz < 2; ++cycle2MHz)
		{
			if (cycle2MHz == 1)
			{
				RPI_SetGpioHi(RPI_GPIO1);
			}
			if (pi1551.m6502.SYNC())	// About to start a new instruction.

pi1551.Update - ~350-400ns when drive busy and spinning


# Tests

- test if h/w reset doesn't end emulation, should just reset drive
- test if `CD:<diskimage>` works now
    - yes, but can't exit the image / end emulation with `SW1`, immediately goes back into image
- (done) test if `DLOAD"*` works right after reset
- (done) test if `StarFileName` works from any subfolder
- (done) test if /RESET_3_3V needs a pullup to 3.3V - YES
- (done) test if DLOAD after reset would work if `OPEN15,8,15,"I":CLOSE15` is issued or `?DS$`
- (done) stick in parobek, test 1551 fastloader
- (done) test if switching to drive 9 works
- (done) save tested
- (done) base and -clean versions tested
- (done) remove DIO8 printing (appears in browser mode too)
- (done) test with alternative DOS https://plus4world.powweb.com/software/Super_DOS_1551
- (done) turn off debug option

# Other

- backport https://github.com/pottendo/pottendo-Pi1541
    - can upload new kernel via wifi (!)
    - with wifi enabled we can download stuff from plus4world (like Assembly64) - download+unzip to /tmp (RAM)
    - do it while 1541/81 code is present
    - runs much hotter than this legacy code
- remove irrelevant options, this is pure 1551, no IEC, no VIA, no 1581 - OR keep it but update Makefile/Makefile.rules
    - old code was kept for reference
- rebrand to Pi1551
- credit myself where appropriate
- provide fully prepared sd card boot partition for download
- add traps in ROM code to handle U0 commands to support tcbm2sd fastload protocol (all the commands)
+ correctly setup fastboot (reset starts at $e9b5, ends after 1012787 when PC reaches mainloop at eabd)
+ trap/patch ROM to ignore motor spin up delay
+ backport 32K ROM support + 8K RAM + backport RAMBOard][ DOS patches?
    - probably no one cares to do it on real hardware (I would, but without real circuit it's just emulating fantasy stuff)
+ TAP format support (done) (how? IRQ on MOTOR, timed IRQ to pump data)
    - runtime option `tapeMotorAlwaysOn` (default 1) replaces compile-time `TAPE_MOTOR_SUPPORT`; 1 = ignore MOTOR GPIO and keep motor active, 0 = poll MOTOR GPIO; SENSE stays asserted while a TAP is loaded and not at end, motor still gates playback
