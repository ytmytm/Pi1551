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

#ifndef ROMs_H
#define ROMs_H

#include "defs.h"
#include "types.h"

class Screen;
class ScreenBase;

class ROMs
{
public:
	ROMs() :
		currentROMIndex(0),
		lastManualSelectedROMIndex(0),
		longestRomNameLen(0)
#if defined(PI1551SUPPORT)
		, ROM1551SlotCount(0)
#endif
	{
#if defined(PI1551SUPPORT)
		for (unsigned i = 0; i < MAX_ROMS; ++i)
			ROM1551Sizes[i] = 0;
#endif
	}

	void SelectROM(const char* ROMName);
	void SelectROMIndex(unsigned index);

	inline u8 Read(u16 address)
	{
		return ROMImages[currentROMIndex][address & 0x3fff];
	}
	inline u8 Read1551(u16 address)
	{
#if defined(PI1551SUPPORT)
		unsigned romSize = Get1551ROMSize();
		if (romSize == 32768)
			return ROMImage1551[currentROMIndex][address & 0x7fff];
		return ROMImage1551[currentROMIndex][address & 0x3fff];
#else
		if (ROM1551Size == 32768)
			return ROMImage1551[address & 0x7fff];
		return ROMImage1551[address & 0x3fff];
#endif
	}
	inline u8 Read1581(u16 address)
	{
		return ROMImage1581[address & 0x7fff];
	}

	void ResetCurrentROMIndex();

	static const int ROM_SIZE = 16384;
	static const int ROM1551_SIZE = 32768;  // Support both 16k and 32k ROMs
	static const int ROM1581_SIZE = 16384 * 2;
	static const int MAX_ROMS = 7;

	unsigned char ROMImages[MAX_ROMS][ROM_SIZE];
#if defined(PI1551SUPPORT)
	unsigned char ROMImage1551[MAX_ROMS][ROM1551_SIZE];
	unsigned ROM1551Sizes[MAX_ROMS];
	unsigned ROM1551SlotCount;
#else
	unsigned char ROMImage1551[ROM1551_SIZE];
	unsigned ROM1551Size;  // Actual size of loaded ROM: 16384 (16k) or 32768 (32k)
#endif
	unsigned char ROMImage1581[ROM1581_SIZE];
	char ROMName1551[256];
	char ROMName1581[256];
	char ROMNames[MAX_ROMS][256];
	bool ROMValid[MAX_ROMS];

	unsigned currentROMIndex;
	unsigned lastManualSelectedROMIndex;

	unsigned GetLongestRomNameLen() { return longestRomNameLen; }
	unsigned UpdateLongestRomNameLen(unsigned maybeLongest);

	const char* GetSelectedROMName() const
	{
		return ROMNames[currentROMIndex];
	}

#if defined(PI1551SUPPORT)
	unsigned Get1551ROMSize() const { return ROM1551Sizes[currentROMIndex]; }
	unsigned CountValid1551ROMs() const;
	unsigned NextValid1551ROMIndex(unsigned from, int delta) const;
	void Display1551ROMSelector(unsigned selectedIndex, Screen* screen, ScreenBase* screenLCD, bool inEmulation);
#endif

protected:
	unsigned longestRomNameLen;
};

#endif
