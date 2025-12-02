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

#include "tape_player.h"
#include "rpiHardware.h"
#include <string.h>
#include <malloc.h>
#include <limits>

extern "C"
{
#include "rpi-aux.h"
}

// Static member definitions
u64* TapePlayer::pulseTimings = nullptr;
size_t TapePlayer::pulseCount = 0;
size_t TapePlayer::currentPulseIndex = 0;
bool TapePlayer::loaded = false;
#if TAPE_MOTOR_SUPPORT
bool TapePlayer::motorActive = false;
#else
bool TapePlayer::motorActive = true;  // Motor always active when support is disabled
#endif
bool TapePlayer::atEnd = false;
bool TapePlayer::readLineState = true;  // Start with READ line high (hardware inverts GPIO signal) - matches cbmtapepi initial state, first transfer_pulse will start with LOW
// SENSE line state: true = PLAY pressed (active), false = PLAY released (inactive)
// When true: GPIO HIGH → hardware inverts to output LOW → computer sees LOW = active
// When false: GPIO LOW → hardware inverts to output HIGH → computer sees HIGH = inactive
bool TapePlayer::senseLineState = false;  // Start with PLAY released (inactive)
u64 TapePlayer::totalPlaybackTimeUs = 0;
u64 TapePlayer::cumulativePulseTimeUs = 0;
char TapePlayer::tapFilename[256] = {0};

// TAP file header structure
struct TAPHeader
{
	char signature[12];  // "C16-TAPE-RAW" for Plus/4
	u8 version;
	u8 machine;
	u8 video;
	u8 reserved;
	u32 dataSize;  // Little-endian
};

// Plus/4/C16 tape clock frequencies (Hz), as used by VICE TAP implementation (tap.c)
// CAUTION: C16 timers always run in slow mode (CLK/2), see VICE clklist[]:
// PAL: (1773447 + 1) / 2 ≈ 886,724 Hz
// NTSC: (1789772 + 1) / 2 ≈ 894,886 Hz
#define TAPE_CLOCK_PAL_HZ  886724
#define TAPE_CLOCK_NTSC_HZ 894886

TapePlayer::TapePlayer()
{
}

TapePlayer::~TapePlayer()
{
    FreePulseBuffer();
}

void TapePlayer::InitializeGPIO()
{
#if TAPE_MOTOR_SUPPORT
	// GPIO6 (MOTOR) - input with pull-up
	RPI_SetGpioInputPullUp((rpi_gpio_pin_t)TAPE_MOTOR_GPIO);
#else
	// Motor support disabled - motor is always active
#endif

	// GPIO19 (READ) - output, start high (hardware inverts, so set GPIO low for high output)
	// Matches cbmtapepi: initial state is HIGH, first transfer_pulse will start with LOW
	RPI_SetGpioOutput((rpi_gpio_pin_t)TAPE_READ_GPIO);
	RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_READ_GPIO);  // Hardware inverts: GPIO low = output high
	readLineState = true;

	// GPIO26 (WRITE) - input with pull-up (stub, not used)
	RPI_SetGpioInputPullUp((rpi_gpio_pin_t)TAPE_WRITE_GPIO);
	
	// GPIO0 (SENSE) - output, start with PLAY released (inactive state)
	// SENSE logic: senseLineState=true (SENSE=1) = PLAY pressed (active)
	//   - Set GPIO HIGH → hardware inverts to output LOW → computer sees LOW = active
	//   - Set GPIO LOW → hardware inverts to output HIGH → computer sees HIGH = inactive
	RPI_SetGpioOutput((rpi_gpio_pin_t)TAPE_SENSE_GPIO);
	RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_SENSE_GPIO);  // GPIO LOW → output HIGH → PLAY released (inactive)
	senseLineState = false;  // PLAY released (inactive)
}

