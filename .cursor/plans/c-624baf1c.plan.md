<!-- 624baf1c-f802-4d78-9edf-9703ad02039f b4c361b4-6520-424e-b854-21521e38f7d9 -->
# C16/Plus4 TAP Support – Implementation Completed

## Implementation Status: ✅ COMPLETE

All implementation tasks have been completed and the code compiles successfully.

### Completed Tasks:

1. ✅ **TAP Parser** - Implemented TAP v0 and v2 format parser with header validation and pulse timing calculation
2. ✅ **GPIO Setup** - Fixed GPIO pins (6, 19, 26) initialized with correct directions and pull-up/pull-down
3. ✅ **IRQ Handler** - TIMER1/C1 system timer IRQ implemented for precise playback timing
4. ✅ **File Browser Integration** - TAP file recognition and loading (no emulation entry)
5. ✅ **UI Display** - Filename, 3-digit counter, motor status, and END indicator implemented
6. ✅ **Debug Graph** - Motor and READ signal traces added to rolling chart
7. ✅ **Motor Polling** - Motor pin polling and gating implemented in browse loop and ISR

### Build Status: ✅ COMPILES SUCCESSFULLY

All linking issues resolved:

- GPIO functions properly wrapped in `extern "C"` (matching tcbm_bus.h pattern)
- Replaced C++ operators with malloc/free for bare metal compatibility
- Fixed const char* issues in PrintText calls

### Files Modified/Created:

- `src/tape_player.h` - Header with GPIO pins, TAP constants, TapeUIState struct
- `src/tape_player.cpp` - Full implementation of TAP player
- `src/DiskImage.h` - Added `IsTAPExtention()` declaration
- `src/DiskImage.cpp` - Added `IsTAPExtention()` implementation
- `src/FileBrowser.cpp` - Added TAP file handling
- `src/main.cpp` - Added TapePlayer instance, UI display, graph plotting, motor polling
- `Makefile` - Added `tape_player.o` to build

### Design Decisions:

- **Motor Pin Polling**: Using polling (every 10ms in main loop + checks in timer ISR) rather than edge-detect IRQ. This is appropriate because motor state changes are infrequent (seconds, not microseconds), timing-critical part is the READ line (already handled by timer IRQ), and polling provides adequate responsiveness with simpler implementation.

### Ready for Hardware Testing

The implementation is complete and ready for on-hardware verification with actual C16/Plus4 TAP files.

### References / input prompts

#### 1.

we're going to add support for TAP files
figure out and plan

1) how the TAP format looks like and how to handle it, we're going to use C16 / Plus4 only ; probably VICE documentation is a good place to start, they have a web page explaining all the file formats used by emulators

2) how to pick a TAP file - probably from a browser, selecting a TAP file doesn't "enter emulation" (like D64) but would "load a cassette into a deck"

3) TAP file plays when TAPE_MOTOR GPIO is active, and is stopped when inactive; motor is active high (motor running)

4) TAP output goes into a TAPE_READ GPIO

5) TAP input would come from TAPE_WRITE GPIO but it's just a stub, don't want to implement recording tapes yet

6) TAP UI must show TAP file name, tape counter (3 digits like on a casette deck) and some kind of indicator if motor is running or not

7) for debug we might want both motor and read pin status on the rolling chart like IEC/TCBM bus

8) not sure how to deal with actual playback, it must run from UI core so that it doesn't interfere with 1551 emulation core; probably use IRQ and/or DMA ; not sure which option is better or more efficient - prepare data stream for DMA (almost all memory is available - like 300MB on RPi0 and 1,5GB on RPi3) or calculate time gaps using some kind of timed IRQ and pull READ line low or high

9) TAPE_MOTOR probably best controlled from IRQ to enable/disable playback



