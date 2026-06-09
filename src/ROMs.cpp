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
#include "ROMs.h"
#include "debug.h"
#if defined(PI1551SUPPORT)
#include "tape_player.h"
extern TapePlayer* g_tapePlayer;
#endif
#include <strings.h>
#include <string.h>
#include <stdio.h>
#if not defined(EXPERIMENTALZERO)
#include "Screen.h"
#include "ScreenLCD.h"
#endif

void ROMs::ResetCurrentROMIndex()
{
	currentROMIndex = lastManualSelectedROMIndex;

	DEBUG_LOG("Reset ROM back to %d %s\r\n", currentROMIndex, ROMNames[currentROMIndex]);
}

void ROMs::SelectROM(const char* ROMName)
{
	unsigned index;

	for (index = 0; index < MAX_ROMS; ++index)
	{
		if (ROMNames[index] && strcasecmp(ROMNames[index], ROMName) == 0)
		{
			DEBUG_LOG("LST switching ROM %d %s\r\n", index, ROMNames[index]);

			currentROMIndex = index;
			break;
		}
	}
}

void ROMs::SelectROMIndex(unsigned index)
{
	if (index < MAX_ROMS && ROMValid[index])
	{
		currentROMIndex = index;
		lastManualSelectedROMIndex = index;
	}
}

#if defined(PI1551SUPPORT)
unsigned ROMs::CountValid1551ROMs() const
{
	unsigned count = 0;
	for (unsigned i = 0; i < MAX_ROMS; ++i)
	{
		if (ROMValid[i])
			++count;
	}
	return count;
}

unsigned ROMs::NextValid1551ROMIndex(unsigned from, int delta) const
{
	if (CountValid1551ROMs() <= 1)
		return from;

	unsigned idx = from;
	for (unsigned step = 0; step < MAX_ROMS; ++step)
	{
		idx = (idx + MAX_ROMS + (unsigned)delta) % MAX_ROMS;
		if (ROMValid[idx])
			return idx;
	}
	return from;
}

void ROMs::Display1551ROMSelector(unsigned selectedIndex, Screen* screen, ScreenBase* screenLCD, bool inEmulation)
{
	static const u32 screenPosXCaddySelections = 240;
	static const u32 screenPosYCaddySelections = 280;
	static char buffer[256];
	static u32 white = RGBA(0xff, 0xff, 0xff, 0xff);
	static u32 redDark = RGBA(0x88, 0, 0, 0xff);
	static u32 grey = RGBA(0x88, 0x88, 0x88, 0xff);
	static u32 greyDark = RGBA(0x44, 0x44, 0x44, 0xff);
	unsigned romIndex;
	int x;
	int y;

#if not defined(EXPERIMENTALZERO)
	if (screen)
	{
		x = screen->ScaleX(screenPosXCaddySelections);
		y = screen->ScaleY(screenPosYCaddySelections);

		snprintf(buffer, 256, "                                                        ");
		screen->PrintText(false, x, y, buffer, RGBA(0xff, 0xff, 0xff, 0xff), redDark);
		snprintf(buffer, 256, "  Select ROM");
		screen->PrintText(false, x, y, buffer, RGBA(0xff, 0xff, 0xff, 0xff), redDark);
		y += 16;

		for (romIndex = 0; romIndex < MAX_ROMS; ++romIndex)
		{
			if (!ROMValid[romIndex])
				continue;

			const char* name = ROMNames[romIndex];
			snprintf(buffer, 256, "                                                        ");
			screen->PrintText(false, x, y, buffer, grey, greyDark);
			if (romIndex == selectedIndex)
				snprintf(buffer, 256, "* %d %s", romIndex + 1, name);
			else
				snprintf(buffer, 256, "  %d %s", romIndex + 1, name);
			screen->PrintText(false, x, y, buffer, romIndex == selectedIndex ? white : grey, greyDark);
			y += 16;
		}
	}
#endif

	if (screenLCD)
	{
		unsigned fontHeight = screenLCD->GetFontHeight();
		unsigned totalRows = screenLCD->Height() / fontHeight;
		unsigned tapeRows = 0;
#if defined(PI1551SUPPORT)
		if (inEmulation && g_tapePlayer && g_tapePlayer->IsLoaded())
			tapeRows = 1;
#endif
		unsigned reservedRows = inEmulation ? (1 + tapeRows) : 0;
		unsigned visibleRows = totalRows;
		if (totalRows > reservedRows)
			visibleRows = totalRows - reservedRows;
		unsigned scrollOffset = 0;
		unsigned slot;
		u32 lcdX = 0;
		u32 lcdY = 0;
		RGBA BkColour = RGBA(0, 0, 0, 0xFF);

#if defined(PI1551SUPPORT)
		if (inEmulation && tapeRows)
			lcdY = fontHeight * 2;
		else if (inEmulation)
			lcdY = fontHeight;
#endif

		unsigned validCount = CountValid1551ROMs();
		if (validCount > visibleRows && visibleRows > 0 && selectedIndex >= visibleRows)
		{
			if (selectedIndex >= validCount - visibleRows)
				scrollOffset = validCount - visibleRows;
			else
				scrollOffset = selectedIndex - visibleRows + 1;
		}

		unsigned visibleIndex = 0;
		for (romIndex = 0; romIndex < MAX_ROMS; ++romIndex)
		{
			if (!ROMValid[romIndex])
				continue;
			if (visibleIndex >= scrollOffset)
				break;
			++visibleIndex;
		}

		for (slot = romIndex; slot < MAX_ROMS; ++slot)
		{
			if (!ROMValid[slot])
				continue;

			memset(buffer, ' ', screenLCD->Width() / screenLCD->GetFontWidth());
			screenLCD->PrintText(false, lcdX, lcdY, buffer, BkColour, BkColour);
			snprintf(buffer, 256, "%d %s", slot + 1, ROMNames[slot]);
			screenLCD->PrintText(false, lcdX, lcdY, buffer, 0, slot == selectedIndex ? RGBA(0xff, 0xff, 0xff, 0xff) : BkColour);
			lcdY += fontHeight;
			if (lcdY >= screenLCD->Height())
				break;
		}

		while (lcdY < screenLCD->Height())
		{
			memset(buffer, ' ', screenLCD->Width() / screenLCD->GetFontWidth());
			screenLCD->PrintText(false, lcdX, lcdY, buffer, BkColour, BkColour);
			lcdY += fontHeight;
		}
		screenLCD->SwapBuffers();
	}
}
#endif

unsigned ROMs::UpdateLongestRomNameLen( unsigned maybeLongest )
{
	if (maybeLongest > longestRomNameLen)
		longestRomNameLen = maybeLongest;

	return longestRomNameLen;
}
