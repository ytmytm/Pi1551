# HAT

- forgot about rotary encoder connection
- hat fix for /RESET confirmed in falstad (1 diode w/o pullup is good enough, but pullup recommended)
- could also add 100R parallel resistors on all the lines to reduce noise
- it would be possible to enable/disable arduino
    - only on safe tcbm2sd > 1.3 without diode because it won't pass the reset back into tcbm2sd
    - control line #16 from RPi - switch all lines to input (it will be pulled up as unconnected) except for reset to restart arduino/system
- TAP support - header for tape recorder? how U2+ does it? how Arduino does it?
    - RPi circuit https://github.com/RhinoDevel/cbmtapepi
    - Tapuino (TAP) https://github.com/sweetlilmre/tapuino
    - needs 4 GPIO, motor goes through optocoupler 4N35; but we have 2 GPIOs left (force rotary encoder and free SW4/5?)

# Browser mode

- somewhat works
- browser mode is sometimes faulty
    - `StarFileName` doesn't seem to work
    - fastload / fastdir mode (tcbm2sd `U0` commands) don't work
- must support SD2IEC option to list disk images as DIR

# Tests

- stick in parobek, test 1551 fastloader
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
- provide fully prepared sd card boot partition for download
- add traps in ROM code to handle U0 commands to support tcbm2sd fastload protocol (all the commands)
- on safe tcbm2sd > 1.4 the hat could drive line 16 and /RESET to enable/disable Arduino
- backport 32K ROM support + 8K RAM + backport RAMBOard][ DOS patches?
    - probably no one cares to do it on real hardware (I would, but without real circuit it's just emulating fantasy stuff)
- TAP format support (how? IRQ on MOTOR, DMA to pump data)
