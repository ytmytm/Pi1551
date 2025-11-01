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

#ifndef IEC_COMMANDS_H
#define IEC_COMMANDS_H

#include "commands_base.h"
#include "iec_bus.h"

class IEC_Commands : public Commands_Base
{
public:
	IEC_Commands();
	void Initialise();

	void SetAutoBootFB128(bool autoBootFB128) { this->autoBootFB128 = autoBootFB128; }
	void Set128BootSectorName(const char* SectorName) 
	{
		if (SectorName && SectorName[0])
			this->C128BootSectorName = SectorName;
		else
			this->C128BootSectorName = 0;
	}

	void Reset(void);
	void SimulateIECBegin(void);
	UpdateAction SimulateIECUpdate(void);

protected:
	bool CheckATN(void);
	bool WriteSerialPortByte(u8 data, bool eoi) override;
	bool ReadSerialPortByte(u8& byte, bool eoi = false) override;
	void ReadBrowseMode() { IEC_Bus::ReadBrowseMode(); }
	bool IsReset() { return IEC_Bus::IsReset(); }

	bool HandleSpecialOpenFile(Channel& channel);
	void User(void);

	// IEC-specific members
	bool usingVIC20 : 1;	// When sending data we need to wait longer for the 64 as its VICII may be stealing its cycles. VIC20 does not have this problem and can accept data faster.
	bool autoBootFB128 : 1;
	const char* C128BootSectorName;
};
#endif

