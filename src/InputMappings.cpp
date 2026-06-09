
// Pi1541 - A Commodore 1541 disk drive emulator
// Copyright(C) 2018 Stephen White
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

#include "defs.h"
#include "InputMappings.h"
#include "FileBrowser.h"
#if defined(PI1551SUPPORT)
#include "tcbm_bus.h"
#define BUS_API TCBM_Bus
#else
#include "iec_bus.h"
#define BUS_API IEC_Bus
#endif
#include "debug.h"
extern "C"
{
#include "rpi-aux.h"
extern void usDelay(unsigned nMicroSeconds);
}
extern void Reboot_Pi(void);

// If disk swaps can be done via multiple cores then directDiskSwapRequest needs to be volatile. WARNING: volatile acesses can be very expensive.
//volatile unsigned InputMappings::directDiskSwapRequest = 0;
unsigned InputMappings::directDiskSwapRequest = 0;
//volatile unsigned InputMappings::uartFlags = 0;
//unsigned InputMappings::escapeSequenceIndex = 0;

u8 InputMappings::INPUT_BUTTON_ENTER = 0;
u8 InputMappings::INPUT_BUTTON_UP = 1;
u8 InputMappings::INPUT_BUTTON_DOWN = 2;
u8 InputMappings::INPUT_BUTTON_BACK = 3;
u8 InputMappings::INPUT_BUTTON_INSERT = 4;

InputMappings::InputMappings()
	: keyboardBrowseLCDScreen(false)
	, insertButtonPressedPrev(false)
	, insertButtonPressed(false)
	, enterButtonPressedPrev(false)
	, enterButtonPressed(false)
{
}

bool InputMappings::CheckButtonsBrowseMode()
{
	buttonFlags = 0;

	if (BUS_API::GetInputButtonHeld(INPUT_BUTTON_INSERT))	// Change DeviceID
	{
		if (BUS_API::GetInputButtonRepeating(INPUT_BUTTON_ENTER))
		{
			SetButtonFlag(FUNCTION_FLAG);
			inputROMOrDevice = 8;
		}
		else if (BUS_API::GetInputButtonRepeating(INPUT_BUTTON_UP))
		{
			SetButtonFlag(FUNCTION_FLAG);
			inputROMOrDevice = 9;
		}
		else if (BUS_API::GetInputButtonRepeating(INPUT_BUTTON_DOWN))
		{
			SetButtonFlag(FUNCTION_FLAG);
			inputROMOrDevice = 10;
		}
		else if (BUS_API::GetInputButtonRepeating(INPUT_BUTTON_BACK))
		{
			SetButtonFlag(FUNCTION_FLAG);
			inputROMOrDevice = 11;
		}
		insertButtonPressedPrev = false;
	}
	else if (BUS_API::GetInputButtonHeld(INPUT_BUTTON_ENTER))	// Change ROMs
	{
		if (BUS_API::GetInputButtonRepeating(INPUT_BUTTON_UP))
		{
			SetButtonFlag(FUNCTION_FLAG);
			inputROMOrDevice = 1;
		}
		else if (BUS_API::GetInputButtonRepeating(INPUT_BUTTON_DOWN))
		{
			SetButtonFlag(FUNCTION_FLAG);
			inputROMOrDevice = 2;
		}
		else if (BUS_API::GetInputButtonRepeating(INPUT_BUTTON_BACK))
		{
			SetButtonFlag(FUNCTION_FLAG);
			inputROMOrDevice = 3;
		}
		else if (BUS_API::GetInputButtonRepeating(INPUT_BUTTON_INSERT))
		{
			SetButtonFlag(FUNCTION_FLAG);
			inputROMOrDevice = 4;
		}
		enterButtonPressedPrev = false;
	}
	else if (BUS_API::GetInputButtonRepeating(INPUT_BUTTON_UP))
		SetButtonFlag(UP_FLAG);
	else if (BUS_API::GetInputButtonRepeating(INPUT_BUTTON_DOWN))
		SetButtonFlag(DOWN_FLAG);
	else if (BUS_API::GetInputButtonPressed(INPUT_BUTTON_BACK))
		SetButtonFlag(BACK_FLAG);
	else
	{
		// edge detection
		insertButtonPressed = !BUS_API::GetInputButtonReleased(INPUT_BUTTON_INSERT);
		if (insertButtonPressedPrev && !insertButtonPressed)
			SetButtonFlag(INSERT_FLAG);
		insertButtonPressedPrev = insertButtonPressed;

		enterButtonPressed = !BUS_API::GetInputButtonReleased(INPUT_BUTTON_ENTER);
		if (enterButtonPressedPrev && !enterButtonPressed)
			SetButtonFlag(ENTER_FLAG);
		enterButtonPressedPrev = enterButtonPressed;
	}

	return buttonFlags != 0;
}

