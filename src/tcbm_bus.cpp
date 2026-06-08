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
#include "options.h"

extern Options options;

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
volatile bool TCBM_Bus::rotaryDiskStepNext = false;
volatile bool TCBM_Bus::rotaryDiskStepPrev = false;
u8 TCBM_Bus::lastDirA = 0xff; // force initial programming of pin functions
u8 TCBM_Bus::lastDirC = 0xff; // force initial programming of Port C functions
u32 TCBM_Bus::lastSet = 0;
u32 TCBM_Bus::lastClear = 0;
u8 TCBM_Bus::lastOutA = 0xff; // invalid
u8 TCBM_Bus::lastOutC = 0xff; // invalid
bool TCBM_Bus::lastOutputLED = false;
bool TCBM_Bus::lastOutputSound = false;

// TCBM bus inputs sampled by the drive emulator (long cable / missing series R can glitch).
static const u32 TCBM_GPIO_INPUT_MASK =
	PIGPIO_MASK_ANY_DIO | PIGPIO_MASK_IN_DAV |
	PIGPIO_MASK_OUT_STATUS0 | PIGPIO_MASK_OUT_STATUS1 |
	PIGPIO_MASK_OUT_ACK | PIGPIO_MASK_OUT_DEV |
	PIGPIO_MASK_IN_RESET;

static const unsigned TCBM_GPIO_STABLE_MAX_ATTEMPTS = 32;

static u32 ReadStableGplev0ForTCBM(void)
{
	u32 sample = read32(ARM_GPIO_GPLEV0);
	for (unsigned attempt = 0; attempt < TCBM_GPIO_STABLE_MAX_ATTEMPTS; ++attempt)
	{
		u32 next = read32(ARM_GPIO_GPLEV0);
		if ((sample & TCBM_GPIO_INPUT_MASK) == (next & TCBM_GPIO_INPUT_MASK))
			return next;
		sample = next;
	}
	return sample;
}

void TCBM_Bus::PollGPIOInputs1551(void)
{
	gplev0 = ReadStableGplev0ForTCBM();
	Resetting = !ignoreReset && ((gplev0 & PIGPIO_MASK_IN_RESET) != (PIGPIO_MASK_IN_RESET));
}

void TCBM_Bus::ReadGPIOUserInput()
{
	ReadGPIOUserInput(gplev0);
}

void TCBM_Bus::PollGPIOUserInput1551()
{
	ReadGPIOUserInput(read32(ARM_GPIO_GPLEV0));
}

void TCBM_Bus::ConsumeRotaryDiskSteps(bool& nextDisk, bool& prevDisk)
{
	nextDisk = rotaryDiskStepNext;
	prevDisk = rotaryDiskStepPrev;
	rotaryDiskStepNext = false;
	rotaryDiskStepPrev = false;
}

void TCBM_Bus::ClearRotaryDiskSteps()
{
	rotaryDiskStepNext = false;
	rotaryDiskStepPrev = false;
}

