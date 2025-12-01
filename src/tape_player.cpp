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
#include "interrupt.h"
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
bool TapePlayer::readLineState = true;  // Start with READ line high (hardware inverts GPIO signal)
// SENSE line state: true = PLAY pressed (active), false = PLAY released (inactive)
// When true: GPIO HIGH → hardware inverts to output LOW → computer sees LOW = active
// When false: GPIO LOW → hardware inverts to output HIGH → computer sees HIGH = inactive
bool TapePlayer::senseLineState = false;  // Start with PLAY released (inactive)
u64 TapePlayer::totalPlaybackTimeUs = 0;
u64 TapePlayer::cumulativePulseTimeUs = 0;
char TapePlayer::tapFilename[256] = {0};
TapePlayer* TapePlayer::instance = nullptr;

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

// Plus/4 CPU frequencies (Hz)
// PAL: ~1.76 MHz, NTSC: ~2.0 MHz
// But TAP format uses tape clock, not CPU clock
// For Plus/4, the tape clock is approximately:
// PAL: 886,724 Hz, NTSC: 1,022,727 Hz
#define TAPE_CLOCK_PAL_HZ  886724
#define TAPE_CLOCK_NTSC_HZ 1022727

// TAP format: 1 unit = 8 CPU cycles / frequency
// So pulse_length_us = unit_value * (8 / frequency) * 1,000,000
// For microseconds: pulse_length_us = unit_value * (8,000,000 / frequency)
#define TAP_UNIT_TO_US_PAL  (8000000.0 / TAPE_CLOCK_PAL_HZ)   // ~9.024 us per unit
#define TAP_UNIT_TO_US_NTSC (8000000.0 / TAPE_CLOCK_NTSC_HZ)   // ~7.822 us per unit

TapePlayer::TapePlayer()
{
	if (instance == nullptr)
	{
		instance = this;
		InitializeGPIO();
	}
}

TapePlayer::~TapePlayer()
{
	TeardownIRQ();
    FreePulseBuffer();
	if (instance == this)
	{
		instance = nullptr;
	}
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
	TeardownIRQ();
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
		readLineState = true;
		RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_READ_GPIO);  // Hardware inverts: GPIO low = output high
		// TAPE_SENSE: PLAY button pressed (active state) when TAP is loaded
		// senseLineState=true: GPIO HIGH → hardware inverts to output LOW → computer sees LOW = active
		RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_SENSE_GPIO);
		senseLineState = true;  // PLAY pressed (active)
		SetupIRQ();
		
		// Start playback immediately if motor is active (or if motor support is disabled)
#if TAPE_MOTOR_SUPPORT
		if (motorActive && pulseTimings && pulseCount > 0)
#else
		if (pulseTimings && pulseCount > 0)
