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

#ifndef TCBM_BUS_H
#define TCBM_BUS_H

#include "defs.h"
#include "debug.h"
#include "m6523.h"

extern "C"
{
#include "rpi-gpio.h"
}

//ROTARY: Added for rotary encoder support - 09/05/2019 by Geo...
#include "dmRotary.h"

#define INPUT_BUTTON_DEBOUNCE_THRESHOLD 20000
#define INPUT_BUTTON_REPEAT_THRESHOLD 460000

/* moved to variables in InputMapping
#define INPUT_BUTTON_ENTER 0
#define INPUT_BUTTON_UP 1
#define INPUT_BUTTON_DOWN 2
#define INPUT_BUTTON_BACK 3
#define INPUT_BUTTON_INSERT 4
*/

// DIN ATN is inverted and then connected to pb7 and ca1
//	- also input to xor with ATNAout pb4
//		- output of xor is inverted and connected to DIN pin 5 DATAout (OC so can only pull low)
// VIA ATNin pb7 input to xor
// VIA ATNin ca1 output from inverted DIN 3 ATN
// DIN DATA is inverted and then connected to pb1
// VIA DATAin pb0 output from inverted DIN 5 DATA
// VIA DATAout pb1 inverted and then connected to DIN DATA
// DIN CLK is inverted and then connected to pb2
// VIA CLKin pb2 output from inverted DIN 4 CLK
// VIA CLKout pb3 inverted and then connected to DIN CLK

// $1800
// PB 0		data in
// PB 1		data out
// PB 2		clock in
// PB 3		clock out
// PB 4		ATNA
// PB 5,6	device address
// PB 7,CA1	ATN IN

// If ATN and ATNA are out of sync
//	- VIA's DATA IN will automatically set high and hence the DATA line will be pulled low (activated)
// If ATN and ATNA are in sync
//	- the output from the XOR gate will be low and the output of its inverter will go high
//	- when this occurs the DATA line must be still able to be pulled low via the PC or VIA's inverted PB1 (DATA OUT)
//
// Therefore in the same vein if PB7 is set to output it could cause the input of the XOR to be pulled low
//

// NOTE ABOUT SRQ
// SRQ is a little bit different.
// The 1581 does not pull it high. Only the 128 pulls it high.
// 
#if defined(HAS_40PINS)
enum PIGPIO
{
	PIGPIO_DIO1 = 14,
	PIGPIO_DIO2 = 15,
	PIGPIO_DIO3 = 18,
	PIGPIO_DIO4 = 23,
	PIGPIO_DIO5 = 24,
	PIGPIO_DIO6 = 25,
	PIGPIO_DIO7 = 8,
	PIGPIO_DIO8 = 7,

	PIGPIO_IN_DAV = 20, // not 12! tcbm2sd ACK output (13) goes to drive DAV input (here)
	PIGPIO_OUT_STATUS0 = 16,
	PIGPIO_OUT_STATUS1 = 21,
	PIGPIO_OUT_ACK = 12, // not 20! tcbm2sd DAV input (11) goes to drive ACK output (here)

	PIGPIO_IN_BUTTON4 = 4,
	PIGPIO_IN_BUTTON3 = 17,
	PIGPIO_IN_BUTTON1 = 27,
	PIGPIO_IN_BUTTON2 = 22,
	PIGPIO_OUT_LED = 10, // was 16
	PIGPIO_OUT_DEV = 9,
	PIGPIO_IN_RESET = 11,
	PIGPIO_IN_BUTTON5 = 5,
	PIGPIO_OUT_SOUND = 13
};
#else
#error "Raspberry Pi 1B Rev 1/2 not supported (only 26 I/O ports)"
#endif