void TCBM_Bus::ReadGPIOUserInput(unsigned gpioLevel)
{
	//ROTARY: Added for rotary encoder support - 09/05/2019 by Geo...
	if (TCBM_Bus::rotaryEncoderEnable == true)
	{
		int indexEnter = InputMappings::INPUT_BUTTON_ENTER;
		int indexUp = InputMappings::INPUT_BUTTON_UP;
		int indexDown = InputMappings::INPUT_BUTTON_DOWN;
		int indexBack = InputMappings::INPUT_BUTTON_BACK;
		int indexInsert = InputMappings::INPUT_BUTTON_INSERT;

		// Static counters to track rotation button hold duration
		// Buttons are held for a few cycles to ensure they are detected
		static int rotationUpCounter = 0;
		static int rotationDownCounter = 0;
		const int ROTATION_BUTTON_HOLD_CYCLES = 5; // Hold button for 5 cycles

		// Decrement counters and reset buttons when counter reaches zero
		if (rotationUpCounter > 0)
		{
			rotationUpCounter--;
			if (rotationUpCounter == 0)
			{
				SetButtonState(indexUp, false);
			}
		}
		if (rotationDownCounter > 0)
		{
			rotationDownCounter--;
			if (rotationDownCounter == 0)
			{
				SetButtonState(indexDown, false);
			}
		}

		//Poll the rotary encoder
		//
		// Note: If the rotary encoder returns any value other than 'NoChange' an
		//       event has been detected.  We force the button state of the original
		//       input button registers to reflect the desired action, and allow the
		//       original processing logic to do it's work.
		//
		rotary_result_t rotaryResult = TCBM_Bus::rotaryEncoder.Poll(gpioLevel);
		switch (rotaryResult)
		{

			case ButtonDown:
				SetButtonState(indexEnter, true);
				break;

			case ButtonUp:
				SetButtonState(indexEnter, false);
				break;

			case RotateNegative:
				SetButtonState(indexUp, true);
				rotationUpCounter = ROTATION_BUTTON_HOLD_CYCLES;
				rotaryDiskStepNext = true;
				break;

			case RotatePositive:
				SetButtonState(indexDown, true);
				rotationDownCounter = ROTATION_BUTTON_HOLD_CYCLES;
				rotaryDiskStepPrev = true;
				break;

			case NoChange:
			default:
				// No rotation detected - buttons will be reset in next cycle if they were set
				break;

		}

		UpdateButton(indexBack, gpioLevel);
		UpdateButton(indexInsert, gpioLevel);
	}
	else // Unmolested original logic
	{

		int index;
		for (index = 0; index < buttonCount; ++index)
		{
			UpdateButton(index, gpioLevel);
		}

	}
}


//ROTARY: Modified for rotary encoder support - 09/05/2019 by Geo...
/// @brief read real I/O pins in browser mode + read the rotary encoder/buttons
///        don't update TIA port status
/// @param  
void TCBM_Bus::ReadBrowseMode(void)
{
	ReadEmulationMode1551(false);
	ReadGPIOUserInput();
}