#endif
		{
			u32 now = read32(ARM_SYSTIMER_CLO);
			write32(ARM_SYSTIMER_C1, now + 100);  // Start in 100us
			DataMemBarrier();
		}
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

	// Validate version (support v0, v1, and v2)
	// v0: each byte is a complete cycle (low-high)
	// v1: same as v2, each byte is a half-wave, 0x00 means extended length
	// v2: each byte is a half-wave, 0x00 means extended length
	if (header.version != 0 && header.version != 1 && header.version != 2)
		return false;

	// Validate machine (0x02 = Plus/4/C16)
	if (header.machine != 0x02)
		return false;

	// Determine timing based on video standard
	// 0 = PAL, 1 = NTSC
	double unitToUs = (header.video == 1) ? TAP_UNIT_TO_US_NTSC : TAP_UNIT_TO_US_PAL;

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

	// Parse data based on version
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

	if (header.version == 2)
	{
		// Version 2: each byte is a half-wave, 0x00 means extended length (next 3 bytes, little-endian, in TAP units)
		while (dataIndex < bytesRead)
		{
			u32 unitValue;
			if (dataBuffer[dataIndex] == 0x00)
			{
				// Extended length (in TAP units)
				if (dataIndex + 3 >= bytesRead)
					break;
				unitValue = dataBuffer[dataIndex + 1] | 
				           (dataBuffer[dataIndex + 2] << 8) | 
				           (dataBuffer[dataIndex + 3] << 16);
				dataIndex += 4;
			}
			else
			{
				unitValue = dataBuffer[dataIndex];
				dataIndex++;
			}

			// Convert to microseconds
			u64 pulseUs = (u64)(unitValue * unitToUs + 0.5);  // Round to nearest
			if (pulseUs == 0)
				pulseUs = 1;  // Minimum 1 us
            if (newPulseCount >= newCapacity)
            {
                free(newTimings);
                free(dataBuffer);
                return false;
            }
			newTimings[newPulseCount++] = pulseUs;
			totalTimeUs += pulseUs;
		}
	}
	else if (header.version == 0)
	{
		// Version 0: each byte represents a complete low-high cycle (50% duty cycle)
		// If byte is 0x00, use 256 units (overflow condition)
		while (dataIndex < bytesRead)
		{
			u32 unitValue = dataBuffer[dataIndex++];
			if (unitValue == 0x00)
				unitValue = 256;  // Overflow condition in v0

			// Convert to microseconds (half-wave, so divide by 2 conceptually)
			// Actually, in v0 each byte is the full cycle, so we need two half-waves
			u32 halfWaveUnits = unitValue / 2;
			if (halfWaveUnits == 0)
				halfWaveUnits = 1;

			u64 pulseUs1 = (u64)(halfWaveUnits * unitToUs + 0.5);
			if (pulseUs1 == 0)
				pulseUs1 = 1;

			u64 pulseUs2 = (u64)((unitValue - halfWaveUnits) * unitToUs + 0.5);
			if (pulseUs2 == 0)
				pulseUs2 = 1;

            if (newPulseCount >= newCapacity)
            {
                free(newTimings);
                free(dataBuffer);
                return false;
            }
            newTimings[newPulseCount++] = pulseUs1;

            if (newPulseCount >= newCapacity)
            {
                free(newTimings);
                free(dataBuffer);
                return false;
            }
            newTimings[newPulseCount++] = pulseUs2;
			totalTimeUs += pulseUs1 + pulseUs2;
		}
	}
	else  // header.version == 1
	{
		// Version 1: each byte represents a complete low-high cycle (50% duty cycle)
		// If byte is 0x00, next 3 bytes are extended length (in CYCLES, not TAP units!)
		// Cycles need to be converted: cycles_to_us = cycles / frequency * 1,000,000
		double cyclesToUs = (header.video == 1) ? (1000000.0 / TAPE_CLOCK_NTSC_HZ) : (1000000.0 / TAPE_CLOCK_PAL_HZ);
		
		while (dataIndex < bytesRead)
		{
			u32 unitValue;
			bool isExtended = false;
			
			if (dataBuffer[dataIndex] == 0x00)
			{
				// Extended length (3 bytes, little-endian, in CYCLES, not TAP units!)
				if (dataIndex + 3 >= bytesRead)
					break;
				unitValue = dataBuffer[dataIndex + 1] | 
				           (dataBuffer[dataIndex + 2] << 8) | 
				           (dataBuffer[dataIndex + 3] << 16);
				dataIndex += 4;
				isExtended = true;
			}
			else
			{
				unitValue = dataBuffer[dataIndex++];
				isExtended = false;
			}

			if (isExtended)
			{
				// Extended length is in cycles - convert directly to microseconds
				// This is a full cycle, so we need two half-waves
				u64 cycleUs = (u64)(unitValue * cyclesToUs + 0.5);
				if (cycleUs == 0)
					cycleUs = 1;
				
				u64 halfCycleUs = cycleUs / 2;
				if (halfCycleUs == 0)
					halfCycleUs = 1;
				
				u64 pulseUs1 = halfCycleUs;
				u64 pulseUs2 = cycleUs - halfCycleUs;
				if (pulseUs2 == 0)
					pulseUs2 = 1;

				if (newPulseCount >= newCapacity)
				{
					free(newTimings);
					free(dataBuffer);
					return false;
				}
				newTimings[newPulseCount++] = pulseUs1;

				if (newPulseCount >= newCapacity)
				{
					free(newTimings);
					free(dataBuffer);
					return false;
				}
				newTimings[newPulseCount++] = pulseUs2;
				totalTimeUs += pulseUs1 + pulseUs2;
			}
			else
			{
				// Normal byte is in TAP units (8 cycles per unit)
				// Convert to microseconds (half-wave, so divide by 2 conceptually)
				// Actually, in v1 each byte is the full cycle, so we need two half-waves
				u32 halfWaveUnits = unitValue / 2;
				if (halfWaveUnits == 0)
					halfWaveUnits = 1;

				u64 pulseUs1 = (u64)(halfWaveUnits * unitToUs + 0.5);
				if (pulseUs1 == 0)
					pulseUs1 = 1;

				u64 pulseUs2 = (u64)((unitValue - halfWaveUnits) * unitToUs + 0.5);
				if (pulseUs2 == 0)
					pulseUs2 = 1;

				if (newPulseCount >= newCapacity)
				{
					free(newTimings);
					free(dataBuffer);
					return false;
				}
				newTimings[newPulseCount++] = pulseUs1;

				if (newPulseCount >= newCapacity)
				{
					free(newTimings);
					free(dataBuffer);
					return false;
				}
				newTimings[newPulseCount++] = pulseUs2;
				totalTimeUs += pulseUs1 + pulseUs2;
			}
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

void TapePlayer::SetupIRQ()
{
	// Connect TIMER1 IRQ handler
	InterruptSystemConnectIRQ(ARM_IRQ_TIMER1, TapeIRQHandler, this);
	InterruptSystemEnableIRQ(ARM_IRQ_TIMER1);
}

void TapePlayer::TeardownIRQ()
{
	InterruptSystemDisableIRQ(ARM_IRQ_TIMER1);
	InterruptSystemDisconnectIRQ(ARM_IRQ_TIMER1);
}

void TapePlayer::TapeIRQHandler(void* pParam)
{
	TapePlayer* player = (TapePlayer*)pParam;
	if (player && player->instance == player)
	{
		player->HandleTapeIRQ();
	}
}

void TapePlayer::HandleTapeIRQ()
{
	DataMemBarrier();

	// Clear interrupt flag
	write32(ARM_SYSTIMER_CS, 1 << 1);  // Clear TIMER1 interrupt

#if TAPE_MOTOR_SUPPORT
	// Check motor state (poll GPIO)
	bool motorNow = (RPI_GetGpioValue((rpi_gpio_pin_t)TAPE_MOTOR_GPIO) == RPI_IO_HI);
	
		if (motorNow != motorActive)
		{
			motorActive = motorNow;
			if (!motorActive)
			{
			// Motor stopped - release PLAY button (inactive state)
			// senseLineState=false: GPIO LOW → hardware inverts to output HIGH → computer sees HIGH = inactive
			RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_SENSE_GPIO);
			senseLineState = false;  // PLAY released (inactive)
			// Motor stopped - hold current state (pulse time already tracked)
				// Schedule a short recheck
				u32 now = read32(ARM_SYSTIMER_CLO);
				write32(ARM_SYSTIMER_C1, now + 1000);  // Recheck in 1ms
				DataMemBarrier();
				return;
			}
			// Motor started - press PLAY button (active state)
			// senseLineState=true: GPIO HIGH → hardware inverts to output LOW → computer sees LOW = active
			RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_SENSE_GPIO);
			senseLineState = true;  // PLAY pressed (active)
			// Motor started - resume playback from current position
			// Don't reset currentPulseIndex - continue from where we left off
			// If we're at the end, stay at end
		}
#else
	// Motor support disabled - motor is always active
	motorActive = true;
	// When motor is always active, ensure SENSE is set to PLAY pressed when playback starts
	// senseLineState=true: GPIO HIGH → hardware inverts to output LOW → computer sees LOW = active
	if (loaded && !atEnd && !senseLineState)
	{
		RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_SENSE_GPIO);
		senseLineState = true;  // PLAY pressed (active)
	}
