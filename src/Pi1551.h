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

#ifndef PI1551_H
#define PI1551_H

#include "Drive1551.h"
#include "m6502.h"
#include "tcbm_bus.h"

// handled ports
// PA 0:7 DATA:TCBM - not
// PB 0:7 (head) - yes, Drive1551.cpp
// PC 7:DAV:TCBM - not
// PC 6:SYNC - yes, Drive1551.cpp (active low) ($1C00 bit 7, VIA#2 port B) ??? zanegowac
// PC 5:DEVNUM - yes, Pi1551.h SetDeviceID
// PC 4:MODE - yes, Drive1551.cpp 'writing' ??? zanegowane
// PC 3:ACK:TCBM - not
// PC 2:DEV:TCBM - not
// PC 01:STATUS:TCBM - not

// CPU ~ $1C00 (VIA#2 port B) GetPortCPU
// CPU 7:byte latch - yes, Drive1551.cpp (SO, VIA - CA1, ale chyba zanegowane bo 1=latched, 0=nie)
// CPU 56:density - yes, Drive1551.h OnPortOut
// CPU 4:wps - yes, Drive1551.cpp disk change
// CPU 3:led - yes, Drive1551.h OnPortOut
// CPU 2:motor - yes, Drive1551.h OnPortOut
// CPU 01:stepper - yes, Drive1551.h OnPortOut

class Pi1551
{

public:
	Pi1551();

	void Initialise();

	void Update();

	void Reset();

	//void ConfigureOfExtraRAM(bool extraRAM);

	Drive drive;
	m6523 TPI;

	M6502 m6502;

	inline void SetDeviceID(u8 id)
	{
		// bit 5 = 0 for #8, 1 for #9, this is inverted by ROM and put into bit 2 (DEV line - 0 for #9, 1 for #8)
		TPI.GetPortC()->SetInput(0x20, (id & 1) != 0);
	}

private:
	//u8 Memory[0xc000];

	//static u8 Read6502(u16 address, void* data);
	//static u8 Read6502ExtraRAM(u16 address, void* data);
	//static u8 Peek6502(u16 address, void* data);
	//static void Write6502(u16 address, const u8 value, void* data);
	//static void Write6502ExtraRAM(u16 address, const u8 value, void* data);
};

#endif