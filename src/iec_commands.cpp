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

#include "iec_commands.h"
#include "iec_bus.h"
#include "ff.h"
#include "DiskImage.h"
#include "Petscii.h"
#include "FileBrowser.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <algorithm>

extern void Reboot_Pi(); // Used by User() method

#define WaitWhile(checkStatus) \
	do\
	{\
		IEC_Bus::ReadBrowseMode();\
		if (CheckATN()) return true;\
	} while (checkStatus)

IEC_Commands::IEC_Commands() : Commands_Base()
{
	usingVIC20 = false;
	autoBootFB128 = false;
	C128BootSectorName = 0;
}

void IEC_Commands::Reset(void)
{
	Commands_Base::Reset();
	usingVIC20 = false;
}

bool IEC_Commands::CheckATN(void)
{
	bool atnAsserted = IEC_Bus::IsAtnAsserted();
	if (atnSequence == ATN_SEQUENCE_RECEIVE_COMMAND_CODE)
	{
		// TO CHECK is this case needed? Just let it complete
		if (!atnAsserted) atnSequence = ATN_SEQUENCE_HANDLE_COMMAND_CODE;
		return !atnAsserted;
	}
	else
	{
		if (atnAsserted) atnSequence = ATN_SEQUENCE_ATN;
		return atnAsserted;
	}
}

// Paraphrasing Jim Butterfield;- https://www.atarimagazines.com/compute/issue38/073_1_HOW_THE_VIC_64_SERIAL_BUS_WORKS.php
// The talker is asserting the Clock line.
// The listener is asserting the Data line.
// There could be more than one listener, in which case all of the listeners are asserting the Data line.
// When the talker is ready it releases the Clock line.
// The listener must detect this and respond, but it doesn't have to do so immediately.
// When the listener is ready to listen, it releases the Data line.
// The Data line will go "unasserted" only when all listeners have released it - in other words, when all listeners are ready to accept data.

// What happens next is variable. Either the talker will assert the Clock line again in less than 200 microseconds - usually within 60 microseconds - or 
// it will do nothing. The listener should be watching, and if 200 microseconds pass without the Clock line going to true, it has a special task to perform : note EOI (End Or Identify).
// If the Ready for Data signal isn't acknowledged by the talker within 200 microseconds, the listener knows that the talker is trying to signal EOI ie, "this character will be the last one."

// So if the listener sees the 200 microsecond time - out, it must signal "OK, I noticed the EOI" back to the talker, I does this by asserting Data line for at least 60 microseconds, and then releasing it.
// The talker will then revert to transmitting the character in the usual way; within 60 microseconds it will assert Clock line, and transmission will continue.
// At this point, the Clock line is asserted whether or not we have gone through the EOI sequence; we're back to a common transmission sequence. 

// The talker has eight bits to send. They will go out without handshake; in other words, the listener had better be there to catch them, since the talker won't wait to hear from the listener.
// At this point, the talker controls both lines, Clock and Data. At the beginning of the sequence, it is asserting the Clock, while the Data line is released.
// The Data line will change soon, since we'll send the data over it.

// For each bit, we set the Data line true or false according to whether the bit is one or zero.
// As soon as that's set, the Clock line is released, signalling "data ready."
// The talker will typically have a bit in place and be signalling ready in 70 microseconds or less. Once the talker has signalled "data ready, "it will hold the two lines steady for at least 20 microseconds timing needs to be increased to 
// 60 microseconds if the Commodore 64 is listening, since the 64's video chip may interrupt the processor for 42 microseconds at a time, and without the extra wait the 64 might completely miss a bit.
// The listener plays a passive role here; it sends nothing, and just watches. 
// As soon as it sees the Clock line false, it grabs the bit from the Data line and puts it away.
// It then waits for the clock line to go true, in order to prepare for the next bit.
// When the talker figures the data has been held for a sufficient length of time, it pulls the Clock line true and releases the Data line to false.
// Then it starts to prepare the next bit.

// After the eighth bit has been sent, it's the listener's turn to acknowledge. At this moment, the Clock line is asserted and the Data line is released.
// The listener must acknowledge receiving the byte OK by asserting the Data line. The talker is now watching the Data line.
// If the listener doesn't pull the Data line true within one millisecond it will know that something's wrong and may alarm appropriately. 