enum PIGPIOMasks
{
    PIGPIO_MASK_DIO1 = 1u << PIGPIO_DIO1,
    PIGPIO_MASK_DIO2 = 1u << PIGPIO_DIO2,
    PIGPIO_MASK_DIO3 = 1u << PIGPIO_DIO3,
    PIGPIO_MASK_DIO4 = 1u << PIGPIO_DIO4,
    PIGPIO_MASK_DIO5 = 1u << PIGPIO_DIO5,
    PIGPIO_MASK_DIO6 = 1u << PIGPIO_DIO6,
    PIGPIO_MASK_DIO7 = 1u << PIGPIO_DIO7,
    PIGPIO_MASK_DIO8 = 1u << PIGPIO_DIO8,
	PIGPIO_MASK_ANY_DIO = PIGPIO_MASK_DIO1 | PIGPIO_MASK_DIO2 | PIGPIO_MASK_DIO3 | PIGPIO_MASK_DIO4 | PIGPIO_MASK_DIO5 | PIGPIO_MASK_DIO6 | PIGPIO_MASK_DIO7 | PIGPIO_MASK_DIO8,
    PIGPIO_MASK_IN_DAV = 1u << PIGPIO_IN_DAV,
    PIGPIO_MASK_OUT_STATUS0 = 1u << PIGPIO_OUT_STATUS0,
    PIGPIO_MASK_OUT_STATUS1 = 1u << PIGPIO_OUT_STATUS1,
    PIGPIO_MASK_OUT_ACK = 1u << PIGPIO_OUT_ACK,
	PIGPIO_MASK_OUT_DEV = 1u << PIGPIO_OUT_DEV,
    PIGPIO_MASK_IN_RESET = 1u << PIGPIO_IN_RESET,

    PIGPIO_MASK_IN_BUTTON1 = 1u << PIGPIO_IN_BUTTON1,
    PIGPIO_MASK_IN_BUTTON2 = 1u << PIGPIO_IN_BUTTON2,
    PIGPIO_MASK_IN_BUTTON3 = 1u << PIGPIO_IN_BUTTON3,
    PIGPIO_MASK_IN_BUTTON4 = 1u << PIGPIO_IN_BUTTON4,
    PIGPIO_MASK_IN_BUTTON5 = 1u << PIGPIO_IN_BUTTON5,
	PIGPIO_MASK_ANY_BUTTON = PIGPIO_MASK_IN_BUTTON1 | PIGPIO_MASK_IN_BUTTON2 | PIGPIO_MASK_IN_BUTTON3 | PIGPIO_MASK_IN_BUTTON4 | PIGPIO_MASK_IN_BUTTON5
};

static const unsigned ButtonPinFlags[5] = { PIGPIO_MASK_IN_BUTTON1, PIGPIO_MASK_IN_BUTTON2, PIGPIO_MASK_IN_BUTTON3, PIGPIO_MASK_IN_BUTTON4, PIGPIO_MASK_IN_BUTTON5 };

//GPFSEL0
//  S   S   S                    
//  P   P   P               I   I
//  I   I   I               2   2
//  0   0   0               C   C
//  M   C   C               S   S   I   I
//  I   E   E               C   D   D   D
//  S   0   1   W   R   R   L   A   S   S
//  O           RST B5  B4  1   1   D   C
//	9	8	7	6	5	4	3	2	1	0
//GPFSEL1
//                  RX  TX          S   S
//                  RX  TX          P   P
//                  RX  TX          I   I
//                  RX  TX          0   0
//                  RX  TX              M
//                  RX  TX          C   O
//  W   W   W   W   RX  TX  W   W   L   S
//  SRQ DTA CLK LED RX  TX  SND ATN K   I
//	19	18	17	16	15	14	13	12	11	10
//GPFSEL2
//  X   X                           
//  X   X                           
//  X   X                           
//  X   X                           
//  X   X                           
//  X   X                           
//  X   X   R   R   R   R    R   R  R   R 
//  X   X   B1  CLK DTA ATN B3  B2  SRQ RST  
//	29	28	27	26	25	24	23	22	21	20
// 000 000 000 000 000 000 000 000 000 000
///////////////////////////////////////////////////////////////////////////////////////////////

class TCBM_Bus
{
public:
	static inline void Initialise(void)
	{
		volatile int index; // Force a real delay in the loop below.

		// Clear all outputs to 0
		write32(ARM_GPIO_GPCLR0, 0xFFFFFFFF);

		SetDIODirectionInput(true);
//		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_IN_DAV, FS_INPUT);
		RPI_SetGpioInputPullUp((rpi_gpio_pin_t)PIGPIO_IN_DAV);
		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_OUT_STATUS0, FS_OUTPUT);
		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_OUT_STATUS1, FS_OUTPUT);
		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_OUT_ACK, FS_OUTPUT);
//		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_IN_RESET, FS_INPUT);
		RPI_SetGpioInputPullUp((rpi_gpio_pin_t)PIGPIO_IN_RESET);
		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_OUT_DEV, FS_OUTPUT);

		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_IN_BUTTON4, FS_INPUT);
		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_IN_BUTTON5, FS_INPUT);
		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_IN_BUTTON2, FS_INPUT);
		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_IN_BUTTON3, FS_INPUT);
		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_IN_BUTTON1, FS_INPUT);
		
		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_OUT_SOUND, FS_OUTPUT);
		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_OUT_LED, FS_OUTPUT);
	
