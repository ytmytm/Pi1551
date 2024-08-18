// Pi1541 - A Commodore 1541 disk drive emulator
// Copyright(C) 2018 Stephen White
// Copyright(C) 2024 Maciej Witkowiak
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

#include "m6523.h"

// MOS 6523 TIA is a simple (comparing to VIA or CIA) three port device
// 1551 needs also 8-bit CPU port at $00 (DDR) / $01 (PORT) (implement also using IOPort.h)
// and IRQ source, a free running timer based on 555 with 10ms period (100Hz, 10000 cycles at 1MHz)

m6523::m6523()
{
	Reset();
}

void m6523::Reset()
{
	// ports to input on reset
	portA.SetDirection(0);
	portB.SetDirection(0);
	portC.SetDirection(0);
	portCPU.SetDirection(0);
	// reset NE555
	ne555counter = 0;
}

// Update for a single cycle
void m6523::Execute()
{
	// IRQ from NE555 active for a single cycle only
	if (ne555counter == 0) {
		if (irq) irq->Release();
	}
	ne555counter++;
	if (ne555counter >= 10000) { // 100Hz, 10ms period, cycles counted at 1MHz
		ne555counter = 0;
		if (irq) irq->Assert();
	}
}

unsigned char m6523::Read(unsigned int address)
{
	unsigned char value = 0;

	switch (address & 0x7)
	{
		case PA:
			value = ReadPortA();
		break;
		case PB:
			value = ReadPortB();
		break;
		case PC:
			value = ReadPortC();
		break;
		case DDRA:
			value = portA.GetDirection();
		break;
		case DDRB:
			value = portB.GetDirection();
		break;
		case DDRC:
			value = portC.GetDirection();
		break;
	}
	return value;
}

unsigned char m6523::Peek(unsigned int address)
{
	unsigned char value = 0;

	switch (address & 0x7)
	{
		case PA:
			value = PeekPortA();
		break;
		case PB:
			value = PeekPortB();
		break;
		case PC:
			value = PeekPortC();
		break;
		case DDRA:
			value = portA.GetDirection();
		break;
		case DDRB:
			value = portB.GetDirection();
		break;
		case DDRC:
			value = portC.GetDirection();
		break;
	}
	return value;
}

void m6523::Write(unsigned int address, unsigned char value)
{
	unsigned char ddr;

	switch (address & 0x7)
	{
		case PA:
			WritePortA(value);
		break;
		case PB:
			WritePortB(value);
		break;
		case PC:
			WritePortC(value);
		break;
		case DDRA:
			portA.SetDirection(value);
			// XXXMW DDR change here must change real GPIO DDR (TCBM data bus) too
		break;
		case DDRB:
			portB.SetDirection(value);
		break;
		case DDRC:
			portC.SetDirection(value);
		break;
	}
}