// We're finished, and back where we started. The talker is holding the Clock line true, and the listener is holding the Data line true. We're ready for step 1; we may send another character - unless EOI has happened.
// If EOI was sent or received in this last transmission, both talker and listener "let go." 
// After a suitable pause, the Clock and Data lines are released to false and transmission stops. 

bool IEC_Commands::WriteSerialPortByte(u8 data, bool eoi)
{
	IEC_Bus::WaitMicroSeconds(50); //sidplay64-sd2iec needs this?

	// When the talker is ready it releases the Clock line.
	IEC_Bus::ReleaseClock();

	// Wait for all listeners to be ready. They singal this by releasing the Data line.
	WaitWhile(IEC_Bus::IsDataAsserted());

	if (eoi) // End Or Identify
	{
		WaitWhile(IEC_Bus::IsDataReleased());
		WaitWhile(IEC_Bus::IsDataAsserted());
	}

	IEC_Bus::AssertClock();
	IEC_Bus::WaitMicroSeconds(40);
	WaitWhile(IEC_Bus::IsDataAsserted());
	IEC_Bus::WaitMicroSeconds(21);

	// At this point, the talker controls both lines, Clock and Data. At the beginning of the sequence, it is asserting the Clock, while the Data line is released.
	for (u8 i = 0; i < 8; ++i)
	{
		IEC_Bus::WaitMicroSeconds(45);
		if (data & 1 << i) IEC_Bus::ReleaseData();
		else IEC_Bus::AssertData();
		IEC_Bus::WaitMicroSeconds(22);
		IEC_Bus::ReleaseClock();
		if (usingVIC20) IEC_Bus::WaitMicroSeconds(34);
		else IEC_Bus::WaitMicroSeconds(75);
		IEC_Bus::AssertClock();
		IEC_Bus::WaitMicroSeconds(22);
		IEC_Bus::ReleaseData();
		IEC_Bus::WaitMicroSeconds(14);
	}

	// After the eighth bit has been sent, it's the listener's turn to acknowledge. At this moment, the Clock line is asserted and the Data line is released.
	WaitWhile(IEC_Bus::IsDataReleased());
	return false;
}

bool IEC_Commands::ReadSerialPortByte(u8& byte, bool eoi)
{
	byte = 0;

	// When the talker is ready it releases the Clock line.
	WaitWhile(IEC_Bus::IsClockAsserted());

	// We release data first
	IEC_Bus::ReleaseData();
	WaitWhile(IEC_Bus::IsDataAsserted());

	timer.Start(200);
	do
	{
		IEC_Bus::ReadBrowseMode();
		if (CheckATN()) return true;
		IEC_Bus::WaitMicroSeconds(1);
		timer.count++;
	}
	while (IEC_Bus::IsClockReleased() && !timer.TimedOut());

	if (timer.TimedOut())
	{
		IEC_Bus::AssertData();
		IEC_Bus::WaitMicroSeconds(73);
		IEC_Bus::ReleaseData();
		WaitWhile(IEC_Bus::IsClockReleased());
		receivedEOI = true;
	}

	for (u8 i = 0; i < 8; ++i)
	{
		WaitWhile(IEC_Bus::IsClockAsserted());
		byte = (byte >> 1) | (!!IEC_Bus::IsDataReleased() << 7);
		WaitWhile(IEC_Bus::IsClockReleased());
	}

	IEC_Bus::AssertData();
	return false;
}

void IEC_Commands::SimulateIECBegin(void)
{
	Reset();
	ReadBrowseMode();
}

// Paraphrasing Jim Butterfield
// The computer is the only device that will assert ATN.
// When it does so, all other devices drop what they are doing and become listeners.
// Bytes sent by the computer during an ATN period are commands "Talk," "Listen," "Untalk," and "Unlisten" telling a specific device that it will become (or cease to be) a talker or listener.
// The commands go to all devices, and all devices acknowledge them, but only the ones with the suitable device numbers will switch into talk and listen mode.
// These commands are sometimes followed by a secondary address, and after ATN is released, perhaps by a file name.

// ATN SEQUENCES
// When ATN is asserted, everybody stops what they are doing.
// The computer will quickly assert the Clock line (it's going to send soon).
// At the same time, the processor releases the Data line to false, but all other devices are getting ready to listen and will each assert the Data line.
// They had better do this within one millisecond, since the processor is watching and may sound an alarm("device not present") if it doesn't see this take place.
// Under normal circumstances, transmission now takes place.
// The computer is sending commands rather than data, but the characters are exchanged with exactly the same timing and handshakes.
// All devices receive the commands, but only the specified device acts upon it.