#endif

	if (!motorActive || atEnd || !loaded)
	{
		// Motor off or at end - schedule short recheck
		u32 now = read32(ARM_SYSTIMER_CLO);
		write32(ARM_SYSTIMER_C1, now + 1000);  // Recheck in 1ms
		DataMemBarrier();
		return;
	}

	// Motor is active and we have data - toggle READ line and schedule next pulse
	if (pulseTimings && currentPulseIndex < pulseCount)
	{
		// Toggle READ line (hardware inverts: GPIO low = output high, GPIO high = output low)
		readLineState = !readLineState;
		if (readLineState)
			RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_READ_GPIO);  // Want high output -> set GPIO low
		else
			RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_READ_GPIO);  // Want low output -> set GPIO high

		// Update cumulative pulse time (before incrementing index)
		cumulativePulseTimeUs += pulseTimings[currentPulseIndex];

		// Schedule next pulse
		u32 now = read32(ARM_SYSTIMER_CLO);
		u64 interval64 = pulseTimings[currentPulseIndex];
		
		// Handle very long intervals (overflow protection)
		// Timer is 32-bit, so max interval is ~4294 seconds
		// For intervals longer than 1 second, cap to 1 second to prevent overflow
		// This may cause slight timing inaccuracies for very long pulses, but prevents crashes
		const u32 MAX_SAFE_INTERVAL = 1000000;  // 1 second max per timer interrupt
		u32 nextInterval;
		if (interval64 > MAX_SAFE_INTERVAL)
		{
			// Cap to 1 second - very long pulses are rare and this prevents timer overflow
			nextInterval = MAX_SAFE_INTERVAL;
		}
		else
		{
			nextInterval = static_cast<u32>(interval64);
		}
		
		u32 nextTime = now + nextInterval;
		write32(ARM_SYSTIMER_C1, nextTime);

		currentPulseIndex++;
	}
	else
	{
		// End of tape - hold READ high (hardware inverts: GPIO low = output high)
		atEnd = true;
		readLineState = true;
		RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_READ_GPIO);  // Hardware inverts: GPIO low = output high
		// TAPE_SENSE: PLAY button released (inactive state) when TAP ends
		// senseLineState=false: GPIO LOW → hardware inverts to output HIGH → computer sees HIGH = inactive
		RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_SENSE_GPIO);
		
		// Schedule periodic recheck
		u32 now = read32(ARM_SYSTIMER_CLO);
		write32(ARM_SYSTIMER_C1, now + 1000);  // Recheck in 1ms
	}

	DataMemBarrier();
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
		// Resume playback from current position
		// Trigger next pulse shortly (if we have pulses remaining)
		if (pulseTimings && currentPulseIndex < pulseCount)
		{
			u32 now = read32(ARM_SYSTIMER_CLO);
			write32(ARM_SYSTIMER_C1, now + 100);  // Start in 100us
			DataMemBarrier();
		}
	}
	else if (!motorActive)
	{
		// Motor stopped - release PLAY button (inactive state)
		// senseLineState=false: GPIO LOW → hardware inverts to output HIGH → computer sees HIGH = inactive
		RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_SENSE_GPIO);
		senseLineState = false;  // PLAY released (inactive)
	}
	// Motor state change handled - IRQ will pick it up
}

