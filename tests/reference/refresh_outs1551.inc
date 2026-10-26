// Pi1551 - TCBM bus GPIO interface
// Copyright(C) 2024-2026 Maciej Witkowiak
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

// Frozen GPIO output implementation before the byte-only fast path.
void TCBM_Bus::ReferenceRefreshOuts1551(void)
{
	unsigned set = 0;
	unsigned clear = 0;

	if (TPI && port)
	{
		// port A (tightened loop)
		u8 dir = port->GetDirection();
		u8 out = port->GetOutput();
		IOPort* portC = TPI->GetPortC();
		u8 ddrC = portC->GetDirection();
		u8 pc = portC->GetOutput();
		// Early exit only for TPI port state; LED/buzzer are handled below.
		if (dir == lastDirA && out == lastOutA && ddrC == lastDirC && pc == lastOutC)
			goto push_led_sound;
		u8 changed = dir ^ lastDirA;
		static const u8 masksA[8] = { 0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80 };
		static const rpi_gpio_pin_t pinsA[8] = {
			(rpi_gpio_pin_t)PIGPIO_DIO1,(rpi_gpio_pin_t)PIGPIO_DIO2,(rpi_gpio_pin_t)PIGPIO_DIO3,(rpi_gpio_pin_t)PIGPIO_DIO4,
			(rpi_gpio_pin_t)PIGPIO_DIO5,(rpi_gpio_pin_t)PIGPIO_DIO6,(rpi_gpio_pin_t)PIGPIO_DIO7,(rpi_gpio_pin_t)PIGPIO_DIO8
		};
		static const u32 gpioMasksA[8] = {
			PIGPIO_MASK_DIO1,PIGPIO_MASK_DIO2,PIGPIO_MASK_DIO3,PIGPIO_MASK_DIO4,
			PIGPIO_MASK_DIO5,PIGPIO_MASK_DIO6,PIGPIO_MASK_DIO7,PIGPIO_MASK_DIO8
		};
		for (int i = 0; i < (int)(sizeof(masksA) / sizeof(masksA[0])); ++i)
		{
			u8 m = masksA[i];
			if (changed & m)
			{
				if (dir & m)
					RPI_SetGpioPinFunction(pinsA[i], FS_OUTPUT);
				else
					RPI_SetGpioInputPullUp(pinsA[i]);  // pull-up so when Plus/4 (tcbm2sd) releases bus we read $FF
			}
			if (dir & m)
			{
				if (out & m) set |= gpioMasksA[i]; else clear |= gpioMasksA[i];
			}
		}
		lastDirA = dir;

		// Port C (1551 TIA): PC3..0 default to outputs (ACK, DEV, STATUS1, STATUS0) but emulated code may reprogram DDRC.
		// Honour DDRC and only drive pins configured as outputs; set GPIO function only when DDR changes.
		u8 changedC = ddrC ^ lastDirC;
		static const u8 masksC[5] = { 0x01,0x02,0x04,0x08,0x80 };
		static const rpi_gpio_pin_t pinsC[5] = {
			(rpi_gpio_pin_t)PIGPIO_OUT_STATUS0,(rpi_gpio_pin_t)PIGPIO_OUT_STATUS1,(rpi_gpio_pin_t)PIGPIO_OUT_DEV,(rpi_gpio_pin_t)PIGPIO_OUT_ACK,(rpi_gpio_pin_t)PIGPIO_IN_DAV
		};
		static const u32 gpioMasksC[5] = {
			PIGPIO_MASK_OUT_STATUS0,PIGPIO_MASK_OUT_STATUS1,PIGPIO_MASK_OUT_DEV,PIGPIO_MASK_OUT_ACK,PIGPIO_MASK_IN_DAV
		};
		for (int i = 0; i < (int)(sizeof(masksC) / sizeof(masksC[0])); ++i)
		{
			u8 m = masksC[i];
			if (changedC & m)
			{
				if (ddrC & m)
					RPI_SetGpioPinFunction(pinsC[i], FS_OUTPUT);
				else
					RPI_SetGpioInputPullUp(pinsC[i]);  // pull-up when input so released line reads high
			}
			if (ddrC & m)
			{
				if (pc & m) set |= gpioMasksC[i]; else clear |= gpioMasksC[i];
			}
		}
		lastDirC = ddrC;
		lastOutA = out;
		lastOutC = pc;

		// Avoid redundant TPI writes; also skip zero writes but keep cache correct
		if (clear != lastClear) {
			if (clear) write32(ARM_GPIO_GPCLR0, clear);
			lastClear = clear;
		}
		if (set != lastSet) {
			if (set) write32(ARM_GPIO_GPSET0, set);
			lastSet = set;
		}
	}

push_led_sound:
	// LED and buzzer rarely change, but this function runs multiple times per
	// emulated microsecond. Only touch their GPIO pins on a state transition.
	unsigned ledSoundClear = 0;
	unsigned ledSoundSet = 0;
	if (!ledSoundOutputValid || OutputLED != lastOutputLED)
	{
		if (OutputLED) ledSoundSet |= 1u << PIGPIO_OUT_LED;
		else ledSoundClear |= 1u << PIGPIO_OUT_LED;
		lastOutputLED = OutputLED;
	}
	// Runtime buzzer output is fixed to the board's SOUND pin (GPIO13).
	bool soundGpioEnabled = options.SoundEnabled() && options.SoundOnGPIO();
	if (soundGpioEnabled && (!ledSoundOutputValid || !lastSoundGpioEnabled || OutputSound != lastOutputSound))
	{
		if (OutputSound) ledSoundSet |= 1u << PIGPIO_OUT_SOUND;
		else ledSoundClear |= 1u << PIGPIO_OUT_SOUND;
		lastOutputSound = OutputSound;
	}
	lastSoundGpioEnabled = soundGpioEnabled;
	ledSoundOutputValid = true;
	if (ledSoundClear) write32(ARM_GPIO_GPCLR0, ledSoundClear);
	if (ledSoundSet) write32(ARM_GPIO_GPSET0, ledSoundSet);
}