// TURNAROUND
// An unusual sequence takes place following ATN if the computer wishes the remote device to become a talker.
// This will usually take place only after a Talk command has been sent.
// Immediately after ATN is released, the selected device will be behaving like a listener.
// After all, it's been listening during the ATN cycle, and the computer has been a talker.
// At this instant, we have "wrong way" logic; the device is asserting the Data line, and the computer is asserting the Clock line.
// We must turn this around. The computer quickly realizes what's going on, and asserts the Data line, as well as releasing the Clock line.
// The device waits for this: when it sees the Clock line released, it releases the Data line (which stays asserted anyway since the computer is asserting it)
// and then asserts the Clock line. We're now in our starting position, with the talker (that's the device) asserting the Clock, and the listener (the computer) asserting the Data line true.
IEC_Commands::UpdateAction IEC_Commands::SimulateIECUpdate(void)
{
	if (IEC_Bus::IsReset())
	{
		// If the computer is resetting then just wait until it has come out of reset.
		do
		{
			//DEBUG_LOG("Reset during SimulateIECUpdate\r\n");
			IEC_Bus::ReadBrowseMode();
			IEC_Bus::WaitMicroSeconds(100);
		}
		while (IEC_Bus::IsReset());
		IEC_Bus::WaitMicroSeconds(20);
		return RESET;
	}

	updateAction = NONE;

	if (selectedImageName[0] != 0) updateAction = IMAGE_SELECTED;

	switch (atnSequence)
	{
		case ATN_SEQUENCE_IDLE:
			IEC_Bus::ReadBrowseMode();
			if (IEC_Bus::IsAtnAsserted()) atnSequence = ATN_SEQUENCE_ATN;
			else if (selectedImageName[0] != 0) updateAction = IMAGE_SELECTED;
		break;
		case ATN_SEQUENCE_ATN:
			// All devices must release the Clock line as the computer will be the one assering it.
			IEC_Bus::ReleaseClock();
			// Tell computer we are ready to listen by asserting the Data line.
			IEC_Bus::AssertData();

			deviceRole = DEVICE_ROLE_PASSIVE;
			atnSequence = ATN_SEQUENCE_RECEIVE_COMMAND_CODE;
			receivedEOI = false;

			// Wait until the computer is ready to talk
			// TODO: should set a timer here and if it times out (before the clock is released) go back to IDLE?
			while (IEC_Bus::IsClockReleased())
			{
				IEC_Bus::ReadBrowseMode();
			}
		break;
		case ATN_SEQUENCE_RECEIVE_COMMAND_CODE:
			ReadIECSerialPort(commandCode);
			// Command Code
			// 20 Listen + device address (0-1e)
			// 3f Unlisten
			// 40 Talk + device address (0-1e)
			// 5f Untalk
			// 60 Open Channel or Data + secondary address (0-f)
			// 70 Undefined
			// 80 Undefined
			// 90 Undefined
			// a0 Undefined
			// b0 Undefined
			// c0 Undefined
			// d0 Undefined
			// e0 Close + secondary address or channel (0-f)
			// f0 Open + secondary address or channel (0-f)

			// Notes: from various CBM-DOS books highlighting various DOS requirements.

			// Secondary addresses 0 and 1 are reserved by the DOS for saving and loading programs.

			// Secondary address 15 is designated as the command and error channel.
			// The command/error channel 15 may be opened while a file is open, but when channel 15 is closed, all other channels are closed as well.

			// OPEN lfn, 8, sa, "filename,filetype,mode"
			// lfn - logical file number
			//		 When the logical file number is between 1 and 127, a PRINT# statement sends a RETURN character to the file after each variable.
			//		 If the logical file number is greater than 127 (128 - 255), the PRINT# statement sends an additional linefeed after each RETURN.
			//       The lfn simply a way of assiging a deviceID and a secondary address/channel to a single ID the computer can reference. 

			// Should several files be open at once, they must all use different secondary addresses/channels, as only one file can use a channel.

			// If a file is opened with the secondary address of a previously opened file, the previous file is closed.

			// A maximum of 3 channels can be opened with the 1541 at a time.

			// When specifying the filename to be written to (in the OPEN command), you must be sure that the file name does not already exist.
			// If a file that already exists is to be to opened for writing, the file must first be deleted.

			//DEBUG_LOG("%0x\r\n", commandCode);

			if (commandCode == 0x20 + deviceID)	// Listen
			{
				secondaryAddress = commandCode & 0x0f;
				deviceRole = DEVICE_ROLE_LISTEN;
				if (IEC_Bus::IsAtnAsserted()) atnSequence = ATN_SEQUENCE_RECEIVE_COMMAND_CODE;
				else atnSequence = ATN_SEQUENCE_HANDLE_COMMAND_CODE;
			}
			else if (commandCode == 0x3f)	// Unlisten
			{
				if (deviceRole == DEVICE_ROLE_LISTEN) deviceRole = DEVICE_ROLE_PASSIVE;
				atnSequence = ATN_SEQUENCE_HANDLE_COMMAND_CODE;
			}
			else if (commandCode == 0x40 + deviceID) // Talk
			{
				secondaryAddress = commandCode & 0x0f;
				deviceRole = DEVICE_ROLE_TALK;
				if (IEC_Bus::IsAtnAsserted()) atnSequence = ATN_SEQUENCE_RECEIVE_COMMAND_CODE;
				else atnSequence = ATN_SEQUENCE_HANDLE_COMMAND_CODE;
			}
			else if (commandCode == 0x5f)	// Untalk
			{
				if (deviceRole == DEVICE_ROLE_TALK) deviceRole = DEVICE_ROLE_PASSIVE;
				atnSequence = ATN_SEQUENCE_HANDLE_COMMAND_CODE;
			}
			else if ((commandCode & 0x60) == 0x60)	// Set secondary addresses for 6*, e* and f* commands
			{
				secondaryAddress = commandCode & 0x0f;
				if ((commandCode & 0xf0) == 0xe0)	// Close
				{
					CloseFile(secondaryAddress);

					if (IEC_Bus::IsAtnAsserted()) atnSequence = ATN_SEQUENCE_RECEIVE_COMMAND_CODE;
					else atnSequence = ATN_SEQUENCE_HANDLE_COMMAND_CODE;
				}
				else	// Open
				{
					atnSequence = ATN_SEQUENCE_HANDLE_COMMAND_CODE;
				}
			}
			else
			{
				IEC_Bus::ReleaseClock();
				IEC_Bus::ReleaseData();
				IEC_Bus::WaitWhileAtnAsserted();
				atnSequence = ATN_SEQUENCE_COMPLETE;
			}
		break;
		case ATN_SEQUENCE_HANDLE_COMMAND_CODE:
			IEC_Bus::WaitWhileAtnAsserted();
			if (deviceRole == DEVICE_ROLE_LISTEN)
			{
				Listen();
			}
			else if (deviceRole == DEVICE_ROLE_TALK)
			{
				// Do the turn around and become the talker
				IEC_Bus::ReleaseData();
				IEC_Bus::AssertClock();
				Talk();
			}
			atnSequence = ATN_SEQUENCE_COMPLETE;
		break;
		case ATN_SEQUENCE_COMPLETE:
			IEC_Bus::ReleaseClock();
			IEC_Bus::ReleaseData();

			if (receivedCommand)
			{
				Channel& channelCommand = channels[15];

				//DEBUG_LOG("%s sa = %d\r\n", channelCommand.buffer, secondaryAddress);

				if (secondaryAddress == 0xf) //channel 0xf (15) is reserved as the command channel.
				{
					ProcessCommand();
				}
				else
				{
					// We have no time to do anything now. I don't know why!
					// The ATN sequence is complete and we go back to idle. Maybe anther ATN sequence starts immediately?
					// We should be able to open files now but doing so takes time and breaks communication.
					// So instead, we just cache what we need to know and open the file later (at the start of talking or listening).
					Channel& channel = channels[secondaryAddress];
					memcpy(channel.command, channelCommand.buffer, channelCommand.cursor);
				}

				// Command has been processed so reset it now.
				receivedCommand = false;
			}
			atnSequence = ATN_SEQUENCE_IDLE;
		break;
	}
	return updateAction;
}