void TapePlayer::GetUIState(TapeUIState& state) const
{
	state.isLoaded = loaded;
	state.motorActive = motorActive;
	state.atEnd = atEnd;
	strncpy(state.filename, tapFilename, sizeof(state.filename) - 1);
	state.filename[sizeof(state.filename) - 1] = '\0';

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
	TeardownIRQ();
	
	// Cancel any pending timer interrupt by setting it far in the future
	// This prevents HandleTapeIRQ from being called after reset
	u32 farFuture = read32(ARM_SYSTIMER_CLO) + 0x7FFFFFFF;  // Set to far future
	write32(ARM_SYSTIMER_C1, farFuture);
	DataMemBarrier();
	
	// Clear any pending interrupt
	write32(ARM_SYSTIMER_CS, 1 << 1);  // Clear TIMER1 interrupt
	
	if (loaded)
	{
		currentPulseIndex = 0;
		atEnd = true;  // Set atEnd to prevent automatic restart
		cumulativePulseTimeUs = 0;  // Reset counter
	}
	
	// Set READ and SENSE to inactive state
	readLineState = true;
	RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_READ_GPIO);  // Hardware inverts: GPIO low = output high
	// TAPE_SENSE: PLAY button released (inactive state)
	// senseLineState=false: GPIO LOW → hardware inverts to output HIGH → computer sees HIGH = inactive
	RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_SENSE_GPIO);
	senseLineState = false;  // PLAY released (inactive)
	
	// IMPORTANT: Do NOT call SetupIRQ() here - playback should remain stopped
	// Don't restart playback automatically - wait for motor to start (if motor support enabled)
	// or for user to manually start playback (by loading a new TAP file)
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
