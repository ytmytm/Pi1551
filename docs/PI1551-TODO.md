# HAT

- forgot about rotary encoder connection
    - rotary encoder should enable 3 functions (next/previous/select) but one more (ADD in browser mode) can be added with longpress
- hat fix for /RESET confirmed in falstad (1 diode w/o pullup is good enough, but pullup recommended)
    - (todo) if connected to /RESET3_3V might need a pullup on 5V side as well
- could also add 100R parallel resistors on all the lines to reduce noise
- TAP support - header for tape recorder? how U2+ does it? how Arduino does it?
    - RPi circuit https://github.com/RhinoDevel/cbmtapepi
    - Tapuino (TAP) https://github.com/sweetlilmre/tapuino
    - needs 3 GPIOs 6, 19, 26; SENSE is grounded and motor goes through optocoupler 4N35; WRITE is optional
    
# Browser mode

- browser mode is sometimes faulty
    - must do DIR after each C16 reset, otherwise DLOAD won't work (OPEN15,8,15:CLOSE15 is not enough)
- `StarFileName` doesn't seem to work if we are in another folder (not in /1551/)
- fastload / fastdir mode (tcbm2sd `U0` commands) doesn't work
    - it's not recognized by DIRECTORY BROWSER: 'TCBM2SD' must be in the UI/UJ status
    - then both BOOT.T2SD and PAROBEK should recognize fastloader (stunt car racer must load in ~6s - definietly less than 10s)
- must support SD2IEC option to list disk images as DIR
    - then must ALSO support 'CD:<diskimage.d64>' command to start emulation
    - ? is there anything in SD2IEC docs about automatically creating disk image fliplist (what button 5 does)

# Tests

- (done) stick in parobek, test 1551 fastloader
    ? stays on READY with on cursor after load, why ?
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
    - splitIEClines even interferes with i2c bus selector, must be removed
- rebrand to Pi1551
- provide fully prepared sd card boot partition for download
- add traps in ROM code to handle U0 commands to support tcbm2sd fastload protocol (all the commands)
- on safe tcbm2sd > 1.4 the hat could drive line 16 and /RESET to enable/disable Arduino
    - NO it could not - that would connect Arduino /RESET to system /RESET by default - which is not optimal
- backport 32K ROM support + 8K RAM + backport RAMBOard][ DOS patches?
    - probably no one cares to do it on real hardware (I would, but without real circuit it's just emulating fantasy stuff)
- TAP format support (how? IRQ on MOTOR, IRQ or DMA to pump data)
    - see pi1551-tap-support branch
    - hardware not defined yet, needs at least READ and MOTOR, tie SENSE to GND, WRITE is optional
