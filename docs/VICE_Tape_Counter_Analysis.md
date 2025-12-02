# VICE Tape Counter Implementation Analysis

## Overview
Based on VICE's architecture, the tape counter is initialized in `tape/tap.c` in `tap_new()`, but the actual counter updates happen in the datasette module.

**Key Discovery:** VICE uses a physical model formula to calculate the tape counter, modeling the tape spool geometry. The counter formula is defined in `datasette/datasette.h`.

## Key Files to Examine

### 1. `datasette/datasette.c`
This is the main file where the tape counter is updated. Look for:
- **`datasette_tick()`** or **`datasette_update()`** - These functions are called periodically during emulation
- Functions that handle tape motor state changes
- Functions that process tape data during playback

### 2. `datasette/datasette.h`
**This is where the counter formula is defined!**

The header file contains:
- **Counter formula comment**: `/* Counter is c=g*(sqrt(v*t/d*pi+r^2/d^2)-r/d) */`
- **Physical model constants**:
  - `DS_D = 1.27e-5` (tape thickness parameter)
  - `DS_R = 1.07e-2` (initial spool radius)
  - `DS_V_PLAY = 4.76e-2` (playback velocity)
  - `DS_G = 0.525` (counter gain factor)
- Structure definitions and function prototypes

### 3. `tape/tape.c`
May contain functions that:
- Read/write tape data
- Update tape position
- Call datasette update functions

## Typical Implementation Pattern

In VICE, the tape counter typically works like this:

1. **Initialization** (in `tap_new()` in `tape/tap.c`):
   - Counter is initialized to 0 or a starting value
   - Counter variable is part of the tape structure

2. **Update Mechanism** (in `datasette/datasette.c`):
   - Counter is calculated using a **physical model formula** that simulates tape spool geometry
   - Formula: `c = g × (√(v×t/(d×π) + r²/d²) - r/d)`
     - `c` = counter value
     - `g` = DS_G = 0.525 (gain factor)
     - `v` = DS_V_PLAY = 4.76e-2 (playback velocity)
     - `t` = elapsed time in seconds
     - `d` = DS_D = 1.27e-5 (tape thickness parameter)
     - `r` = DS_R = 1.07e-2 (initial spool radius)
   - This models how the spool radius decreases as tape unwinds, causing non-linear counter advancement
   - Counter wraps at 999 (3-digit display: 000-999)

3. **Update Frequency**:
   - Counter value is calculated continuously based on elapsed playback time
   - The formula produces counter increments approximately every 3 seconds during normal playback
   - This matches the original Commodore Datasette hardware behavior where the mechanical counter advanced roughly every 3 seconds
   - The non-linear formula means the counter advances faster as more tape unwinds (spool gets smaller)

## Search Strategy

To find the exact implementation in VICE:

1. **Search for counter variable names**:
   ```bash
   grep -r "counter" vice/src/datasette/
   grep -r "tape_counter" vice/src/
   grep -r "datasette.*counter" vice/src/
   ```

2. **Look for update functions**:
   ```bash
   grep -r "datasette_tick\|datasette_update" vice/src/
   ```

3. **Check for motor-related updates**:
   ```bash
   grep -r "motor.*counter\|counter.*motor" vice/src/datasette/
   ```

4. **Examine the datasette structure**:
   - Look in `datasette/datasette.h` for the main structure
   - Find where counter is defined as a member variable

## VICE Counter Formula (Actual Implementation)

VICE uses a **physical model** based on tape spool geometry. The formula is documented in `datasette.h`:

```c
/* Counter is c=g*(sqrt(v*t/d*pi+r^2/d^2)-r/d)
   Some constants for the Datasette-Counter, maybe resourses in future */

#define DS_D        1.27e-5
#define DS_R        1.07e-2
#define DS_V_PLAY   4.76e-2
#define DS_G        0.525
```

### Formula Implementation
```c
// VICE's actual counter calculation (pseudo-code)
double t_seconds = elapsed_time_in_seconds;
double term1 = (DS_V_PLAY * t_seconds) / (DS_D * PI);
double term2 = (DS_R * DS_R) / (DS_D * DS_D);
double sqrt_term = sqrt(term1 + term2);
double counter_value = DS_G * (sqrt_term - DS_R / DS_D);
int counter = ((int)(counter_value + 0.5)) % 1000;  // Round and wrap at 1000
```

### Why This Formula?
- Models the physical tape spool: as tape unwinds, the spool radius decreases
- The square root term accounts for the changing spool geometry
- Results in non-linear counter advancement (faster as more tape unwinds)
- Produces approximately 3-second increments during normal playback
- Matches the original hardware's mechanical counter behavior

## Specific Functions to Look For

1. **`datasette_tick()`** - Called every emulation cycle
2. **`datasette_update_counter()`** - Explicit counter update function
3. **`datasette_set_motor()`** - May update counter when motor state changes
4. **`datasette_advance()`** - May update counter as tape advances

## Your Implementation Reference

Your implementation in `tape_player.cpp` uses **VICE's physical model formula**:

### Implementation Details
- Uses the same formula as VICE: `c = g × (√(v×t/(d×π) + r²/d²) - r/d)`
- Same constants as VICE (DS_G, DS_V_PLAY, DS_D, DS_R)
- Converts `cumulativePulseTimeUs` from microseconds to seconds for the formula
- Calculates counter value using `std::sqrt()` from `<cmath>`
- Wraps result at 1000 (displays 000-999)

### Code Location
- Formula implementation: `src/tape_player.cpp` in `GetUIState()` method
- Includes `<cmath>` for `std::sqrt()` function
- Constants defined inline matching VICE's `datasette.h` values

### Why Match VICE?
- **Authenticity**: Matches the exact behavior of VICE emulator
- **Hardware Accuracy**: The physical model accurately represents the original Datasette's mechanical counter
- **Consistency**: Software using the counter will behave the same in VICE and your implementation
- **Non-linear Behavior**: The formula correctly models how the counter advances faster as more tape unwinds

**Reference:** Formula and constants from `https://raw.githubusercontent.com/VICE-Team/svn-mirror/main/vice/src/datasette/datasette.h`

## References

1. **VICE Header File**: `https://raw.githubusercontent.com/VICE-Team/svn-mirror/main/vice/src/datasette/datasette.h`
   - Contains the counter formula comment and constants
   
2. **VICE Source File**: `https://github.com/VICE-Team/svn-mirror/blob/main/vice/src/datasette/datasette.c`
   - Contains the actual implementation of the counter calculation
   - Search for uses of DS_G, DS_V_PLAY, DS_D, DS_R to find counter update code

## Summary

The VICE tape counter is **not** a simple linear time division. Instead, it uses a sophisticated physical model that:
- Models the tape spool geometry
- Accounts for decreasing spool radius as tape unwinds
- Produces non-linear counter advancement
- Results in approximately 3-second increments during normal playback
- Accurately emulates the original Commodore Datasette hardware behavior

This approach ensures authentic emulation and consistent behavior across different tape lengths and playback speeds.

