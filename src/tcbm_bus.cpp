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

#include "tcbm_bus.h"
#include "InputMappings.h"

//#define REAL_XOR 1

int TCBM_Bus::buttonCount = sizeof(ButtonPinFlags) / sizeof(unsigned);

u32 TCBM_Bus::oldClears = 0;
u32 TCBM_Bus::oldSets = 0;

u8 TCBM_Bus::PI_Data = 0;
u8 TCBM_Bus::PI_Status = 0;
bool TCBM_Bus::PI_ACK = false;
bool TCBM_Bus::PI_DAV = false;
bool TCBM_Bus::PI_DEV = false;
bool TCBM_Bus::PI_Reset = false;

u8 TCBM_Bus::TPI_Data = 0;
u8 TCBM_Bus::TPI_Status = 0;
bool TCBM_Bus::TPI_ACK = false;
bool TCBM_Bus::TPI_DAV = false;
bool TCBM_Bus::TPI_DEV = false;

bool TCBM_Bus::DataSetToOut = false;

m6523* TCBM_Bus::TPI = 0;
IOPort* TCBM_Bus::port = 0;

bool TCBM_Bus::OutputLED = false;
bool TCBM_Bus::OutputSound = false;

bool TCBM_Bus::Resetting = false;

bool TCBM_Bus::ignoreReset = false;

u32 TCBM_Bus::myOutsGPFSEL1 = 0;
u32 TCBM_Bus::myOutsGPFSEL0 = 0;
bool TCBM_Bus::InputButton[5] = { 0 };
bool TCBM_Bus::InputButtonPrev[5] = { 0 };
u32 TCBM_Bus::validInputCount[5] = { 0 };
u32 TCBM_Bus::inputRepeatThreshold[5];
u32 TCBM_Bus::inputRepeat[5] = { 0 };
u32 TCBM_Bus::inputRepeatPrev[5] = { 0 };


u32 TCBM_Bus::emulationModeCheckButtonIndex = 0;

unsigned TCBM_Bus::gplev0;

//ROTARY: Added for rotary encoder support - 09/05/2019 by Geo...
RotaryEncoder TCBM_Bus::rotaryEncoder;
bool TCBM_Bus::rotaryEncoderEnable;
//ROTARY: Added for rotary encoder inversion (Issue#185) - 08/13/2020 by Geo...
bool TCBM_Bus::rotaryEncoderInvert;

void TCBM_Bus::ReadGPIOUserInput()
{
	//ROTARY: Added for rotary encoder support - 09/05/2019 by Geo...
	if (TCBM_Bus::rotaryEncoderEnable == true)
	{
		int indexEnter = InputMappings::INPUT_BUTTON_ENTER;
		int indexUp = InputMappings::INPUT_BUTTON_UP;
		int indexDown = InputMappings::INPUT_BUTTON_DOWN;
		int indexBack = InputMappings::INPUT_BUTTON_BACK;
		int indexInsert = InputMappings::INPUT_BUTTON_INSERT;

		//Poll the rotary encoder
		//
		// Note: If the rotary encoder returns any value other than 'NoChange' an
		//       event has been detected.  We force the button state of the original
		//       input button registers to reflect the desired action, and allow the
		//       original processing logic to do it's work.
		//
		rotary_result_t rotaryResult = TCBM_Bus::rotaryEncoder.Poll(gplev0);
		switch (rotaryResult)
		{

			case ButtonDown:
				SetButtonState(indexEnter, true);
				break;

			case RotateNegative:
				SetButtonState(indexUp, true);
				break;

			case RotatePositive:
				SetButtonState(indexDown, true);
				break;

			default:
				SetButtonState(indexEnter, false);
				SetButtonState(indexUp, false);
				SetButtonState(indexDown, false);
				break;

		}

		UpdateButton(indexBack, gplev0);
		UpdateButton(indexInsert, gplev0);
	}
	else // Unmolested original logic
	{

		int index;
		for (index = 0; index < buttonCount; ++index)
		{
			UpdateButton(index, gplev0);
		}

	}
}


void TCBM_Bus::SetDIODirectionInput(bool input)
{
	if (input)
	{
		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_DIO1, FS_INPUT);
		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_DIO2, FS_INPUT);
		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_DIO3, FS_INPUT);
		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_DIO4, FS_INPUT);
		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_DIO5, FS_INPUT);
		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_DIO6, FS_INPUT);
		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_DIO7, FS_INPUT);
		RPI_SetGpioPinFunction((rpi_gpio_pin_t)PIGPIO_DIO8, FS_INPUT);
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


