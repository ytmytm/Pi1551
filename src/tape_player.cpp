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

extern "C"
{
#include "rpi-aux.h"
}

// Static member definitions
u32 TapePlayer::pulseTimings[TAPE_MAX_HALFWAVES];
u32 TapePlayer::pulseCount = 0;
u32 TapePlayer::currentPulseIndex = 0;
bool TapePlayer::loaded = false;
bool TapePlayer::motorActive = false;
bool TapePlayer::atEnd = false;
bool TapePlayer::readLineState = true;  // Start with READ line high
u32 TapePlayer::totalPlaybackTimeUs = 0;
u32 TapePlayer::cumulativePulseTimeUs = 0;
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
	if (instance == this)
	{
		instance = nullptr;
	}
}

void TapePlayer::InitializeGPIO()
{
	// GPIO6 (MOTOR) - input with pull-up
	RPI_SetGpioInputPullUp((rpi_gpio_pin_t)TAPE_MOTOR_GPIO);

	// GPIO19 (READ) - output, start high
	RPI_SetGpioOutput((rpi_gpio_pin_t)TAPE_READ_GPIO);
	RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_READ_GPIO);
	readLineState = true;

	// GPIO26 (WRITE) - input with pull-up (stub, not used)
	RPI_SetGpioInputPullUp((rpi_gpio_pin_t)TAPE_WRITE_GPIO);
}

bool TapePlayer::LoadTap(const FILINFO* fileInfo)
{
	// Close any existing tape
	TeardownIRQ();
	loaded = false;
	pulseCount = 0;
	currentPulseIndex = 0;
	atEnd = false;
	motorActive = false;
	totalPlaybackTimeUs = 0;
	cumulativePulseTimeUs = 0;

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
		RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_READ_GPIO);
		SetupIRQ();
	}

	return success;
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

	// Validate version (only support v0 and v2)
	if (header.version != 0 && header.version != 2)
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
	pulseCount = 0;
	u32 dataIndex = 0;
	u32 totalTimeUs = 0;

	if (header.version == 2)
	{
		// Version 2: each byte is a half-wave, 0x00 means extended length (next 3 bytes, little-endian)
		while (dataIndex < bytesRead && pulseCount < TAPE_MAX_HALFWAVES - 1)
		{
			u32 unitValue;
			if (dataBuffer[dataIndex] == 0x00)
			{
				// Extended length
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
			u32 pulseUs = (u32)(unitValue * unitToUs + 0.5);  // Round to nearest
			if (pulseUs == 0)
				pulseUs = 1;  // Minimum 1 us

			pulseTimings[pulseCount++] = pulseUs;
			totalTimeUs += pulseUs;
		}
	}
	else
	{
		// Version 0: each byte represents a complete low-high cycle (50% duty cycle)
		// Treat as half-waves (same as v2), but if byte is 0x00, use 256 units
		while (dataIndex < bytesRead && pulseCount < TAPE_MAX_HALFWAVES - 1)
		{
			u32 unitValue = dataBuffer[dataIndex++];
			if (unitValue == 0x00)
				unitValue = 256;  // Common player behavior

			// Convert to microseconds (half-wave, so divide by 2 conceptually)
			// Actually, in v0 each byte is the full cycle, so we need two half-waves
			u32 halfWaveUnits = unitValue / 2;
			if (halfWaveUnits == 0)
				halfWaveUnits = 1;

			u32 pulseUs1 = (u32)(halfWaveUnits * unitToUs + 0.5);
			if (pulseUs1 == 0)
				pulseUs1 = 1;

			u32 pulseUs2 = (u32)((unitValue - halfWaveUnits) * unitToUs + 0.5);
			if (pulseUs2 == 0)
				pulseUs2 = 1;

			if (pulseCount < TAPE_MAX_HALFWAVES - 2)
			{
				pulseTimings[pulseCount++] = pulseUs1;
				pulseTimings[pulseCount++] = pulseUs2;
				totalTimeUs += pulseUs1 + pulseUs2;
			}
			else
				break;
		}
	}

	free(dataBuffer);

	if (pulseCount == 0)
		return false;

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

	// Check motor state (poll GPIO)
	bool motorNow = (RPI_GetGpioValue((rpi_gpio_pin_t)TAPE_MOTOR_GPIO) == RPI_IO_HI);
	
	if (motorNow != motorActive)
	{
		motorActive = motorNow;
		if (!motorActive)
		{
			// Motor stopped - hold current state (pulse time already tracked)
			// Schedule a short recheck
			u32 now = read32(ARM_SYSTIMER_CLO);
			write32(ARM_SYSTIMER_C1, now + 1000);  // Recheck in 1ms
			DataMemBarrier();
			return;
		}
		// Motor started - resume playback from current position
		// Don't reset currentPulseIndex - continue from where we left off
		// If we're at the end, stay at end
	}

	if (!motorActive || atEnd || !loaded)
	{
		// Motor off or at end - schedule short recheck
		u32 now = read32(ARM_SYSTIMER_CLO);
		write32(ARM_SYSTIMER_C1, now + 1000);  // Recheck in 1ms
		DataMemBarrier();
		return;
	}

	// Motor is active and we have data - toggle READ line and schedule next pulse
	if (currentPulseIndex < pulseCount)
	{
		// Toggle READ line
		readLineState = !readLineState;
		if (readLineState)
			RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_READ_GPIO);
		else
			RPI_SetGpioLo((rpi_gpio_pin_t)TAPE_READ_GPIO);

		// Update cumulative pulse time (before incrementing index)
		cumulativePulseTimeUs += pulseTimings[currentPulseIndex];

		// Schedule next pulse
		u32 now = read32(ARM_SYSTIMER_CLO);
		u32 nextTime = now + pulseTimings[currentPulseIndex];
		write32(ARM_SYSTIMER_C1, nextTime);

		currentPulseIndex++;
	}
	else
	{
		// End of tape - hold READ high
		atEnd = true;
		readLineState = true;
		RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_READ_GPIO);
		
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
		// Resume playback from current position
		// Trigger next pulse shortly (if we have pulses remaining)
		if (currentPulseIndex < pulseCount)
		{
			u32 now = read32(ARM_SYSTIMER_CLO);
			write32(ARM_SYSTIMER_C1, now + 100);  // Start in 100us
			DataMemBarrier();
		}
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
		state.currentTimeMs = cumulativePulseTimeUs / 1000;
		state.totalTimeMs = totalPlaybackTimeUs / 1000;
		state.tapeCounter = (state.currentTimeMs / 200) % 1000;  // 200ms per count
		
		// Calculate percentage
		if (state.totalTimeMs > 0)
		{
			// Use 64-bit to avoid overflow for long tapes
			u64 percent = ((u64)state.currentTimeMs * 100) / (u64)state.totalTimeMs;
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
	// Main purpose is to ensure motor state is polled
	bool motorNow = (RPI_GetGpioValue((rpi_gpio_pin_t)TAPE_MOTOR_GPIO) == RPI_IO_HI);
	if (motorNow != motorActive)
	{
		OnMotorChange(motorNow);
	}
}

void TapePlayer::Reset()
{
	if (!loaded)
		return;

	currentPulseIndex = 0;
	atEnd = false;
	readLineState = true;
	RPI_SetGpioHi((rpi_gpio_pin_t)TAPE_READ_GPIO);
	cumulativePulseTimeUs = 0;  // Reset counter
	
	if (motorActive)
	{
		u32 now = read32(ARM_SYSTIMER_CLO);
		write32(ARM_SYSTIMER_C1, now + 100);  // Start in 100us
		DataMemBarrier();
	}
}