void InputMappings::WaitForClearButtons()
{
	buttonFlags = 0;

	do
	{
		BUS_API::ReadBrowseMode();

		insertButtonPressed = !BUS_API::GetInputButtonReleased(INPUT_BUTTON_INSERT);
		insertButtonPressedPrev = insertButtonPressed;

		enterButtonPressed = !BUS_API::GetInputButtonReleased(INPUT_BUTTON_ENTER);
		enterButtonPressedPrev = enterButtonPressed;
		
		usDelay(1);
	} while (insertButtonPressedPrev || enterButtonPressedPrev);

	enterButtonPressedPrev = false;
	insertButtonPressedPrev = false;
}

void InputMappings::CheckButtonsEmulationMode()
{
	buttonFlags = 0;

	if (BUS_API::GetInputButtonRepeating(INPUT_BUTTON_UP)
		|| BUS_API::GetInputButtonPressed(INPUT_BUTTON_UP))
		SetButtonFlag(NEXT_FLAG);
	else if (BUS_API::GetInputButtonRepeating(INPUT_BUTTON_DOWN)
		|| BUS_API::GetInputButtonPressed(INPUT_BUTTON_DOWN))
		SetButtonFlag(PREV_FLAG);
	//else if (BUS_API::GetInputButtonPressed(INPUT_BUTTON_BACK))
	//	SetButtonFlag(BACK_FLAG);
	//else if (BUS_API::GetInputButtonPressed(INPUT_BUTTON_INSERT))
	//	SetButtonFlag(INSERT_FLAG);
	else {
		enterButtonPressed = !BUS_API::GetInputButtonReleased(INPUT_BUTTON_ENTER);
		if (enterButtonPressedPrev && !enterButtonPressed)
			SetButtonFlag(ESC_FLAG);
		enterButtonPressedPrev = enterButtonPressed;
	}
}


//void InputMappings::CheckUart()
//{
//	char charReceived;
//
//	uartFlags = 0;
//
//	if (RPI_AuxMiniUartRead(&charReceived))
//	{
//		DEBUG_LOG("charReceived=%c %02x\r\n", charReceived, charReceived);
//		if (charReceived == '[')
//		{
//			escapeSequenceIndex++;
//		}
//		else
//		{
//			if (escapeSequenceIndex == 0)
//			{
//				if (charReceived == 27)
//					SetUartFlag(ESC_FLAG);
//				else if (charReceived == 13)
//					SetUartFlag(ENTER_FLAG);
//				else if (charReceived == ' ')
//					SetUartFlag(SPACE_FLAG);
//				else if (charReceived == 0x7f)
//					SetUartFlag(BACK_FLAG);
//				//else if (charReceived == 'u')
//				//	SetUartFlag(UP_FLAG);
//				//else if (charReceived == 'U')
//				//	SetUartFlag(PAGEUP_FLAG);
//				//else if (charReceived == 'd')
//				//	SetUartFlag(DOWN_FLAG);
//				//else if (charReceived == 'D')
//				//	SetUartFlag(PAGEDOWN_FLAG);
//				else
//				{
//					char number = charReceived - '0';
//					if (number >= 0 && number <= 9)
//					{
//						if (number == 0)
//							number = 10;
//						directDiskSwapRequest |= (1 << (number - 1));
//						printf("SWAP %d\r\n", number);
//					}
//				}
//			}
//			else if (escapeSequenceIndex == 1)
//			{
//				if (charReceived == 'A')
//					SetUartFlag(UP_FLAG);
//				else if (charReceived == 'B')
//					SetUartFlag(DOWN_FLAG);
//				else if (charReceived == 'C')
//					SetUartFlag(PAGEDOWN_FLAG);
//				else if (charReceived == 'D')
//					SetUartFlag(PAGEUP_FLAG);
//				else if (charReceived == '2')
//					SetUartFlag(INSERT_FLAG);
//				escapeSequenceIndex = 0;
//			}
//		}
//	}
//}