// All static helper functions and duplicate method implementations moved to commands_base.cpp

// User() override for IEC-specific VIC20 timing support
void IEC_Commands::User(void)
{
	Channel& channel = channels[15];

	switch (toupper(channel.buffer[1]))
	{
		case 'A':
		case 'B':
		case '1':
		case '2':
			Error(ERROR_31_SYNTAX_ERROR);
		break;

		case 'I':
		case '9':
			if (channel.cursor == 2)
			{
				Error(ERROR_73_DOSVERSION);
				return;
			}
			// IEC-specific: VIC20 timing handling
			switch (channel.buffer[2])
			{
				case '+':
					usingVIC20 = true;
				break;
				case '-':
					usingVIC20 = false;
				break;
				default:
					Error(ERROR_73_DOSVERSION);
				break;
			}
		break;

		case 'J':
		case ':':
			Error(ERROR_73_DOSVERSION);
		break;
		case 202:
			Reboot_Pi();
		break;
		case '0':
			if ((channel.buffer[2] & 0x1f) == 0x1e && channel.buffer[3] >= 4 && channel.buffer[3] <= 30)
			{
				SetDeviceId(channel.buffer[3]);
				updateAction = DEVICEID_CHANGED;
				DEBUG_LOG("Changed deviceID to %d\r\n", channel.buffer[3]);
			}
			else
			{
				Error(ERROR_31_SYNTAX_ERROR);
			}
		break;
		default:
			Commands_Base::User(); // Call base implementation for other cases
		break;
	}
}

