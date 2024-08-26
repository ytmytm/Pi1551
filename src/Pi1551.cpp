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

#include "Pi1551.h"
#include "debug.h"
#include "options.h"
#include "ROMs.h"

extern Options options;
extern Pi1551 pi1551;
extern u8 s_u8Memory[0xc000];
extern ROMs roms;

///////////////////////////////////////////////////////////////////////////////////////
// 6502 Address bus functions.
// Move here out of Pi1541 to increase performance.
///////////////////////////////////////////////////////////////////////////////////////
// In a 1541 address decoding and chip selects are performed by a 74LS42 ONE-OF-TEN DECODER
// 74LS42 Ouputs a low to the !CS based on the four inputs provided by address bits 10-13
// 1800 !cs2 on pin 9
// 1c00 !cs2 on pin 7
u8 read6502_1551(u16 address)
{
	if (address & 0x8000)
	{
		switch (address & 0xe000) // keep bits 15,14,13
		{
			case 0x8000: // 0x8000-0x9fff
				if (options.GetRAMBOard()) {
					return s_u8Memory[address]; // 74LS42 outputs low on pin 1 or pin 2
				}
			case 0xa000: // 0xa000-0xbfff
			case 0xc000: // 0xc000-0xdfff
			case 0xe000: // 0xe000-0xffff
				return roms.Read(address);
		}
	}
	// 0x0, 0x1 is CPU port
	else if (address == 0) {
		return pi1551.TPI.GetPortCPU()->GetDirection();
	}
	else if (address == 1) {
		return pi1551.TPI.ReadCPUPort();
	}
	// 0x4000-0x7fff is VIA
	else if (address >= 0x4000 && address < 0x8000) {
		return pi1551.TPI.Read(address);
	}
	// 0x0002-0x3FFF is RAM
	return s_u8Memory[address & 0x7ff];
}

// Allows a mode where we have RAM at all addresses other than the ROM and the VIAs. (Maybe useful to someone?)
u8 read6502ExtraRAM_1551(u16 address)
{
	if (address & 0x8000)
	{
		return roms.Read(address);
	}
	// 0x0, 0x1 is CPU port
	else if (address == 0) {
		return pi1551.TPI.GetPortCPU()->GetDirection();
	}
	else if (address == 1) {
		return pi1551.TPI.ReadCPUPort();
	}
	// 0x4000-0x4008 is TIA
	else if (address >= 0x4000 && address < 0x4008) {
		return pi1551.TPI.Read(address);
	}
	// otherwise it's RAM
	return s_u8Memory[address & 0x7fff];
}

// Use for debugging (Reads VIA registers without the regular VIA read side effects)
u8 peek6502_1551(u16 address)
{
	if (address & 0x8000)	// address line 15 selects the ROM
	{
		return roms.Read(address);
	}
	// 0x0, 0x1 is CPU port
	else if (address == 0) {
		return pi1551.TPI.GetPortCPU()->GetDirection();
	}
	else if (address == 1) {
		return pi1551.TPI.PeekCPUPort();
	}
	// 0x4000-0x7fff is TIA
	else if (address >= 0x4000 && address < 0x8000) {
		return pi1551.TPI.Peek(address);
	}
	// otherwise it's RAM
	return s_u8Memory[address & 0x7ff];
}

void write6502_1551(u16 address, const u8 value)
{
	if (address & 0x8000)
	{
		switch (address & 0xe000) // keep bits 15,14,13
		{
			case 0x8000: // 0x8000-0x9fff
				if (options.GetRAMBOard()) {
					s_u8Memory[address] = value; // 74LS42 outputs low on pin 1 or pin 2
					return;
					break;
				}
			case 0xa000: // 0xa000-0xbfff
			case 0xc000: // 0xc000-0xdfff
			case 0xe000: // 0xe000-0xffff
				return;
		}
	}
	// 0x0, 0x1 is CPU port
	else if (address == 0) {
		pi1551.TPI.GetPortCPU()->SetDirection(value);
	}
	else if (address == 1) {
		pi1551.TPI.WriteCPUPort(value);
	}
	// 0x4000-0x7fff is TIA
	else if (address >= 0x4000 && address < 0x8000) {
		pi1551.TPI.Write(address, value);
	}
	else {
		// otherwise it's RAM
		s_u8Memory[address & 0x7ff] = value;
	}
}

void write6502ExtraRAM_1551(u16 address, const u8 value)
{	
	if (address & 0x8000) return; // address line 15 selects the ROM
	// 0x0, 0x1 is CPU port
	if (address == 0) {
		pi1551.TPI.GetPortCPU()->SetDirection(value);
	}
	else if (address == 1) {
		pi1551.TPI.WriteCPUPort(value);
	}
	// 0x4000-0x4008 is TIA
	else if (address >= 0x4000 && address < 0x4008) {
		pi1551.TPI.Write(address, value);
	}
	else {
		// otherwise it's RAM
		s_u8Memory[address & 0x7fff] = value;
	}
}

Pi1551::Pi1551()
{
	TPI.ConnectIRQ(&m6502.IRQ);
}

void Pi1551::Initialise()
{
	TPI.ConnectIRQ(&m6502.IRQ);
}

//void Pi1551::ConfigureOfExtraRAM(bool extraRAM)
//{
//	if (extraRAM)
//		m6502.SetBusFunctions(this, Read6502ExtraRAM, Write6502ExtraRAM);
//	else
//		m6502.SetBusFunctions(this, Read6502, Write6502);
//}

void Pi1551::Update()
{
	if (drive.Update())
	{
		//This pin sets the overflow flag on a negative transition from TTL one to TTL zero.
		// SO is sampled at the trailing edge of P1, the cpu V flag is updated at next P1.
		m6502.SO(); //XXXMW not used in 1551
	}

	// TIA does nothing, but IRQ source is embedded there, a free running timer based on 555 with 10ms period (100Hz, 10000 cycles at 1MHz)
	TPI.Execute();
}

void Pi1551::Reset()
{
	TPI.Reset();
	drive.Reset();
	IEC_Bus::Reset(); // XXXMW
}