bool InputMappings::CheckKeyboardBrowseMode()
{
#if not defined(EXPERIMENTALZERO)
	Keyboard* keyboard = Keyboard::Instance();
#endif
	keyboardFlags = 0;
#if not defined(EXPERIMENTALZERO)
	keyboardNumLetter = 0;
	if (!keyboard->CheckChanged())
	{
		return false;
	}

	if (keyboard->KeyHeld(KEY_DELETE) && keyboard->KeyLCtrlAlt() )
		Reboot_Pi();

	if (keyboard->KeyHeld(KEY_ESC))
		SetKeyboardFlag(ESC_FLAG);
	else if (keyboard->KeyHeld(KEY_INSERT) || (keyboard->KeyHeld(KEY_ENTER) && keyboard->KeyEitherAlt()))
		SetKeyboardFlag(INSERT_FLAG);
	else if (keyboard->KeyHeld(KEY_ENTER))
		SetKeyboardFlag(ENTER_FLAG);
	else if (keyboard->KeyHeld(KEY_BACKSPACE))
		SetKeyboardFlag(BACK_FLAG);
	else if (keyboard->KeyHeld(KEY_SPACE))
		SetKeyboardFlag(SPACE_FLAG);
	else if (keyboard->KeyHeld(KEY_UP))
		SetKeyboardFlag(UP_FLAG);
	else if (keyboard->KeyHeld(KEY_PAGEUP) || keyboard->KeyHeld(KEY_LEFT))
	{
		if (keyboardBrowseLCDScreen)
			SetKeyboardFlag(PAGEUP_LCD_FLAG);
		else
			SetKeyboardFlag(PAGEUP_FLAG);
	}
	else if (keyboard->KeyHeld(KEY_DOWN))
		SetKeyboardFlag(DOWN_FLAG);
	else if (keyboard->KeyHeld(KEY_PAGEDOWN) || keyboard->KeyHeld(KEY_RIGHT))
	{
		if (keyboardBrowseLCDScreen)
			SetKeyboardFlag(PAGEDOWN_LCD_FLAG);
		else
			SetKeyboardFlag(PAGEDOWN_FLAG);
	}
	else if (keyboard->KeyHeld(KEY_HOME))
		SetKeyboardFlag(HOME_FLAG);
	else if (keyboard->KeyHeld(KEY_END))
		SetKeyboardFlag(END_FLAG);
	else if (keyboard->KeyHeld(KEY_N) && keyboard->KeyEitherAlt() )
		SetKeyboardFlag(NEWD64_FLAG);
	else if (keyboard->KeyHeld(KEY_A) && keyboard->KeyEitherAlt() )
		SetKeyboardFlag(AUTOLOAD_FLAG);
	else if (keyboard->KeyHeld(KEY_R) && keyboard->KeyEitherAlt() )
		SetKeyboardFlag(FAKERESET_FLAG);
	else if (keyboard->KeyHeld(KEY_W) && keyboard->KeyEitherAlt() )
		SetKeyboardFlag(WRITEPROTECT_FLAG);
	else if (keyboard->KeyHeld(KEY_L) && keyboard->KeyEitherAlt() )
		SetKeyboardFlag(MAKELST_FLAG);
	else if (keyboard->KeyPressed(KEY_R) && keyboard->KeyNoModifiers())
		SetKeyboardFlag(TAPE_READ_TOGGLE_FLAG);  // 'r' toggles TAPE_READ (test function, before search)
	else if (keyboard->KeyPressed(KEY_P) && keyboard->KeyNoModifiers())
		SetKeyboardFlag(TAPE_SENSE_TOGGLE_FLAG);  // 'p' toggles TAPE_SENSE (test function, before search)
	else
	{
		if (keyboard->KeyNoModifiers())
		{
			unsigned index;

			for (index = 0; index <= 9; ++index)
			{
				if (keyboard->KeyHeld(KEY_1+index) || keyboard->KeyHeld(KEY_KP1+index))
				{
					SetKeyboardFlag(NUMLET_FLAG);
					keyboardNumLetter = index+'1';	// key 1 is ascii '1'
					if (keyboardNumLetter > '9') keyboardNumLetter = '0';
				}
			}

			// Skip 'r' and 'p' from search (they are handled above)
			for (index = KEY_A; index <= KEY_Z; ++index)
			{
				if (index == KEY_R || index == KEY_P)
					continue;  // Skip 'r' and 'p' - they are used for tape control
				if (keyboard->KeyHeld(index))
				{
					SetKeyboardFlag(NUMLET_FLAG);
					keyboardNumLetter = index-KEY_A+'A';	// key A is ascii 'A'
				}
			}
			for (index = KEY_F1; index <= KEY_F12; ++index)	// F13 isnt contiguous
			{
				if (keyboard->KeyHeld(index))
				{
					SetKeyboardFlag(FUNCTION_FLAG);
					inputROMOrDevice = index-KEY_F1+1;	// key F1 is 1
				}
			}
		}
	}
#endif
	return keyboardFlags != 0;
}

