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
u32 TCBM_Bus::PIGPIO_MASK_IN_ATN = 1 << PIGPIO_ATN;
u32 TCBM_Bus::PIGPIO_MASK_IN_DATA = 1 << PIGPIO_DATA;
u32 TCBM_Bus::PIGPIO_MASK_IN_CLOCK = 1 << PIGPIO_CLOCK;
u32 TCBM_Bus::PIGPIO_MASK_IN_SRQ = 1 << PIGPIO_SRQ;
u32 TCBM_Bus::PIGPIO_MASK_IN_RESET = 1 << PIGPIO_RESET;

u8 TCBM_Bus::PI_Data = false;
u8 TCBM_Bus::PI_Status = false;
bool TCBM_Bus::PI_ACK = false;
bool TCBM_Bus::PI_DEV = false;
bool TCBM_Bus::PI_Reset = false;

u8 TCBM_Bus::TPI_Data = false;
u8 TCBM_Bus::TPI_Status = false;
bool TCBM_Bus::TPI_ACK = false;
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


//ROTARY: Modified for rotary encoder support - 09/05/2019 by Geo...
/// @brief read real I/O pins in browser mode + read the rotary encoder/buttons
/// @param  
void TCBM_Bus::ReadBrowseMode(void)
{
	gplev0 = read32(ARM_GPIO_GPLEV0);
	ReadGPIOUserInput();

	bool ATNIn = (gplev0 & PIGPIO_MASK_IN_ATN) == (PIGPIO_MASK_IN_ATN);
	if (PI_Atn != ATNIn)
	{
		PI_Atn = ATNIn;
	}

	if (!AtnaDataSetToOut && !DataSetToOut)	// only sense if we have not brought the line low (because we can't as we have the pin set to output but we can simulate in software)
	{
		bool DATAIn = (gplev0 & PIGPIO_MASK_IN_DATA) == (PIGPIO_MASK_IN_DATA);
		if (PI_Data != DATAIn)
		{
			PI_Data = DATAIn;
		}
	}
	else
	{
		PI_Data = true;
	}

	if (!ClockSetToOut)	// only sense if we have not brought the line low (because we can't as we have the pin set to output but we can simulate in software)
	{
		bool CLOCKIn = (gplev0 & PIGPIO_MASK_IN_CLOCK) == (PIGPIO_MASK_IN_CLOCK);
		if (PI_Clock != CLOCKIn)
		{
			PI_Clock = CLOCKIn;
		}
	}
	else
	{
		PI_Clock = true;
	}

	Resetting = !ignoreReset && ((gplev0 & PIGPIO_MASK_IN_RESET) == (PIGPIO_MASK_IN_RESET));
}