//ROTARY: Modified for rotary encoder support - 09/05/2019 by Geo...
/// @brief read real I/O pins in browser mode + read the rotary encoder/buttons
///        don't update TIA port status
/// @param  
void TCBM_Bus::ReadBrowseMode(void)
{
	gplev0 = read32(ARM_GPIO_GPLEV0);
	ReadGPIOUserInput();

	PI_DAV  = gplev0 & PIGPIO_MASK_IN_DAV == PIGPIO_MASK_IN_DAV;

	if (!DataSetToOut) { // we treat port A on byte level, not bit level
		PI_Data = (gplev0 & PIGPIO_DIO1 ? 0x01 : 0x00) |
			(gplev0 & PIGPIO_DIO2 ? 0x02 : 0x00) |
			(gplev0 & PIGPIO_DIO3 ? 0x04 : 0x00) |
			(gplev0 & PIGPIO_DIO4 ? 0x08 : 0x00) |
			(gplev0 & PIGPIO_DIO5 ? 0x10 : 0x00) |
			(gplev0 & PIGPIO_DIO6 ? 0x20 : 0x00) |
			(gplev0 & PIGPIO_DIO7 ? 0x40 : 0x00) |
			(gplev0 & PIGPIO_DIO8 ? 0x80 : 0x00);
	}

	Resetting = !ignoreReset && ((gplev0 & PIGPIO_MASK_IN_RESET) == (PIGPIO_MASK_IN_RESET));
}

/// @brief read real I/O pins before emulation step in emulation mode
/// @param  
void TCBM_Bus::ReadEmulationMode1551(void)
{
	IOPort* portC = TPI->GetPortC();

	gplev0 = read32(ARM_GPIO_GPLEV0);

	PI_DAV  = gplev0 & PIGPIO_MASK_IN_DAV == PIGPIO_MASK_IN_DAV;
	portC -> SetInput(0x80, PI_DAV); // DAV is bit 7

	IOPort* portA = TPI->GetPortA();
	DataSetToOut = portA->GetDirection() != 0; // we treat port A on byte level, not bit level
	if (!DataSetToOut) {
		PI_Data = (gplev0 & PIGPIO_DIO1 ? 0x01 : 0x00) |
			(gplev0 & PIGPIO_DIO2 ? 0x02 : 0x00) |
			(gplev0 & PIGPIO_DIO3 ? 0x04 : 0x00) |
			(gplev0 & PIGPIO_DIO4 ? 0x08 : 0x00) |
			(gplev0 & PIGPIO_DIO5 ? 0x10 : 0x00) |
			(gplev0 & PIGPIO_DIO6 ? 0x20 : 0x00) |
			(gplev0 & PIGPIO_DIO7 ? 0x40 : 0x00) |
			(gplev0 & PIGPIO_DIO8 ? 0x80 : 0x00);
		portA->SetInput(PI_Data);
	}

	Resetting = !ignoreReset && ((gplev0 & PIGPIO_MASK_IN_RESET) == (PIGPIO_MASK_IN_RESET));
}

/// @brief Set real I/O pins / directions after emulation step in emulation mode
/// @param  
void TCBM_Bus::RefreshOuts1551(void)
{
	unsigned set = 0;
	unsigned clear = 0;

	if (TPI && port)
	{
		// portA handled in PortA_OnPortOut? XXX or maybe here - both data and direction?
		IOPort* portC = TPI->GetPortC();
		if (portC->GetOutput() & 0x08) set |= 1 << PIGPIO_OUT_ACK;
		else clear |= 1 << PIGPIO_OUT_ACK;
		if (portC->GetOutput() & 0x02) set |= 1 << PIGPIO_OUT_DEV;
		else clear |= 1 << PIGPIO_OUT_DEV;
		if (portC->GetOutput() & 0x02) set |= 1 << PIGPIO_OUT_STATUS1;
		else clear |= 1 << PIGPIO_OUT_STATUS1;
		if (portC->GetOutput() & 0x01) set |= 1 << PIGPIO_OUT_STATUS0;
		else clear |= 1 << PIGPIO_OUT_STATUS0;
	}

	if (OutputLED) set |= 1 << PIGPIO_OUT_LED;
	else clear |= 1 << PIGPIO_OUT_LED;

	if (OutputSound) set |= 1 << PIGPIO_OUT_SOUND;
	else clear |= 1 << PIGPIO_OUT_SOUND;

	write32(ARM_GPIO_GPCLR0, clear);
	write32(ARM_GPIO_GPSET0, set);
}

void TCBM_Bus::PortA_OnPortOut(void* pUserData, unsigned char status)
{
	bool oldDataSetToOut = DataSetToOut;

	// These are the values the VIA is trying to set the outputs to
	VIA_Atna = (status & (unsigned char)VIAPORTPINS_ATNAOUT) != 0;
	VIA_Data = (status & (unsigned char)VIAPORTPINS_DATAOUT) != 0;		// VIA DATAout PB1 inverted and then connected to DIN DATA
	VIA_Clock = (status & (unsigned char)VIAPORTPINS_CLOCKOUT) != 0;	// VIA CLKout PB3 inverted and then connected to DIN CLK

	if (TPI && port)
	{
		// If the VIA's data and clock outputs ever get set to inputs the real hardware reads these lines as asserted.
		bool PB1SetToInput = (port->GetDirection() & 2) == 0;
		bool PB3SetToInput = (port->GetDirection() & 8) == 0;
		if (PB1SetToInput) VIA_Data = true;
		if (PB3SetToInput) VIA_Clock = true;
	}

	DataSetToOut = VIA_Data;
}

void TCBM_Bus::Reset(void)
{
	WaitUntilReset();

	DataSetToOut = false;
	SetDIODirectionInput(DataSetToOut);

	TPI_Data = 0;
	TPI_Status = 0;
	TPI_ACK = false;

	PI_Data = 0;
	PI_Status = 0;
	PI_ACK = false;

}