void InputMappings::CheckKeyboardEmulationMode(unsigned numberOfImages, unsigned numberOfImagesMax)
{
#if not defined(EXPERIMENTALZERO)
	Keyboard* keyboard = Keyboard::Instance();

	keyboardFlags = 0;
	if (!keyboard->CheckChanged())
		return;

	if (keyboard->KeyHeld(KEY_DELETE) && keyboard->KeyLCtrlAlt() )
		Reboot_Pi();

	if (keyboard->KeyHeld(KEY_ESC))
		SetKeyboardFlag(ESC_FLAG);
	else if (keyboard->KeyHeld(KEY_PAGEUP))
		SetKeyboardFlag(PREV_FLAG);
	else if (keyboard->KeyHeld(KEY_PAGEDOWN))
		SetKeyboardFlag(NEXT_FLAG);
	else if (keyboard->KeyHeld(KEY_A) && keyboard->KeyEitherAlt() )
		SetKeyboardFlag(AUTOLOAD_FLAG);
	else if (keyboard->KeyHeld(KEY_R) && keyboard->KeyEitherAlt() )
		SetKeyboardFlag(FAKERESET_FLAG);
		else if (keyboard->KeyHeld(KEY_H))
			SetKeyboardFlag(HALT_FLAG);
		else if (keyboard->KeyHeld(KEY_S))
			SetKeyboardFlag(STEP_FLAG);
		else if (keyboard->KeyPressed(KEY_R) && !keyboard->KeyEitherAlt())
			SetKeyboardFlag(TAPE_READ_TOGGLE_FLAG);  // 'r' toggles TAPE_READ (when not Alt)
		else if (keyboard->KeyHeld(KEY_R))
			SetKeyboardFlag(RUN_FLAG);
		else if (keyboard->KeyHeld(KEY_E))
			SetKeyboardFlag(RESET_FLAG);
		else if (keyboard->KeyPressed(KEY_P))
			SetKeyboardFlag(TAPE_SENSE_TOGGLE_FLAG);  // 'p' toggles TAPE_SENSE
	else if (numberOfImages > 1)
	{
		unsigned index;
		for (index = 0; index < 10; index++)
		{
			if ( keyboard->KeyHeld(KEY_F1+index)
				|| keyboard->KeyHeld(KEY_1+index)
				|| keyboard->KeyHeld(KEY_KP1+index) )
				directDiskSwapRequest |= (1 << index);
		}
	}
#endif
}