#if not defined(EXPERIMENTALZERO)
		// Set up audio.
		write32(CM_PWMDIV, CM_PASSWORD + 0x2000);
		write32(CM_PWMCTL, CM_PASSWORD + CM_ENAB + CM_SRC_OSCILLATOR);	// Use Default 100MHz Clock
		// Set PWM
		write32(PWM_RNG1, 0x1B4);	// 8bit 44100Hz Mono
		write32(PWM_RNG2, 0x1B4);
		write32(PWM_CTL, PWM_USEF2 + PWM_PWEN2 + PWM_USEF1 + PWM_PWEN1 + PWM_CLRF1);
#endif

		for (index = 0; index < buttonCount; ++index)
		{
			InputButton[index] = false;
			InputButtonPrev[index] = false;
			validInputCount[index] = 0;
			inputRepeatThreshold[index] = INPUT_BUTTON_REPEAT_THRESHOLD;
			inputRepeat[index] = 0;
			inputRepeatPrev[index] = 0;
		}

		// Enable the internal pullups for the input button pins using the method described in BCM2835-ARM-Peripherals manual.
		RPI_SetGpioInputPullUp((rpi_gpio_pin_t)PIGPIO_IN_BUTTON1);
		RPI_SetGpioInputPullUp((rpi_gpio_pin_t)PIGPIO_IN_BUTTON2);
		RPI_SetGpioInputPullUp((rpi_gpio_pin_t)PIGPIO_IN_BUTTON3);
		RPI_SetGpioInputPullUp((rpi_gpio_pin_t)PIGPIO_IN_BUTTON4);
		RPI_SetGpioInputPullUp((rpi_gpio_pin_t)PIGPIO_IN_BUTTON5);

		//ROTARY: Added for rotary encoder support - 09/05/2019 by Geo...
		if (TCBM_Bus::rotaryEncoderEnable == true)
		{
			//ROTARY: Added for rotary encoder inversion (Issue#185) - 08/13/2020 by Geo...
			if (TCBM_Bus::rotaryEncoderInvert == true)
			{ // button 2 / 3 / 1 (A=sw2, B=sw3, SW=sw1)
				TCBM_Bus::rotaryEncoder.Initialize((rpi_gpio_pin_t)PIGPIO_IN_BUTTON2, (rpi_gpio_pin_t)PIGPIO_IN_BUTTON3, (rpi_gpio_pin_t)PIGPIO_IN_BUTTON1);
			}
			else
			{ // button 3 / 2 / 1 (A=sw3, B=sw2, SW=sw1)
				TCBM_Bus::rotaryEncoder.Initialize((rpi_gpio_pin_t)PIGPIO_IN_BUTTON3, (rpi_gpio_pin_t)PIGPIO_IN_BUTTON2, (rpi_gpio_pin_t)PIGPIO_IN_BUTTON1);
			}
		}

	}

	static inline void SetDIODirectionInput(bool input)
	{
		if (input)
		{
			// Pull-up when input so that when Plus/4 (tcbm2sd) releases bus (Z) we read $FF as 1551 expects
			RPI_SetGpioInputPullUp((rpi_gpio_pin_t)PIGPIO_DIO1);
			RPI_SetGpioInputPullUp((rpi_gpio_pin_t)PIGPIO_DIO2);
			RPI_SetGpioInputPullUp((rpi_gpio_pin_t)PIGPIO_DIO3);
			RPI_SetGpioInputPullUp((rpi_gpio_pin_t)PIGPIO_DIO4);
			RPI_SetGpioInputPullUp((rpi_gpio_pin_t)PIGPIO_DIO5);
			RPI_SetGpioInputPullUp((rpi_gpio_pin_t)PIGPIO_DIO6);
			RPI_SetGpioInputPullUp((rpi_gpio_pin_t)PIGPIO_DIO7);
			RPI_SetGpioInputPullUp((rpi_gpio_pin_t)PIGPIO_DIO8);
		}
		else
		{
			RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_DIO1, FS_OUTPUT);
			RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_DIO2, FS_OUTPUT);
			RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_DIO3, FS_OUTPUT);
			RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_DIO4, FS_OUTPUT);
			RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_DIO5, FS_OUTPUT);
			RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_DIO6, FS_OUTPUT);
			RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_DIO7, FS_OUTPUT);
			RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_DIO8, FS_OUTPUT);
		}
	}