bool TapePlayer::LoadTap(const FILINFO* fileInfo)
{
	// Close any existing tape
	loaded = false;
	currentPulseIndex = 0;
	atEnd = false;
	motorActive = false;
	totalPlaybackTimeUs = 0;
	cumulativePulseTimeUs = 0;
	FreePulseBuffer();
	
		// TAPE_SENSE: PLAY button released (inactive state) when TAP is unloaded
		// senseLineState=false: GPIO LOW → hardware inverts to output HIGH → computer sees HIGH = inactive
		RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_SENSE_GPIO);

	if (!fileInfo || !fileInfo->fname)
		return false;

	// Copy filename
	strncpy(tapFilename, fileInfo->fname, sizeof(tapFilename) - 1);
	tapFilename[sizeof(tapFilename) - 1] = '\0';

	// Open and parse TAP file
	FIL fp;
	FRESULT res = f_open(&fp, fileInfo->fname, FA_READ);
	if (res != FR_OK)
		return false;

	bool success = ParseTAP(fp, fileInfo->fsize);
	f_close(&fp);

	if (success)
	{
		loaded = true;
		currentPulseIndex = 0;
		atEnd = false;
		readLineState = true;  // Start with HIGH (output) like cbmtapepi, first pulse will toggle to LOW
		RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_READ_GPIO);  // Hardware inverts: GPIO low = output high
		// TAPE_SENSE: PLAY button pressed (active state) when TAP is loaded
		// senseLineState=true: GPIO HIGH → hardware inverts to output LOW → computer sees LOW = active
		RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_SENSE_GPIO);
		senseLineState = true;  // PLAY pressed (active)
		
#if TAPE_MOTOR_SUPPORT
		// Motor support enabled - CoreLoop() will wait for MOTOR line to become active
#else
		// Motor support disabled - motor is always active, start playback immediately
		motorActive = true;
#endif
		
		// Core 2 playback loop will see loaded/motorActive and start generating pulses.
	}

	return success;
}

void TapePlayer::FreePulseBuffer()
{
	if (pulseTimings)
	{
		free(pulseTimings);
		pulseTimings = nullptr;
	}
    pulseCount = 0;
}