/// @brief read real I/O pins before emulation step in emulation mode
/// @param  
void TCBM_Bus::ReadEmulationMode1551(void)
{
	IOPort* portB = 0;
	gplev0 = read32(ARM_GPIO_GPLEV0);

	portB = port;

#ifndef REAL_XOR
	bool ATNIn = (gplev0 & PIGPIO_MASK_IN_ATN) == (PIGPIO_MASK_IN_ATN);
	if (PI_Atn != ATNIn)
	{
		PI_Atn = ATNIn;

		//DEBUG_LOG("A%d\r\n", PI_Atn);
		//if (port)
		{
			if ((portB->GetDirection() & 0x10) != 0)
			{
				// Emulate the XOR gate UD3
				// We only need to do this when fully emulating, iec commands do this internally
				AtnaDataSetToOut = (VIA_Atna != PI_Atn);
			}

			portB->SetInput(VIAPORTPINS_ATNIN, ATNIn);	//is inverted and then connected to pb7 and ca1
			VIA->InputCA1(ATNIn);
		}
	}

	if (portB && (portB->GetDirection() & 0x10) == 0)
		AtnaDataSetToOut = false; // If the ATNA PB4 gets set to an input then we can't be pulling data low. (Maniac Mansion does this)

	// moved from PortB_OnPortOut
	if (AtnaDataSetToOut)
		portB->SetInput(VIAPORTPINS_DATAIN, true);	// simulate the read in software

	if (!AtnaDataSetToOut && !DataSetToOut)	// only sense if we have not brought the line low (because we can't as we have the pin set to output but we can simulate in software)
	{
		bool DATAIn = (gplev0 & PIGPIO_MASK_IN_DATA) == (PIGPIO_MASK_IN_DATA);
		//if (PI_Data != DATAIn)
		{
			PI_Data = DATAIn;
			portB->SetInput(VIAPORTPINS_DATAIN, DATAIn);	// VIA DATAin pb0 output from inverted DIN 5 DATA
		}
	}
	else
	{
		PI_Data = true;
		portB->SetInput(VIAPORTPINS_DATAIN, true);	// simulate the read in software
	}
#else
	bool ATNIn = (gplev0 & PIGPIO_MASK_IN_ATN) == (PIGPIO_MASK_IN_ATN);
	if (PI_Atn != ATNIn)
	{
		PI_Atn = ATNIn;

		{
			portB->SetInput(VIAPORTPINS_ATNIN, ATNIn);	//is inverted and then connected to pb7 and ca1
			VIA->InputCA1(ATNIn);
		}
	}

	if (!DataSetToOut)	// only sense if we have not brought the line low (because we can't as we have the pin set to output but we can simulate in software)
	{
		bool DATAIn = (gplev0 & PIGPIO_MASK_IN_DATA) == (PIGPIO_MASK_IN_DATA);
		//if (PI_Data != DATAIn)
		{
			PI_Data = DATAIn;
			portB->SetInput(VIAPORTPINS_DATAIN, DATAIn);	// VIA DATAin pb0 output from inverted DIN 5 DATA
		}
	}
	else
	{
		PI_Data = true;
		portB->SetInput(VIAPORTPINS_DATAIN, true);	// simulate the read in software
	}

#endif
	if (!ClockSetToOut)	// only sense if we have not brought the line low (because we can't as we have the pin set to output but we can simulate in software)
	{
		bool CLOCKIn = (gplev0 & PIGPIO_MASK_IN_CLOCK) == (PIGPIO_MASK_IN_CLOCK);
		//if (PI_Clock != CLOCKIn)
		{
			PI_Clock = CLOCKIn;
			portB->SetInput(VIAPORTPINS_CLOCKIN, CLOCKIn); // VIA CLKin pb2 output from inverted DIN 4 CLK
		}
	}
	else
	{
		PI_Clock = true;
		portB->SetInput(VIAPORTPINS_CLOCKIN, true); // simulate the read in software
	}

	Resetting = !ignoreReset && ((gplev0 & PIGPIO_MASK_IN_RESET) == (PIGPIO_MASK_IN_RESET));
}

/// @brief Set real I/O pins / directions after emulation step in emulation mode
/// @param  
void TCBM_Bus::RefreshOuts1551(void)
{
	unsigned set = 0;
	unsigned clear = 0;
	unsigned tmp;

	if (!splitIECLines)
	{
		unsigned outputs = 0;

		if (AtnaDataSetToOut || DataSetToOut) outputs |= (FS_OUTPUT << ((PIGPIO_DATA - 10) * 3));
		if (ClockSetToOut) outputs |= (FS_OUTPUT << ((PIGPIO_CLOCK - 10) * 3));

		unsigned nValue = (myOutsGPFSEL1 & PI_OUTPUT_MASK_GPFSEL1) | outputs;
		write32(ARM_GPIO_GPFSEL1, nValue);
	}
	else
	{
		if (AtnaDataSetToOut || DataSetToOut) set |= 1 << PIGPIO_OUT_DATA;
		else clear |= 1 << PIGPIO_OUT_DATA;

		if (ClockSetToOut) set |= 1 << PIGPIO_OUT_CLOCK;
		else clear |= 1 << PIGPIO_OUT_CLOCK;

		if (!invertIECOutputs) {
			tmp = set;
			set = clear;
			clear = tmp;
		}
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

	// VIA $1800
	//	CA2, CB1 and CB2 are not connected (reads as high)
	// VIA $1C00
	//	CB1 not connected (reads as high)

	DataSetToOut = false;

	TPI_Data = 0;
	TPI_Status = 0;
	TPI_ACK = false;

	PI_Data = 0;
	PI_Status = 0;
	PI_ACK = false;

}