#if defined(EXPERIMENTALZERO)
	static inline bool AnyButtonPressed()
	{
		return ((gplev0 & PIGPIO_MASK_ANY_BUTTON) != PIGPIO_MASK_ANY_BUTTON);
	}
#endif

	static void UpdateButton(int index, unsigned gplev0)
	{
		bool inputcurrent = (gplev0 & ButtonPinFlags[index]) == 0;

		InputButtonPrev[index] = InputButton[index];
		inputRepeatPrev[index] = inputRepeat[index];

		if (inputcurrent)
		{
			validInputCount[index]++;
			if (validInputCount[index] == INPUT_BUTTON_DEBOUNCE_THRESHOLD)
			{
				InputButton[index] = true;
				inputRepeatThreshold[index] = INPUT_BUTTON_DEBOUNCE_THRESHOLD + INPUT_BUTTON_REPEAT_THRESHOLD;
				inputRepeat[index]++;
			}

			if (validInputCount[index] == inputRepeatThreshold[index])
			{
				inputRepeat[index]++;
				inputRepeatThreshold[index] += INPUT_BUTTON_REPEAT_THRESHOLD / inputRepeat[index];
			}
		}
		else
		{
			InputButton[index] = false;
			validInputCount[index] = 0;
			inputRepeatThreshold[index] = INPUT_BUTTON_REPEAT_THRESHOLD;
			inputRepeat[index] = 0;
			inputRepeatPrev[index] = 0;
		}
	}

	
	//ROTARY: Added for rotary encoder support - 09/05/2019 by Geo...
	//
	// Note: This method serves as a shim to allow the rotary encoder
	//       logic to set a specific input button state (fooling the
	//       original logic into thinking a button was pressed or
	//       released).
	//
	static inline void SetButtonState(int index, bool state)
	{

		InputButtonPrev[index] = InputButton[index];
		inputRepeatPrev[index] = inputRepeat[index];

		if (state == true)
		{

			InputButton[index] = true;
			validInputCount[index] = INPUT_BUTTON_DEBOUNCE_THRESHOLD;
			inputRepeatThreshold[index] = INPUT_BUTTON_DEBOUNCE_THRESHOLD + INPUT_BUTTON_REPEAT_THRESHOLD;
			inputRepeat[index]++;

			validInputCount[index] = inputRepeatThreshold[index];
			inputRepeat[index]++;
			inputRepeatThreshold[index] += INPUT_BUTTON_REPEAT_THRESHOLD / inputRepeat[index];

		}	
		else
		{

			InputButton[index] = false;
			validInputCount[index] = 0;
			inputRepeatThreshold[index] = INPUT_BUTTON_REPEAT_THRESHOLD;
			inputRepeat[index] = 0;
			inputRepeatPrev[index] = 0;
			
		}

	}

	static void Reset(void);
	static void ReadBrowseMode(void);
	static void ReadGPIOUserInput(void);
	static void ReadGPIOUserInput(unsigned gpioLevel);
	static void PollGPIOUserInput1551(void);
	/// Sample reset line and GPLEV0 for buttons/rotary (no TPI input merge). Call once per emulation loop before ReadGPIOUserInput when not using ReadEmulationMode1551 every step.
	static void PollGPIOInputs1551(void);
	static void ReadEmulationMode1551(bool updateTIAStatus = true);

	static void WaitUntilReset(void)
	{
        unsigned gplev0;
        do
        {
            gplev0 = read32(ARM_GPIO_GPLEV0);
            // RESET is active-low on the TCBM bus: asserted when input is 0
            Resetting = !ignoreReset && ((gplev0 & PIGPIO_MASK_IN_RESET) != (PIGPIO_MASK_IN_RESET));

            if (Resetting)
                TCBM_Bus::WaitMicroSeconds(100);
        }
        while (Resetting);
	}

	// Out going
	static void PortA_OnPortOut(void* pUserData, unsigned char status);

	static void RefreshOuts1551(void);

	static void WaitMicroSeconds(u32 amount)
	{
		u32 count;

		for (count = 0; count < amount; ++count)
		{
			unsigned before;
			unsigned after;
			// We try to update every micro second and use as a rough timer to count micro seconds
			before = read32(ARM_SYSTIMER_CLO);
			do
			{
				after = read32(ARM_SYSTIMER_CLO);
			} while (after == before);
		}
	}

	///////////////////////////////////////////////////////////////////////////////////////////////
	// Manual methods used by IEC_Commands

	static inline u8 GetPI_Data() { return PI_Data; }
	static inline bool GetPI_Reset() { return PI_Reset; }
	static inline bool IsDataSetToOut() { return DataSetToOut; }
	static inline bool IsReset() { return Resetting; }
	static inline bool IsDAVAsserted() { return PI_DAV; }
	static inline bool IsDAVReleased() { return !PI_DAV; }
	static inline bool GetPI_DAV() { return PI_DAV; }
	static inline bool GetPI_ACK() { return PI_ACK; }
	static inline u8 GetPI_Status() { return PI_Status; }

	//  set DEV (PC bit 2) via TPI and refresh, when TPI is available
	static inline void SetDEV(bool high)
	{
		if (TPI) {
			IOPort* portC = TPI->GetPortC();
			u8 value = portC->GetOutput();
			if (high) value |= 0x04; else value &= ~0x04;
			portC->SetOutput(value);
			RefreshOuts1551();
		}
		else
		{
			// Fallback when TPI is detached (browse mode)
			RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_OUT_DEV, FS_OUTPUT);
			if (high) write32(ARM_GPIO_GPSET0, PIGPIO_MASK_OUT_DEV);
			else write32(ARM_GPIO_GPCLR0, PIGPIO_MASK_OUT_DEV);
		}
	}

	// Convenience: apply device id mapping to DEV output (bit 2)
	// id&1 == 0 -> drive 8 -> DEV=1, id&1 == 1 -> drive 9 -> DEV=0
	static inline void ForceDevFromDeviceId(u8 id)
	{
		SetDEV((id & 1) == 0);
	}

	static inline void AssertACK() {
		if (TPI) {
			IOPort* portC = TPI->GetPortC();
			u8 value = portC->GetOutput();
			portC->SetOutput(value | 0x08);
			RefreshOuts1551();
		}
		else
		{
			RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_OUT_ACK, FS_OUTPUT);
			write32(ARM_GPIO_GPSET0, PIGPIO_MASK_OUT_ACK);
		}
	}

	static inline void ReleaseACK() {
		if (TPI) {
			IOPort* portC = TPI->GetPortC();
			u8 value = portC->GetOutput();
			portC->SetOutput(value & ~0x08);
			RefreshOuts1551();
		}
		else
		{
			RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_OUT_ACK, FS_OUTPUT);
			write32(ARM_GPIO_GPCLR0, PIGPIO_MASK_OUT_ACK);
		}
	}

	static inline void SetStatus(u8 status) {
		if (TPI) {
			IOPort* portC = TPI->GetPortC();
			u8 value = portC->GetOutput();
			portC->SetOutput((value & ~0x03) | (status & 0x03));
			RefreshOuts1551();
		}
		else
		{
			RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_OUT_STATUS0, FS_OUTPUT);
			RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_OUT_STATUS1, FS_OUTPUT);
			u32 set = 0;
			u32 clear = 0;
			set  |= (status & 0x01) ? PIGPIO_MASK_OUT_STATUS0 : 0; clear |= (status & 0x01) ? 0 : PIGPIO_MASK_OUT_STATUS0;
			set  |= (status & 0x02) ? PIGPIO_MASK_OUT_STATUS1 : 0; clear |= (status & 0x02) ? 0 : PIGPIO_MASK_OUT_STATUS1;
			if (clear) write32(ARM_GPIO_GPCLR0, clear);
			if (set)   write32(ARM_GPIO_GPSET0, set);
		}
	}

	static inline void SetData(u8 data) {
		if (TPI && port) {
			port->SetDirection(0xff);
			port->SetOutput(data);
			RefreshOuts1551();
		}
		else
		{
			// Drive DIO lines directly via GPIO
			SetDIODirectionInput(false);
			unsigned set = 0;
			unsigned clear = 0;
			set  |= (data & 0x01) ? PIGPIO_MASK_DIO1 : 0; clear |= (data & 0x01) ? 0 : PIGPIO_MASK_DIO1;
			set  |= (data & 0x02) ? PIGPIO_MASK_DIO2 : 0; clear |= (data & 0x02) ? 0 : PIGPIO_MASK_DIO2;
			set  |= (data & 0x04) ? PIGPIO_MASK_DIO3 : 0; clear |= (data & 0x04) ? 0 : PIGPIO_MASK_DIO3;
			set  |= (data & 0x08) ? PIGPIO_MASK_DIO4 : 0; clear |= (data & 0x08) ? 0 : PIGPIO_MASK_DIO4;
			set  |= (data & 0x10) ? PIGPIO_MASK_DIO5 : 0; clear |= (data & 0x10) ? 0 : PIGPIO_MASK_DIO5;
			set  |= (data & 0x20) ? PIGPIO_MASK_DIO6 : 0; clear |= (data & 0x20) ? 0 : PIGPIO_MASK_DIO6;
			set  |= (data & 0x40) ? PIGPIO_MASK_DIO7 : 0; clear |= (data & 0x40) ? 0 : PIGPIO_MASK_DIO7;
			set  |= (data & 0x80) ? PIGPIO_MASK_DIO8 : 0; clear |= (data & 0x80) ? 0 : PIGPIO_MASK_DIO8;
			if (clear) write32(ARM_GPIO_GPCLR0, clear);
			if (set)   write32(ARM_GPIO_GPSET0, set);
			DataSetToOut = true;
		}
	}

	static inline void SetDataInput() {
		if (TPI && port) {
			port->SetDirection(0x00);
			RefreshOuts1551();
		}
		else
		{
			SetDIODirectionInput(true);
			DataSetToOut = false;
		}
	}

	static inline void WaitWhileDAVAsserted()
	{
		while (IsDAVAsserted()) {
			ReadBrowseMode();
		}
	}

	static inline void WaitWhileDAVReleased()
	{
		while (IsDAVReleased()) {
			ReadBrowseMode();
		}
	}

	///////////////////////////////////////////////////////////////////////////////////////////////

	static inline void SetIgnoreReset(bool value)
	{
		ignoreReset = value;
	}

	//ROTARY: Added for rotary encoder support - 09/05/2019 by Geo...
	static inline void SetRotaryEncoderEnable(bool value)
	{
		rotaryEncoderEnable = value;
	}

	//ROTARY: Added for rotary encoder inversion (Issue#185) - 08/13/2020 by Geo...
	static inline void SetRotaryEncoderInvert(bool value)
	{
		rotaryEncoderInvert = value;
	}

	static m6523* TPI;
	static IOPort* port;

	static bool GetInputButtonPressed(int buttonIndex) { return InputButton[buttonIndex] && !InputButtonPrev[buttonIndex]; }
	static bool GetInputButtonReleased(int buttonIndex) { return InputButton[buttonIndex] == false; }
	static bool GetInputButton(int buttonIndex) { return InputButton[buttonIndex]; }
	static bool GetInputButtonRepeating(int buttonIndex) { return inputRepeat[buttonIndex] != inputRepeatPrev[buttonIndex]; }
	static bool GetInputButtonHeld(int buttonIndex) { return inputRepeatThreshold[buttonIndex] >= INPUT_BUTTON_DEBOUNCE_THRESHOLD + (INPUT_BUTTON_REPEAT_THRESHOLD * 2); }

	static bool OutputLED;
	static bool OutputSound;

