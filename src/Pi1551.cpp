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
		bool ramBoard = options.GetRAMBOard();
		unsigned romSize = roms.ROM1551Size;
		
		if (ramBoard && romSize == 16384)
		{
			// 16k ROM with RAMBOard=1
			if (address >= 0x8000 && address < 0xa000)
			{
				// 0x8000-0x9fff: RAM
				return s_u8Memory[address];
			}
			else if (address >= 0xa000 && address < 0xc000)
			{
				// 0xa000-0xbfff: return 0xff
				return 0xff;
			}
			else
			{
				// 0xc000-0xffff: ROM (16k ROM at 0xc000)
				return roms.Read1551(address);
			}
		}
		else if (ramBoard && romSize == 32768)
		{
			// 32k ROM with RAMBOard=1
			if (address >= 0x8000 && address < 0xa000)
			{
				// 0x8000-0x9fff: RAM (takes priority)
				return s_u8Memory[address];
			}
			else
			{
				// 0xa000-0xffff: ROM (32k ROM starting at 0x8000, but first 8k covered by RAM)
				return roms.Read1551(address);
			}
		}
		else if (!ramBoard && romSize == 32768)
		{
			// 32k ROM with RAMBOard=0
			// 0x8000-0xffff: ROM
			return roms.Read1551(address);
		}
		else
		{
			// 16k ROM with RAMBOard=0: default behavior
			switch (address & 0xe000) // keep bits 15,14,13
			{
				case 0x8000: // 0x8000-0x9fff
				case 0xa000: // 0xa000-0xbfff
				case 0xc000: // 0xc000-0xdfff
				case 0xe000: // 0xe000-0xffff
					return roms.Read1551(address);
			}
		}
	}
	// 0x0, 0x1 is CPU port
	else if (address == 0) {
		TCBM_Bus::ReadEmulationMode1551();
		return pi1551.TPI.GetPortCPU()->GetDirection();
	}
	else if (address == 1) {
		TCBM_Bus::ReadEmulationMode1551();
		return pi1551.TPI.ReadCPUPort();
	}
	// 0x4000-0x7fff is VIA
	else if (address >= 0x4000 && address < 0x8000) {
		TCBM_Bus::ReadEmulationMode1551();
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
		TCBM_Bus::ReadEmulationMode1551();
		return pi1551.TPI.GetPortCPU()->GetDirection();
	}
	else if (address == 1) {
		TCBM_Bus::ReadEmulationMode1551();
		return pi1551.TPI.ReadCPUPort();
	}
	// 0x4000-0x4008 is TIA
	else if (address >= 0x4000 && address < 0x4008) {
		TCBM_Bus::ReadEmulationMode1551();
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
		bool ramBoard = options.GetRAMBOard();
		unsigned romSize = roms.ROM1551Size;
		
		if (ramBoard && romSize == 16384)
		{
			// 16k ROM with RAMBOard=1
			if (address >= 0x8000 && address < 0xa000)
			{
				// 0x8000-0x9fff: RAM
				s_u8Memory[address] = value;
				return;
			}
			// 0xa000-0xbfff: read-only (returns 0xff), writes ignored
			// 0xc000-0xffff: ROM, writes ignored
			return;
		}
		else if (ramBoard && romSize == 32768)
		{
			// 32k ROM with RAMBOard=1
			if (address >= 0x8000 && address < 0xa000)
			{
				// 0x8000-0x9fff: RAM
				s_u8Memory[address] = value;
				return;
			}
			// 0xa000-0xffff: ROM, writes ignored
			return;
		}
		else if (!ramBoard && romSize == 32768)
		{
			// 32k ROM with RAMBOard=0
			// 0x8000-0xffff: ROM, writes ignored
			return;
		}
		else
		{
			// 16k ROM with RAMBOard=0: default behavior
			switch (address & 0xe000) // keep bits 15,14,13
			{
				case 0x8000: // 0x8000-0x9fff
				case 0xa000: // 0xa000-0xbfff
				case 0xc000: // 0xc000-0xdfff
				case 0xe000: // 0xe000-0xffff
					return; // ROM, writes ignored
			}
		}
	}
	// 0x0, 0x1 is CPU port
	else if (address == 0) {
		pi1551.TPI.GetPortCPU()->SetDirection(value);
		TCBM_Bus::RefreshOuts1551();
	}
	else if (address == 1) {
		pi1551.TPI.WriteCPUPort(value);
		TCBM_Bus::RefreshOuts1551();
	}
	// 0x4000-0x7fff is TIA
	else if (address >= 0x4000 && address < 0x8000) {
		pi1551.TPI.Write(address, value);
		TCBM_Bus::RefreshOuts1551();
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
		TCBM_Bus::RefreshOuts1551();
	}
	else if (address == 1) {
		pi1551.TPI.WriteCPUPort(value);
		TCBM_Bus::RefreshOuts1551();
	}
	// 0x4000-0x4008 is TIA
	else if (address >= 0x4000 && address < 0x4008) {
		pi1551.TPI.Write(address, value);
		TCBM_Bus::RefreshOuts1551();
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
		// 1551: keep SO internal; do not touch CPU V flag
		//This pin sets the overflow flag on a negative transition from TTL one to TTL zero.
		// SO is sampled at the trailing edge of P1, the cpu V flag is updated at next P1.
		//m6502.SO(); // not used in 1551 - latched when byte is ready, cleared when any TIA port is read
	}

	// TIA does nothing, but IRQ source is embedded there, a free running timer based on 555 with 10ms period (100Hz, 10000 cycles at 1MHz)
	TPI.Execute();
}

void Pi1551::Reset()
{
	TPI.Reset();
	drive.Reset();
	TCBM_Bus::Reset();
	// Ensure the CPU also performs a hardware reset so the reset vector is fetched
	// 1541/1581 don't reset the CPU, only the hardware
	//m6502.Reset();
}