bool TapePlayer::ParseTAP(FIL& fp, u32 fileSize)
{
	if (fileSize < 20)
		return false;  // Too small to be a valid TAP

	TAPHeader header;
	UINT bytesRead;

	// Read header
	if (f_read(&fp, &header, sizeof(header), &bytesRead) != FR_OK || bytesRead != sizeof(header))
		return false;

	// Validate signature
	if (memcmp(header.signature, "C16-TAPE-RAW", 12) != 0)
		return false;

	// Validate version (support v0, v1, and v2 as per VICE TAP spec)
	if (header.version != 0 && header.version != 1 && header.version != 2)
		return false;
	
	// Validate machine (0x02 = Plus/4/C16)
	if (header.machine != 0x02)
		return false;
	
	// Determine Plus/4 tape clock based on video standard (0 = PAL, 1 = NTSC)
	// Used to compute:
	// - TAP units → microseconds: unit_to_us = 8,000,000 / clockSpeed
	// - CPU cycles → microseconds: cycles_to_us = 1,000,000 / clockSpeed
	double clockSpeed      = (header.video == 1) ? (double)TAPE_CLOCK_NTSC_HZ : (double)TAPE_CLOCK_PAL_HZ;
	double unitToUs        = 8000000.0 / clockSpeed;  // 8 cycles per TAP unit
	double cyclesToUs      = 1000000.0 / clockSpeed;  // 1 cycle

	// Read data size (little-endian)
	u32 dataSize = header.dataSize;
	if (dataSize == 0 || dataSize > (fileSize - 20))
		dataSize = fileSize - 20;  // Use remaining file size if header says 0

	// Allocate buffer for reading data
	u8* dataBuffer = (u8*)malloc(dataSize);
	if (!dataBuffer)
		return false;

	if (f_read(&fp, dataBuffer, dataSize, &bytesRead) != FR_OK)
	{
		free(dataBuffer);
		return false;
	}

	// Parse data using VICE TAP semantics:
	// - v0: each byte is a complete cycle in TAP units (8 cycles per unit)
	// - v1:
	//     * normal bytes (>0): complete cycle in TAP units
	//     * 0x00: extended length, next 3 bytes are absolute cycle count
	// - v2:
	//     * each byte is a half-wave in TAP units
	//     * 0x00: extended length, next 3 bytes are TAP units (half-wave)
	// We always convert to half-waves in microseconds for CoreLoop() to use.
	u32 dataIndex = 0;
	u64 totalTimeUs = 0;
	size_t newPulseCount = 0;

    size_t newCapacity = TAPE_MAX_HALFWAVES;
    u64* newTimings = (u64*)malloc(newCapacity * sizeof(u64));
    if (!newTimings)
    {
        free(dataBuffer);
        return false;
    }

	if (header.version == 0)
	{
		// Version 0: each byte represents a complete low-high cycle in TAP units.
		// If byte is 0x00, use 256 units (overflow condition).
		while (dataIndex < bytesRead)
		{
			u32 unitValue = dataBuffer[dataIndex++];
			if (unitValue == 0x00)
				unitValue = 256;  // Overflow condition in v0

			// Split full cycle into two half-waves in TAP units.
			u32 halfWaveUnits = unitValue / 2;
			if (halfWaveUnits == 0)
				halfWaveUnits = 1;

			u64 pulseUs1 = (u64)(halfWaveUnits * unitToUs + 0.5);
			if (pulseUs1 == 0)
				pulseUs1 = 1;

			u64 pulseUs2 = (u64)((unitValue - halfWaveUnits) * unitToUs + 0.5);
			if (pulseUs2 == 0)
				pulseUs2 = 1;

			if (newPulseCount + 2 > newCapacity)
			{
				free(newTimings);
				free(dataBuffer);
				return false;
			}

			newTimings[newPulseCount++] = pulseUs1;
			newTimings[newPulseCount++] = pulseUs2;
			totalTimeUs += pulseUs1 + pulseUs2;
		}
	}
	else if (header.version == 1)
	{
		// Version 1: each byte is a full cycle unless 0x00, which indicates an extended
		// length specified in *cycles* (not TAP units) in the following 3 bytes.
		while (dataIndex < bytesRead)
		{
			bool isExtended = false;
			u32 value = 0;

			if (dataBuffer[dataIndex] == 0x00)
			{
				// Extended length (3 bytes, little-endian, in CPU cycles)
				if (dataIndex + 3 >= bytesRead)
					break;
				value = (u32)dataBuffer[dataIndex + 1] |
				        ((u32)dataBuffer[dataIndex + 2] << 8) |
				        ((u32)dataBuffer[dataIndex + 3] << 16);
				dataIndex += 4;
				isExtended = true;
			}
			else
			{
				// Normal byte in TAP units (8 cycles per unit)
				value = dataBuffer[dataIndex++];
				isExtended = false;
			}

			if (isExtended)
			{
				// Value is in CPU cycles: convert directly to microseconds and split.
				u64 cycleUs = (u64)(value * cyclesToUs + 0.5);
				if (cycleUs == 0)
					cycleUs = 1;

				u64 halfCycleUs = cycleUs / 2;
				if (halfCycleUs == 0)
					halfCycleUs = 1;

				u64 pulseUs1 = halfCycleUs;
				u64 pulseUs2 = cycleUs - halfCycleUs;
				if (pulseUs2 == 0)
					pulseUs2 = 1;

				if (newPulseCount + 2 > newCapacity)
				{
					free(newTimings);
					free(dataBuffer);
					return false;
				}

				newTimings[newPulseCount++] = pulseUs1;
				newTimings[newPulseCount++] = pulseUs2;
				totalTimeUs += pulseUs1 + pulseUs2;
			}
			else
			{
				// Normal byte: full cycle in TAP units (8 cycles per unit).
				u32 unitValue = value;
				u32 halfWaveUnits = unitValue / 2;
				if (halfWaveUnits == 0)
					halfWaveUnits = 1;

				u64 pulseUs1 = (u64)(halfWaveUnits * unitToUs + 0.5);
				if (pulseUs1 == 0)
					pulseUs1 = 1;

				u64 pulseUs2 = (u64)((unitValue - halfWaveUnits) * unitToUs + 0.5);
				if (pulseUs2 == 0)
					pulseUs2 = 1;

				if (newPulseCount + 2 > newCapacity)
				{
					free(newTimings);
					free(dataBuffer);
					return false;
				}

				newTimings[newPulseCount++] = pulseUs1;
				newTimings[newPulseCount++] = pulseUs2;
				totalTimeUs += pulseUs1 + pulseUs2;
			}
		}
	}
	else // header.version == 2
	{
		// Version 2: each byte represents a *half-wave* in TAP units.
		// If byte is 0x00, next 3 bytes are TAP units for the half-wave.
		while (dataIndex < bytesRead)
		{
			u32 unitValue = 0;

			if (dataBuffer[dataIndex] == 0x00)
			{
				// Extended length (3 bytes, little-endian, in TAP units)
				if (dataIndex + 3 >= bytesRead)
					break;
				unitValue = (u32)dataBuffer[dataIndex + 1] |
				            ((u32)dataBuffer[dataIndex + 2] << 8) |
				            ((u32)dataBuffer[dataIndex + 3] << 16);
				dataIndex += 4;
			}
			else
			{
				unitValue = dataBuffer[dataIndex++];
			}

			u64 pulseUs = (u64)(unitValue * unitToUs + 0.5);
			if (pulseUs == 0)
				pulseUs = 1;

			if (newPulseCount + 1 > newCapacity)
			{
				free(newTimings);
				free(dataBuffer);
				return false;
			}

			newTimings[newPulseCount++] = pulseUs;
			totalTimeUs += pulseUs;
		}
	}

	free(dataBuffer);

	if (newPulseCount == 0)
	{
		free(newTimings);
		return false;
	}

	FreePulseBuffer();
	pulseTimings = newTimings;
    pulseCount = newPulseCount;
	totalPlaybackTimeUs = totalTimeUs;
	return true;
}


