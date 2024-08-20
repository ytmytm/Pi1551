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

#ifndef M6523_H
#define M6523_H

#include "IOPort.h"
#include "m6502.h"

class m6523
{

// CPU port is not part of TIA, but there is only one TIA in 1551 and this neatly encapsulates it

// ; TIA 6523 registers $4000-$4005
// ;
// ; Port registers A, B and C
// V_4000 = $4000	; I/O port towards the computer                         TCBM-IO-0:7 (bidir)
// V_4001 = $4001	; connected to the shift register of the R/W head       VIA2 PA $1C01
// V_4002 = $4002	; drive control, status and sync register
// ;
// ; bit 7: DAV (DAta Valid)                                               TCBM-IO-8 (input)
// ; 	handshake from bit #6 from $FEF2/FEC2 of plus/4
// ; bit 6: SYNC                                                           $1C00-7
// ; 	0 -> SYNC found
// ; 	1 -> no SYNC
// ; bit 5: DEVNUM (jumper)                                                  $1800-6/5
// ; 	drive number (0 -> 8, 1 -> 9)                                       (jumper to GND, normally 0 maps->drive 8, DEV=bit 2 set) [setting from config file]
// ; bit 4: MODE (R/W head)                                                CB2 VIA#2
// ; 	0 -> head in write mode
// ; 	1 -> head in read mode
// ; bit 3: ACK (ACKnowledge)                                              TCBM-IO-9 (output)
// ; 	handshake to bit #7 of $FEF2/$FEC2
// ; bit 2: DEV                                                            TCBM-IO-10 $1800-6/5 (zanegowany bit 5) (output)
// ; 	1 -> drive mapped at $FEF0 on plus/4 #8
// ; 	0 -> drive mapped at $FEC0 on plus/4 #9
// ; bit 1: STATUS1 - mapped at $FEF1/FEC1 on plus/4                       TCBM-IO-11 (output)
// ; bit 0: STATUS0 - mapped at $FEF1/FEC1 on plus/4                       TCBM-IO-12 (output)
// ; 		  01 Timeout during reading
// ; 		  10 Timeout during writing
// ; 		  11 End of data
// ;
// ; Data direction registers for ports A, B and C
// V_4003 = $4003
// V_4004 = $4004
// V_4005 = $4005

	// $1800
	// PB 0		data in
	// PB 1		data out
	// PB 2		clock in
	// PB 3		clock out
	// PB 4		ATNA out
	// PB 5,6	device address
	// PB 7,CA1	ATN IN

	// $1C00
	// PB 0,1	step motor
	// PB 2		MTR dirve motor
	// PB 3		ACT drive LED
	// PB 4		WPS	write protect switch
	// PB 5,6	bit rate
	// PB 7		Sync
	// CA 1		Byte ready
	// CA 2		SOE set overflow enable 6502
	// CB 2		read/write

	enum Registers
	{
		PA,		// 0 Port A
		PB,		// 1 Port B
		PC,		// 2 Port C
		DDRA,	// 3 Data direction register for port A
		DDRB,	// 4 Data direction register for port B
		DDRC	// 5 Data direction register for port C
	};

public:

	m6523();

	void Reset();
	void ConnectIRQ(Interrupt* irq) { this->irq = irq; }

	inline IOPort* GetPortA() { return &portA; }
	inline IOPort* GetPortB() { return &portB; }
	inline IOPort* GetPortC() { return &portC; }
	inline IOPort* GetPortCPU() { return &portCPU; }

	void Execute();

	unsigned char Read(unsigned int address);
	unsigned char Peek(unsigned int address);
	void Write(unsigned int address, unsigned char value);

	inline unsigned char ReadCPUPort()
	{
		unsigned char ddr = portCPU.GetDirection();
		unsigned char value = (unsigned char)((portCPU.GetInput() & ~ddr) | (portCPU.GetOutput() & ddr));
		return value;
	}

	inline unsigned char PeekCPUPort()
	{
		return ReadCPUPort();
	}

	inline void WriteCPUPort(unsigned char value)
	{
		portCPU.SetOutput(value);
	}

private:

	inline unsigned char ReadPortA()
	{
		unsigned char ddr = portA.GetDirection();
		unsigned char value = (unsigned char)((portA.GetInput() & ~ddr) | (portA.GetOutput() & ddr));
		return value;
	}

	inline unsigned char PeekPortA()
	{
		return ReadPortA();
	}

	inline void WritePortA(unsigned char value)
	{
		portA.SetOutput(value);
	}

	inline unsigned char ReadPortB()
	{
		unsigned char ddr = portB.GetDirection();
		unsigned char value = (unsigned char)((portB.GetInput() & ~ddr) | (portB.GetOutput() & ddr));
		return value;
	}

	inline unsigned char PeekPortB()
	{
		return ReadPortB();
	}

	inline void WritePortB(unsigned char value)
	{
		portB.SetOutput(value);
	}

	inline unsigned char ReadPortC()
	{
		unsigned char ddr = portC.GetDirection();
		unsigned char value = (unsigned char)((portC.GetInput() & ~ddr) | (portC.GetOutput() & ddr));
		return value;
	}

	inline unsigned char PeekPortC()
	{
		return ReadPortC();
	}

	inline void WritePortC(unsigned char value)
	{
		portC.SetOutput(value);
	}

	IOPort portA;
	IOPort portB;
	IOPort portC;
	IOPort portCPU;

	Interrupt* irq;

	unsigned int ne555counter;
};

#endif