// Handle C128 boot sector special case
bool IEC_Commands::HandleSpecialOpenFile(Channel& channel)
{
	Channel& channelCommand = channels[15];

	if (strcmp((char*)channelCommand.buffer, "U1:13 0 01 00") == 0)
	{
		// This is a 128 trying to auto boot
		memset(channel.buffer, 0, 256);
		channel.cursor = 256;

		if (autoBootFB128)
		{
			int index = 0;
			channel.buffer[0] = 'C';
			channel.buffer[1] = 'B';
			channel.buffer[2] = 'M';
			index += 3;
			index += 4;
			channel.buffer[index++] = 'P';
			channel.buffer[index++] = 'I';
			channel.buffer[index++] = '1';
			channel.buffer[index++] = '5';
			channel.buffer[index++] = '4';
			channel.buffer[index++] = '1';
			channel.buffer[index++] = ' ';
			channel.buffer[index++] = 'F';
			channel.buffer[index++] = 'B';
			channel.buffer[index++] = '1';
			channel.buffer[index++] = '2';
			channel.buffer[index++] = '8';
			index++;
			channel.buffer[index++] = 'F';
			channel.buffer[index++] = 'B';
			channel.buffer[index++] = '1';
			channel.buffer[index++] = '2';
			channel.buffer[index++] = '8';
			index++;
			channel.buffer[index++] = 0xa2;
			channel.buffer[index] = (index + 5);
			index++;
			channel.buffer[index++] = 0xa0;
			channel.buffer[index++] = 0xb;
			channel.buffer[index++] = 0x4c;
			channel.buffer[index++] = 0xa5;
			channel.buffer[index++] = 0xaf;
			channel.buffer[index++] = 'R';
			channel.buffer[index++] = 'U';
			channel.buffer[index++] = 'N';
			channel.buffer[index++] = '\"';
			channel.buffer[index++] = 'F';
			channel.buffer[index++] = 'B';
			channel.buffer[index++] = '1';
			channel.buffer[index++] = '2';
			channel.buffer[index++] = '8';
			channel.buffer[index++] = '\"';
			channel.fileSize = 256;
		}
		if (C128BootSectorName)
		{
			FIL fpBS;
			u32 bytes;
			if (FR_OK == f_open(&fpBS, C128BootSectorName, FA_READ))
				f_read(&fpBS, channel.buffer, 256, &bytes);
			else
				memset(channel.buffer, 0, 256);
			channel.fileSize = 256;
		}

		if (SendBuffer(channel, true))
			return true; // Handled, indicate ATN
	}
	return false; // Not handled
}

// All other method implementations (Extended, ProcessCommand, Listen, Talk, LoadFile, SaveFile,
// LoadDirectory, OpenFile, CloseFile, CreateNewDisk, WriteNewDiskInRAM, FindFirst, SendBuffer,
// SendError, GetFilenameCharacter, AddDirectoryEntry) are now in Commands_Base and inherited.

