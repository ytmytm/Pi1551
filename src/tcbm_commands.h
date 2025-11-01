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

#ifndef TCBM_COMMANDS_H
#define TCBM_COMMANDS_H

#include "commands_base.h"
#include "tcbm_bus.h"

// TCBM Bus Command Codes (from controller perspective, matching tcbm2sd.ino):
#define TCBM_CODE_COMMAND 0x81  // Controller sends command byte (0x20 LISTEN, 0x3F UNLISTEN, 0x40 TALK, 0x5F UNTALK)
#define TCBM_CODE_SECOND  0x82  // Controller sends secondary address (0x60 SECOND, 0xE0 CLOSE, 0xF0 OPEN)
#define TCBM_CODE_RECV    0x83  // Controller sends data byte (device receives)
#define TCBM_CODE_SEND    0x84  // Controller receives data byte (device sends)

class TCBM_Commands : public Commands_Base
{
public:
	TCBM_Commands();
	void Initialise();

	void Reset(void);
	void SimulateIECBegin(void);
	UpdateAction SimulateIECUpdate(void);

protected:
	bool WriteSerialPortByte(u8 data, bool eoi) override;
	// ReadSerialPortByte not used by TCBM (uses ReadTCBMDataByte instead), so inherits default stub
	void ReadBrowseMode() { TCBM_Bus::ReadBrowseMode(); }
	bool IsReset() { return TCBM_Bus::IsReset(); }

	// Override base class methods to handle TCBM command byte protocol
	// Note: Listen() and Talk() are not overridden - TCBM uses state machine instead
	void LoadFile() override;
	void SaveFile() override;
	void LoadDirectory() override;

	// TCBM-specific state machine (not using ATN sequences)
	enum TCBMState
	{
		TCBM_STATE_IDLE,      // Waiting for command byte
		TCBM_STATE_OPEN,      // Receiving filename/command on channel 15
		TCBM_STATE_LOAD,      // Sending data to computer
		TCBM_STATE_SAVE,      // Receiving data from computer
		TCBM_STATE_DIR        // Sending directory listing
	};

	// Helper functions to read TCBM command bytes
	u8 ReadTCBMCommandByte();      // Non-blocking, returns 0 if no command
	u8 ReadTCBMCommandByteBlock(); // Blocking until command byte received
	u8 ReadTCBMDataByte();         // Read data byte following command byte

	// TCBM-specific members
	TCBMState tcbmState;
	// Note: secondaryAddress (from base class) tracks the currently active channel
};
#endif

