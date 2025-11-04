// Pi1541 - A Commodore 1541 disk drive emulator
// Copyright(C) 2018 Stephen White
//
// This file is part of Pi1541.
// 
// Pi1541 is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// Pi1541 is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with Pi1541. If not, see <http://www.gnu.org/licenses/>.

#ifndef TAPE_PLAYER_H
#define TAPE_PLAYER_H

#include "types.h"
#include "ff.h"

extern "C"
{
#include "rpi-gpio.h"
}

// Fixed GPIO pins for tape interface
#define TAPE_MOTOR_GPIO 6   // Pin 31, input, active-high
#define TAPE_READ_GPIO  19  // Pin 35, output
#define TAPE_WRITE_GPIO 26  // Pin 37, input (stub, not used)

// Maximum number of half-waves we can store (static allocation)
// Typical TAP files have thousands of pulses, so we need a reasonable buffer
// For 300MB available on RPi0, we can use a large buffer
#define TAPE_MAX_HALFWAVES (10 * 1024 * 1024)  // 10M half-waves should be enough for most tapes

struct TapeUIState
{
	bool isLoaded;
	bool motorActive;
	bool atEnd;
	char filename[256];
	u32 tapeCounter;  // 3-digit counter (000-999)
	u32 currentTimeMs;  // Current position in milliseconds
	u32 totalTimeMs;  // Total tape length in milliseconds
	u8 percentage;  // Playback percentage (0-100)
};

class TapePlayer
{
public:
	TapePlayer();
	~TapePlayer();

	// Load a TAP file (replaces any existing tape)
	bool LoadTap(const FILINFO* fileInfo);

	// Check if a tape is loaded
	bool IsLoaded() const { return loaded; }

	// Check if we're at the end of the tape
	bool AtEnd() const { return atEnd; }

	// Get current motor state
	bool GetMotorActive() const { return motorActive; }

	// Called when motor state changes (from polling)
	void OnMotorChange(bool motorHigh);

	// Get UI state for display
	void GetUIState(TapeUIState& state) const;

	// Update counter based on elapsed time (called periodically from main loop)
	void Tick10ms();

	// Reset playback (start from beginning)
	void Reset();

	// Initialize GPIO pins
	void InitializeGPIO();

private:
	// Parse TAP file and build half-wave timing array
	bool ParseTAP(FIL& fp, u32 fileSize);
	void FreePulseBuffer();

	// System timer IRQ handler (static to work with interrupt system)
	static void TapeIRQHandler(void* pParam);

	// Internal IRQ handler (called from static)
	void HandleTapeIRQ();

	// Setup/teardown tape IRQ
	void SetupIRQ();
	void TeardownIRQ();

    // Pulse timing buffer (microseconds per half-wave)
    static u64* pulseTimings;
    static size_t pulseCount;
    static size_t currentPulseIndex;
	static bool loaded;
	static bool motorActive;
	static bool atEnd;
	static bool readLineState;  // Current state of READ line (true = high, false = low)
	static u64 totalPlaybackTimeUs;  // Total time in microseconds
	static u64 cumulativePulseTimeUs;  // Cumulative time from pulses played (derived from currentPulseIndex)
	static char tapFilename[256];
	static TapePlayer* instance;  // Singleton instance for IRQ handler
};

#endif // TAPE_PLAYER_H
