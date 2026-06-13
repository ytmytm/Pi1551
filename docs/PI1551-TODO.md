# HAT

- could also add 100R parallel resistors on all the lines to reduce noise

# Browser mode

- browser mode is sometimes faulty
    - must do DIR after each C16 reset, otherwise DLOAD won't work (OPEN15,8,15:CLOSE15 is not enough)
    - there is clearly some wrong state after reset issue
    - that's exactly the problem described below as shift+run/stop
    - but DLOAD"* works, it's shift+run/stop which doesn't
+ (done) must support SD2IEC option to list disk images as DIR
    - docs: https://c64os.com/post/sd2iecdocumentation
    - this is enabled unconditionally, not an option
    - there is an extended command to turn it on/off, but we need also option in options.txt
    - then must ALSO support 'CD:<diskimage.d64>' command to start emulation
        - fix incomplete, can't exit the emlation with `SW1`, immediately goes back
    - ? is there anything in SD2IEC docs about automatically creating disk image fliplist (what button 5 does)
        - could get right of buttons 4/5 and do everything with encoder + longpress (add)
+ (done) add non-emulation disk-image mode like in Arduino tcbm2sd reference:
    + only for d71/d81/d82/d80
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
+ (done) 'VDC Challenge' (and some other gfx slideshows) use HYPALOAD7 fastloader, that relies on timing.
   Root cause: phase of `Update(16)` vs 6502 halves + 2nd-half stretch for HYPALOAD7 poll window.
   Fix: `Update(16)` after 1st half only; busy-loop delay after 2nd `Step()`; strict 1MHz SYSTIMER (see Emulate1551).

# This branch (pi1551-refactor-attempt)

- pi1551.update much faster thanks to targeted published messages about status of the device (one way) and buttons (another way)
- HDMI/OLED/buttons checked in UI core much less frequently
- acceptable timing (1MHz with some jitter) and duty cycle (30%)
- both bitfire demos (carrion 121) and hypaload (VDC challenge, carrion gfx, standalone hypaload) work; demo Plus4 XL works on Plus4, not on C16 with 6510 replacement

# TODO:

+ (done) disable 'disk always spinning' option for pi1551 update
+ (done) OLED screen gets garbled in emulation mode when switching disk images
+ (done) rotary encoder doesn't work so well in emulation mode, skips wrong direction a lot more than in browser mode (it's not perfect there either)
+ (done) disable 1MHz output to reduce EM noise
+ (done) disassemble 'Corruption' loader and test standalone, it's a software issue

+ star file option doesn't seem to work, need output on HDMI to see what is going on
+ with single disk image in flip list pressing rotary button doesn't exit the emulation - it goes immediately back
+ .lst files supported
+ rebrand to pi1551
+ credit myself
+ remove irrlevant options from options.txt
+ restore multiple ROM option (it was ROM1/ROM2 etc.) on longpress
+ SW4 in emulation mapped to exit emulation
+ prepare full bootable sd card for download
+ update readme and descriptions, original readme didn't have full information (it was on the website)

- remove dead code and 1541/1581 stuff

- shift+run/stop as DLOAD"* shortcut doesn't work properly
    - hangs if used directly
    - hangs if only ?DS$ was done after reset
    - hangs if there was LOAD"$",8
    - but works if directory was listed with DIR
    - suggests that this is some kind of state problem
    - HDMI says WRITE_BYTE[4] and timeout waiting for DAV=1

- disabled U0 by changing status from 'TCBM2SD' into 'TCBM3SD' COMPAT in reset msg
    - files can be loaded from d81 (alpharay,princeofpersia) for tcbm-patched games
    - d64 works normally (without fastdir it's possible to exit them)
    - maybe this needs a clean room implementation based on https://www.pagetable.com/?p=1324
      and Arduino (only at future stage add support for multiple channels for BASIC operations)
      to decouple it from legacy IEC code
- support U0 commands from tcbm2sd (fastload protocol, device number change)
    - fastloader from browser works
    - fastdir works from browser and images (incl. d64)
    - fastloader from d64 fails immediately, doesn't load anything HDMI says timeout waiting for fastload to complete; pi1551 is stuck with disk image data for FASTDIR
    - could it be b/c it can't find the file by its name (confusion of ascii-petscii encoding)
    - fastloader with U0 $1f doesn't work - prince of persia tcbmfast shows that handler is
      stuck with loading directory(?) as if filename was not recognized or some old data stuck
    - block read doesn't work (block-rw prg with d64 mounted) either
    - 1551 DOS CPU trap enter/exit in wrong place? should be caught and exited in the same way as we handle 'CD' command
    - Write support for D64/D71/D81 from original diskimage-0.95 (restore di_write, BAM updates).
    - D80/D82 write per VICE disk image reference — read-only for now.
    - werify read/write/allocate/bam against vice doc


# Tests

- test if h/w reset doesn't end emulation, should just reset drive
- (done) test if `CD:<diskimage>` works now
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
+ correctly setup fastboot (reset starts at $e9b5, ends after 1012787 when PC reaches mainloop at eabd)
+ trap/patch ROM to ignore motor spin up delay
+ backport 32K ROM support + 8K RAM + backport RAMBOard][ DOS patches
+ TAP format support (done) (how? IRQ on MOTOR, timed IRQ to pump data)
    - runtime option `tapeMotorAlwaysOn` (default 1) replaces compile-time `TAPE_MOTOR_SUPPORT`; 1 = ignore MOTOR GPIO and keep motor active, 0 = poll MOTOR GPIO; SENSE stays asserted while a TAP is loaded and not at end, motor still gates playback

# Build on MacOS

make clean && make RASPPI=3 V=1 PREFIX="/Applications/ArmGNUToolchain/15.2.rel1/arm-none-eabi/bin/arm-none-eabi-" -j12
