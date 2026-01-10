/*
 *  dmRotary.cpp - A simple decoder to use a KY-040 rotary encoder for browse
 *                 naviation in Pi1541.
 * 
 *  Copyright © 2019 devMash.com
 * 
 *  https://devMash.com
 *  https://github.com/devMashHub
 * 
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of this
 *  software and associated documentation files (the “Software”), to deal in the Software
 *  without restriction, including without limitation the rights to use, copy, modify, merge,
 *  publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
 *  to whom the Software is furnished to do so, subject to the following conditions:
 *  
 *  The above copyright notice and this permission notice shall be included in all copies or
 *  substantial portions of the Software.
 *  
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
 * FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 * 
 */

#include "dmRotary.h"


//------------------------------------------------------------------------------
// Initialize
//
void RotaryEncoder::Initialize(rpi_gpio_pin_t clockGpioPin, rpi_gpio_pin_t dataGpioPin, rpi_gpio_pin_t switchGpioPin)
{ 

	#ifdef DM_ROTARY_DEBUG

		char message[1024] = "";

		WriteToMiniUart("##########\r\n\r\n");

		WriteToMiniUart("RotaryEncoder::Initialize()\r\n");

		sprintf(message, "  clockGpioPin = %d  dataGpioPin = %d  switchGpioPin = %d\r\n\r\n", clockGpioPin, dataGpioPin, switchGpioPin);
		WriteToMiniUart(message);

	#endif

	//Store specified pins for polling method
	_clockPin.SetGpioPin(clockGpioPin);
	_dataPin.SetGpioPin(dataGpioPin);
	_switchPin.SetGpioPin(switchGpioPin);

	//Set pins for input
	RPI_SetGpioInput(clockGpioPin);
	RPI_SetGpioInput(dataGpioPin);
	RPI_SetGpioInput(switchGpioPin);

	//Enable pull-ups	
	unsigned controlSignal = 2;
	unsigned gpioPinMask = _clockPin.GetGpioPinMask() | _dataPin.GetGpioPinMask() | _switchPin.GetGpioPinMask();
	SetGpioPullUpDown(controlSignal, gpioPinMask);

}


//------------------------------------------------------------------------------
// Poll
//
rotary_result_t RotaryEncoder::Poll()
{

	//Read physical pin levels (GPLEV0 is pins 0 to 31)
	unsigned gplev0 = read32(ARM_GPIO_GPLEV0);
	
	return Poll(gplev0);

}

//------------------------------------------------------------------------------
// Poll
//
rotary_result_t RotaryEncoder::Poll(unsigned gplev0)
{ 

	rotary_result_t result = NoChange;

	#ifdef DM_ROTARY_DEBUG

		char message[1024] = "";

	#endif

	//Decode switch
	if (result == NoChange)
	{

		//Debounce switch and determine state
		_switchPin.Update((gplev0 & _switchPin.GetGpioPinMask()) == 0);
		bool switchState = _switchPin.GetState();

		//Detect switch state change
		if (switchState != _currentSwitchState)
		{

			//Determine result
			result = switchState ? ButtonDown : ButtonUp;

			//Update switch state
			_currentSwitchState = switchState;

		}

	}

	//Decode rotation - simple method: detect CLK edge and check DT state
	if (result == NoChange)
	{
		// Read current pin states (active low, so == 0 means pressed/active)
		bool clockState = (gplev0 & _clockPin.GetGpioPinMask()) == 0;
		bool dataState = (gplev0 & _dataPin.GetGpioPinMask()) == 0;

		// Simple method: detect CLK rising edge (LOW to HIGH) and check DT state
		// This detects each "click" of the encoder
		if (clockState != _currentRotaryState)
		{
			// CLK state changed
			if (clockState)  // CLK went from LOW to HIGH (rising edge)
			{
				// On rising edge of CLK, check DT state to determine direction
				// If DT != CLK (DT is LOW when CLK is HIGH) -> clockwise
				// If DT == CLK (DT is HIGH when CLK is HIGH) -> counter-clockwise
				if (!dataState)  // DT is LOW (different from CLK which is HIGH) -> clockwise
				{
					result = RotatePositive;
				}
				else  // DT is HIGH (same as CLK which is HIGH) -> counter-clockwise
				{
					result = RotateNegative;
				}
			}
			// Only process on rising edge to avoid double-counting
			// Falling edge is ignored for simplicity

			// Update state (using _currentRotaryState as last clock state)
			_currentRotaryState = clockState;
		}
	}

	#ifdef DM_ROTARY_DEBUG

		switch (result)
		{

			case NoChange:
				break;

			case ButtonDown:
				WriteToMiniUart("Button Down\r\n");
				break;

			case ButtonUp:
				WriteToMiniUart("Button Up\r\n");
				break;

			case RotatePositive:
				WriteToMiniUart("Clockwise\r\n");
				break;

			case RotateNegative:
				WriteToMiniUart("Counter-Clockwise\r\n");
				break;

		}

	#endif

	return result;

}


//------------------------------------------------------------------------------
// SetGpioPullUpDown
//
void RotaryEncoder::SetGpioPullUpDown(unsigned controlSignal, unsigned gpioPinMask)
{

	volatile int i;
	int delayCycles = 150;

	// Write to GPPUD to set the required control signal
	//
	//    01 = Enable Pull-Down		00 = Off (disable pull-up/down)
	//	  10 = Enable Pull-Up 		11 = Reserved
	//
	write32(ARM_GPIO_GPPUD, controlSignal);

	// Delay cycles (to provide the required set-up time for the control signal)
	for (i = 0; i < delayCycles; i++) { }

	// Write to GPPUDCLK0/1 to clock the control signal into the GPIO pads
	//
	// 	Note: Only the pads which receive a clock will be modified, all others will
	//		  retain their previous state.
	//
	write32(ARM_GPIO_GPPUDCLK0, gpioPinMask);

	// Delay cycles (to provide the required hold time for the control signal)
	for (i = 0; i < delayCycles; i++) { }

	// Write to GPPUD to remove the control signal
	write32(ARM_GPIO_GPPUD, 0);

	//Write to GPPUDCLK0/1 to remove the clock
	write32(ARM_GPIO_GPPUDCLK0, 0);

}


#ifdef DM_ROTARY_DEBUG

//------------------------------------------------------------------------------
// WriteToMiniUart
//
void RotaryEncoder::WriteToMiniUart(char* pMessage) 
{
	while(*pMessage)
	{
		RPI_AuxMiniUartWrite(*pMessage++);
	}
}

#endif