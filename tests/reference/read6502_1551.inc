// Pi1551 - Commodore 1551 disk drive emulator
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

// Frozen CPU read decoder before the ROM fast-path optimization.
// Copied from src/Pi1551.cpp; GPL-3.0-or-later, same license as the source.
u8 read6502_1551_reference(u16 address)
{
	if (address & 0x8000)
	{
		bool ramBoard = options.GetRAMBOard();
		unsigned romSize = roms.Get1551ROMSize();

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