void TapePlayer::OnMotorChange(bool motorHigh)
{
	// This is called from main loop polling
	// The IRQ handler will pick up the change on next interrupt
	motorActive = motorHigh;

	if (motorActive && loaded && !atEnd)
	{
		// Motor started - press PLAY button (active state)
		// senseLineState=true: GPIO HIGH → hardware inverts to output LOW → computer sees LOW = active
		RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_SENSE_GPIO);
		senseLineState = true;  // PLAY pressed (active)
		// CoreLoop() on core 2 will see motorActive and start/resume pulses.
	}
	else if (!motorActive)
	{
		// Motor stopped - release PLAY button (inactive state)
		// senseLineState=false: GPIO LOW → hardware inverts to output HIGH → computer sees HIGH = inactive
		RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_SENSE_GPIO);
		senseLineState = false;  // PLAY released (inactive)
	}
	// Motor state change handled; CoreLoop() uses motorActive to gate playback.
}

void TapePlayer::GetUIState(TapeUIState& state) const
{
	state.isLoaded = loaded;
	state.motorActive = motorActive;
	state.atEnd = atEnd;
	strncpy(state.filename, tapFilename, sizeof(state.filename) - 1);
	state.filename[sizeof(state.filename) - 1] = '\0';
	state.currentPulseIndex = static_cast<u32>(currentPulseIndex);  // Debug: current pulse index
	// Debug: current pulse duration (show next pulse duration, or 0 if at end or not loaded)
	// Note: currentPulseIndex points to the pulse that was just processed,
	// so we show the NEXT pulse duration (currentPulseIndex) for clarity
	if (loaded && currentPulseIndex < pulseCount && pulseTimings)
		state.currentPulseDurationUs = static_cast<u32>(pulseTimings[currentPulseIndex]);
	else
		state.currentPulseDurationUs = 0;

	// Calculate tape counter (3-digit, 000-999)
	// Counter increments approximately every 200ms of playback
	// Derived from currentPulseIndex (sum of pulse timings played so far)
	if (loaded)
	{
		u64 currentTimeMs64 = cumulativePulseTimeUs / 1000;
		u64 totalTimeMs64 = totalPlaybackTimeUs / 1000;
		u32 maxU32 = std::numeric_limits<u32>::max();
		state.currentTimeMs = (currentTimeMs64 > maxU32) ? maxU32 : static_cast<u32>(currentTimeMs64);
		state.totalTimeMs = (totalTimeMs64 > maxU32) ? maxU32 : static_cast<u32>(totalTimeMs64);
		u64 counterValue = (currentTimeMs64 / 200) % 1000;
		state.tapeCounter = static_cast<u32>(counterValue);  // 200ms per count
		
		// Calculate percentage
		if (totalTimeMs64 > 0)
		{
			// Use 64-bit to avoid overflow for long tapes
			u64 percent = (currentTimeMs64 * 100) / totalTimeMs64;
			state.percentage = (percent > 100) ? 100 : (u8)percent;
		}
		else
		{
			state.percentage = 0;
		}
	}
	else
	{
		state.tapeCounter = 0;
		state.currentTimeMs = 0;
		state.totalTimeMs = 0;
		state.percentage = 0;
	}
}

void TapePlayer::Tick10ms()
{
	// This is called periodically from main loop
	// We could update counter here, but GetUIState() calculates it on-demand
#if TAPE_MOTOR_SUPPORT
	// Main purpose is to ensure motor state is polled
	bool motorNow = (RPI_GetGpioValue((rpi_gpio_pin_t)TAPE_MOTOR_GPIO) == RPI_IO_HI);
	if (motorNow != motorActive)
	{
		OnMotorChange(motorNow);
	}
#else
	// Motor support disabled - motor is always active
	motorActive = true;
#endif
}