private:
	static u32 oldClears;
	static u32 oldSets;

	static bool ignoreReset;

	static u32 emulationModeCheckButtonIndex;

	static unsigned gplev0;

	static u8 PI_Data;
	static u8 PI_Status;
	static bool PI_ACK;
	static bool PI_DAV;
	static bool PI_DEV;
	static bool PI_Reset;

	static u8 TPI_Data;
	static u8 TPI_Status;
	static bool TPI_ACK;
	static bool TPI_DAV;
	static bool TPI_DEV;

	static bool DataSetToOut;
	static bool Resetting;

	static int buttonCount;

	static u32 myOutsGPFSEL0;
	static u32 myOutsGPFSEL1;
	static bool InputButton[5];
	static bool InputButtonPrev[5];
	static u32 validInputCount[5];
	static u32 inputRepeatThreshold[5];
	static u32 inputRepeat[5];
	static u32 inputRepeatPrev[5];
	static u8 lastDirA;
	static u8 lastDirC;
	static u32 lastSet;
	static u32 lastClear;
	static u8 lastOutA;
	static u8 lastOutC;
	static bool lastOutputLED;
	static bool lastOutputSound;

	//ROTARY: Added for rotary encoder support - 09/05/2019 by Geo...
	static RotaryEncoder rotaryEncoder;
	static bool rotaryEncoderEnable;
	//ROTARY: Added for rotary encoder inversion (Issue#185) - 08/13/2020 by Geo...
	static bool rotaryEncoderInvert;

};
#endif