/// @brief read real I/O pins before emulation step in emulation mode
/// @param  
void TCBM_Bus::ReadEmulationMode1551(bool updateTIAStatus)
{
    gplev0 = ReadStableGplev0ForTCBM();

    if (TPI)
    {
        IOPort* portC = TPI->GetPortC();
        u8 ddrC = portC->GetDirection();

        PI_DAV  = (gplev0 & PIGPIO_MASK_IN_DAV) == PIGPIO_MASK_IN_DAV;
        // Port C: only bits 0,1,2,3,7 come from TCBM GPIO (ST0, ST1, DEV, ACK, DAV).
        // Bits 4,5,6 (MODE, DEVNUM, SYNC) are set by drive emulation (Drive1551) - do not overwrite.
        u8 portCFromGPIO = (PI_DAV ? 0x80 : 0) |
            ((gplev0 & PIGPIO_MASK_OUT_STATUS0) ? 0x01 : 0) |
            ((gplev0 & PIGPIO_MASK_OUT_STATUS1) ? 0x02 : 0) |
            ((gplev0 & PIGPIO_MASK_OUT_DEV) ? 0x04 : 0) |
            ((gplev0 & PIGPIO_MASK_OUT_ACK) ? 0x08 : 0);
        u8 portCInput = (portC->GetOutput() & ddrC) |
            ((portCFromGPIO & 0x8Fu) | (portC->GetInput() & 0x70u)) & (u8)~ddrC;
        portC->SetInput(portCInput);

        IOPort* portA = TPI->GetPortA();
        u8 ddrA = portA->GetDirection();
        DataSetToOut = ddrA != 0;
        // Port A input bits from GPIO (same as port C), only where DDRA says input
        PI_Data = ((gplev0 & PIGPIO_MASK_DIO1) ? 0x01 : 0x00) |
            ((gplev0 & PIGPIO_MASK_DIO2) ? 0x02 : 0x00) |
            ((gplev0 & PIGPIO_MASK_DIO3) ? 0x04 : 0x00) |
            ((gplev0 & PIGPIO_MASK_DIO4) ? 0x08 : 0x00) |
            ((gplev0 & PIGPIO_MASK_DIO5) ? 0x10 : 0x00) |
            ((gplev0 & PIGPIO_MASK_DIO6) ? 0x20 : 0x00) |
            ((gplev0 & PIGPIO_MASK_DIO7) ? 0x40 : 0x00) |
            ((gplev0 & PIGPIO_MASK_DIO8) ? 0x80 : 0x00);
        // For 6523: output bits use latch (GetOutput), input bits use GPIO (PI_Data)
        u8 portARead = (portA->GetOutput() & ddrA) | (PI_Data & (u8)~ddrA);
        portA->SetInput(portARead);

        if (updateTIAStatus)
        {
            // Update PI_ACK and PI_Status from TPI port C outputs
            u8 portCOutput = portC->GetOutput();
            PI_ACK = (portCOutput & 0x08) != 0;
            PI_Status = portCOutput & 0x03;
        }
    }
    else
    {
        // Browse mode: read GPIO directly, do not touch TPI ports
        PI_DAV  = (gplev0 & PIGPIO_MASK_IN_DAV) == PIGPIO_MASK_IN_DAV;
        if (!DataSetToOut)
        {
            PI_Data = ((gplev0 & PIGPIO_MASK_DIO1) ? 0x01 : 0x00) |
                ((gplev0 & PIGPIO_MASK_DIO2) ? 0x02 : 0x00) |
                ((gplev0 & PIGPIO_MASK_DIO3) ? 0x04 : 0x00) |
                ((gplev0 & PIGPIO_MASK_DIO4) ? 0x08 : 0x00) |
                ((gplev0 & PIGPIO_MASK_DIO5) ? 0x10 : 0x00) |
                ((gplev0 & PIGPIO_MASK_DIO6) ? 0x20 : 0x00) |
                ((gplev0 & PIGPIO_MASK_DIO7) ? 0x40 : 0x00) |
                ((gplev0 & PIGPIO_MASK_DIO8) ? 0x80 : 0x00);
        }
    }

    // RESET is active-low on the TCBM bus: asserted when input is 0
    Resetting = !ignoreReset && ((gplev0 & PIGPIO_MASK_IN_RESET) != (PIGPIO_MASK_IN_RESET));
}

/// @brief Set real I/O pins / directions after emulation step in emulation mode
/// @param  
void TCBM_Bus::RefreshOuts1551(void)
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
		// Early exit only for TPI port state; LED/buzzer are pushed unconditionally below.
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
	lastOutputLED = OutputLED;
	lastOutputSound = OutputSound;

	// LED is not a TPI register; drive every call (IEC RefreshOuts1541 style).
	unsigned ledSoundClear = 0;
	unsigned ledSoundSet = 0;
	if (OutputLED) ledSoundSet |= 1u << PIGPIO_OUT_LED;
	else ledSoundClear |= 1u << PIGPIO_OUT_LED;
	// SoundOnGPIO=1: square-wave buzzer on GPIO13.  SoundOnGPIO=0: PWM sample via ALT0 — do not drive digitally.
	if (options.SoundOnGPIO())
	{
		if (OutputSound) ledSoundSet |= 1u << PIGPIO_OUT_SOUND;
		else ledSoundClear |= 1u << PIGPIO_OUT_SOUND;
	}
	write32(ARM_GPIO_GPCLR0, ledSoundClear);
	write32(ARM_GPIO_GPSET0, ledSoundSet);
}

// called whenever the emulated 6502 writes to port A ($4000) or DDRA ($4003)
// pUserData is a pointer given when function is attached
// status is status & ddr, we should ignore it
void TCBM_Bus::PortA_OnPortOut(void* pUserData, unsigned char status)
{
	if (TPI && port)
	{
		TPI_Data = port->GetOutput();
		DataSetToOut = port->GetDirection() != 0;
		// GPIO refresh: write6502_1551 / write6502ExtraRAM_1551 call RefreshOuts1551() after TPI.Write.
	}
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

	// TPI/port are already reset (all directions 0 = input). Sync GPIO to that state so DIO pins are input+pull-up.
	if (TPI && port)
		RefreshOuts1551();
}