void TapePlayer::Reset()
{
	// Stop playback and reset tape state
	if (loaded)
	{
		currentPulseIndex = 0;
		atEnd = true;  // Set atEnd to prevent automatic restart
		cumulativePulseTimeUs = 0;  // Reset counter
	}
	
	// Set READ and SENSE to inactive state
	readLineState = true;  // Reset to HIGH (output) like initial state
	RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_READ_GPIO);  // Hardware inverts: GPIO low = output high
	// TAPE_SENSE: PLAY button released (inactive state)
	// senseLineState=false: GPIO LOW → hardware inverts to output HIGH → computer sees HIGH = inactive
	RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_SENSE_GPIO);
	senseLineState = false;  // PLAY released (inactive)
	
	// Playback remains stopped after reset; CoreLoop() will only generate pulses
	// again after a new TAP is loaded and, if applicable, the motor is started.
}

void TapePlayer::CoreLoop()
{
	// Dedicated playback loop intended to run on core 2.
	// Uses busy-waiting on ARM_SYSTIMER_CLO to generate READ pulses, independent of IRQs.

	// Ensure GPIOs are configured on this core.
	InitializeGPIO();

	while (1)
	{
		// Wait until we have a valid tape loaded
		if (!loaded || pulseTimings == nullptr || pulseCount == 0)
		{
			// Short idle to avoid hammering the bus
			for (volatile int i = 0; i < 1000; ++i) { __asm("NOP"); }
			continue;
		}

		// Motor must be active and not at end of tape
		if (!motorActive || atEnd)
		{
			for (volatile int i = 0; i < 1000; ++i) { __asm("NOP"); }
			continue;
		}

		// End-of-tape handling: hold READ high and SENSE released
		if (currentPulseIndex >= pulseCount)
		{
			if (!atEnd)
			{
				atEnd = true;
				readLineState = true;  // HIGH output
				RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_READ_GPIO);  // GPIO low -> output high
				// Release PLAY (inactive)
				RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_SENSE_GPIO);
				senseLineState = false;
			}
			for (volatile int i = 0; i < 1000; ++i) { __asm("NOP"); }
			continue;
		}

		// Get current half-wave duration
		u64 intervalUs = pulseTimings[currentPulseIndex];
		if (intervalUs == 0)
			intervalUs = 1;

		// Busy-wait using free-running system timer (1MHz)
		u32 start = read32(ARM_SYSTIMER_CLO);
		while ((u32)(read32(ARM_SYSTIMER_CLO) - start) < (u32)intervalUs)
		{
			// If tape is unloaded or motor stopped during wait, abort this pulse
			if (!loaded || !motorActive || atEnd)
				break;
		}

		if (!loaded || !motorActive || atEnd)
		{
			// State changed while waiting; re-evaluate outer loop conditions
			continue;
		}

		// Toggle READ line (hardware inverts: GPIO low = output high, GPIO high = output low)
		readLineState = !readLineState;
		if (readLineState)
			RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_READ_GPIO);  // Want high output -> set GPIO low
		else
			RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_READ_GPIO);  // Want low output -> set GPIO high

		// Update timing/debug state
		cumulativePulseTimeUs += intervalUs;
		currentPulseIndex++;
	}
}

void TapePlayer::ToggleTapeRead()
{
	// Toggle READ line state (hardware inverts: GPIO low = output high, GPIO high = output low)
	readLineState = !readLineState;
	if (readLineState)
		RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_READ_GPIO);  // Want high output -> set GPIO low
	else
		RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_READ_GPIO);  // Want low output -> set GPIO high
}

void TapePlayer::ToggleTapeSense()
{
	// Toggle SENSE line state (hardware inverts: GPIO high = low output/PLAY pressed, GPIO low = high output/PLAY released)
	senseLineState = !senseLineState;
	if (senseLineState)
		RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_SENSE_GPIO);  // Want PLAY pressed (low output) -> set GPIO high
	else
		RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_SENSE_GPIO);  // Want PLAY released (high output) -> set GPIO low
}

bool TapePlayer::GetTapeReadState() const
{
	// Return actual output state (not GPIO state)
	// Hardware inverts: GPIO low = output high, GPIO high = output low
	return readLineState;  // true = high output, false = low output
}

bool TapePlayer::GetTapeSenseState() const
{
	// Returns senseLineState: true = PLAY pressed (active), false = PLAY released (inactive)
	// When true: GPIO is HIGH, hardware inverts to output LOW, computer sees LOW = active
	// When false: GPIO is LOW, hardware inverts to output HIGH, computer sees HIGH = inactive
	return senseLineState;
}
