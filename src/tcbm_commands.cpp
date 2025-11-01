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

#include "tcbm_commands.h"
#include "tcbm_bus.h"
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


TCBM_Commands::TCBM_Commands()
{
	deviceID = 8;
	Reset();
	starFileName = 0;
	displayingDevices = false;
	lowercaseBrowseModeFilenames = false;
	newDiskType = DiskImage::D64;
	tcbmState = TCBM_STATE_IDLE;
	secondaryAddress = 0;
}

void TCBM_Commands::Initialise()
{
	SetHeaderVersion();
}

void TCBM_Commands::Reset(void)
{
	Commands_Base::Reset(); // Calls base Reset which closes all channels
	tcbmState = TCBM_STATE_IDLE;
	secondaryAddress = 0;
	deviceRole = DEVICE_ROLE_PASSIVE;
}

// TCBM data byte transmission after TCBM_CODE_SEND command byte
// Matches tcbm_write_data() from tcbm2sd.ino
bool TCBM_Commands::WriteSerialPortByte(u8 data, bool eoi)
{
	u8 status = eoi ? 0x03 : 0x00; // TCBM_STATUS_EOI : TCBM_STATUS_OK
	
	// Wait for DAV=0 (controller ready to receive)
	TCBM_Bus::WaitWhileDAVAsserted();
	TCBM_Bus::ReadBrowseMode();
	
	// Put data on bus
	TCBM_Bus::SetData(data);
	TCBM_Bus::SetStatus(status);
	TCBM_Bus::AssertACK(); // ACK=1
	
	// Wait for DAV=1 (controller received data)
	TCBM_Bus::WaitWhileDAVReleased();
	TCBM_Bus::ReadBrowseMode();
	
	// Return to input mode
	TCBM_Bus::SetDataInput();
	TCBM_Bus::SetStatus(0);
	TCBM_Bus::ReleaseACK(); // ACK=0
	
	// Wait for DAV=0 (controller ready for next byte)
	TCBM_Bus::WaitWhileDAVAsserted();
	TCBM_Bus::ReadBrowseMode();
	
	// Final ACK handshake
	TCBM_Bus::AssertACK(); // ACK=1
	TCBM_Bus::WaitWhileDAVReleased();
	TCBM_Bus::ReadBrowseMode();

	return false;
}

// ReadSerialPortByte is not used by TCBM - we use ReadTCBMDataByte() instead
// The base class provides a default stub implementation that we inherit

void TCBM_Commands::SimulateIECBegin(void)
{
	SetHeaderVersion();
	Reset();
	TCBM_Bus::ReadBrowseMode();
    // Initialize bus like tcbm2sd: STATUS=OK, ACK=1 (ready)
    TCBM_Bus::SetStatus(0);
    TCBM_Bus::AssertACK();
}

// Read TCBM command byte (non-blocking)
// Returns 0 if no valid command byte available, otherwise returns command byte (0x81-0x84)
u8 TCBM_Commands::ReadTCBMCommandByte()
{
			TCBM_Bus::ReadBrowseMode();
	if (!TCBM_Bus::IsDAVAsserted()) return 0; // Controller not ready
	
	u8 data = TCBM_Bus::GetPI_Data();
	if (!(data & 0x80)) return 0; // Not a command byte
	
	if (data != TCBM_CODE_COMMAND && data != TCBM_CODE_SECOND && 
	    data != TCBM_CODE_RECV && data != TCBM_CODE_SEND) return 0; // Invalid command
	
	// Acknowledge by releasing ACK (ACK=0 means we acknowledge)
	TCBM_Bus::ReleaseACK();
	return data;
}

// Read TCBM command byte (with timeout, returns 0 if timeout)
u8 TCBM_Commands::ReadTCBMCommandByteBlock()
{
	u8 cmd = 0;
	timer.Start(10000); // 10ms timeout - should be enough for TCBM protocol
	
	while (cmd != TCBM_CODE_COMMAND && cmd != TCBM_CODE_SECOND && 
	       cmd != TCBM_CODE_RECV && cmd != TCBM_CODE_SEND)
	{
		if (timer.TimedOut())
		{
			return 0; // Timeout - return 0 to indicate no command available
		}
		
		while (!TCBM_Bus::IsDAVAsserted() && !timer.TimedOut())
		{
			TCBM_Bus::ReadBrowseMode();
			TCBM_Bus::WaitMicroSeconds(1);
			timer.count++;
		}
		
		if (timer.TimedOut())
		{
			return 0; // Timeout waiting for DAV
		}
		
		// Read command byte
		cmd = TCBM_Bus::GetPI_Data();
		
		if (!(cmd & 0x80)) 
		{
			cmd = 0;
			continue;
		}
	}
	
	TCBM_Bus::ReleaseACK(); // Acknowledge
	return cmd;
}

// Read data byte following a command byte
// Assumes ACK was already released when command byte was acknowledged
u8 TCBM_Commands::ReadTCBMDataByte()
{
	// Wait for DAV to be released (controller puts data on bus)
	TCBM_Bus::WaitWhileDAVAsserted();
	TCBM_Bus::ReadBrowseMode();
	
	u8 data = TCBM_Bus::GetPI_Data();
	
	// Set status OK, then assert ACK (matches tcbm_read_data sequence)
	TCBM_Bus::SetStatus(0);
	TCBM_Bus::AssertACK();
	
	// Wait for DAV to be asserted again (controller sees our ACK)
	TCBM_Bus::WaitWhileDAVReleased();
	// Status remains OK (no need to reset, already OK)
	
	return data;
}

TCBM_Commands::UpdateAction TCBM_Commands::SimulateIECUpdate(void)
{
	if (TCBM_Bus::IsReset())
	{
		// If the computer is resetting then just wait until it has come out of reset.
		do
		{
			//DEBUG_LOG("Reset during SimulateIECUpdate\r\n");
			TCBM_Bus::ReadBrowseMode();
			TCBM_Bus::WaitMicroSeconds(100);
		}
		while (TCBM_Bus::IsReset());
		TCBM_Bus::WaitMicroSeconds(20);
		return RESET;
	}

	updateAction = NONE;
	if (selectedImageName[0] != 0) updateAction = IMAGE_SELECTED;

	switch (tcbmState)
	{
		case TCBM_STATE_IDLE:
		{
			// Wait for command byte (0x81 = COMMAND)
			u8 cmd = ReadTCBMCommandByte();
			if (cmd == 0) break; // No command available
			
			if (cmd != TCBM_CODE_COMMAND) break; // Not a command byte
			
			// Read the command (LISTEN=0x20, UNLISTEN=0x3F, TALK=0x40, UNTALK=0x5F)
			u8 cmdByte = ReadTCBMDataByte();
			
			if (cmdByte == 0x20 + deviceID) // LISTEN to our device
			{
				deviceRole = DEVICE_ROLE_LISTEN;
				
				// Read secondary address command (should be 0x82 = SECOND)
				cmd = ReadTCBMCommandByteBlock();
				if (cmd != TCBM_CODE_SECOND)
				{
					tcbmState = TCBM_STATE_IDLE;
					break;
				}
				
				u8 secondary = ReadTCBMDataByte();
				secondaryAddress = secondary & 0x0F;
				
				switch (secondary & 0xF0)
				{
					case 0xF0: // OPEN
						// For channel 0, prepare to receive filename
						if (secondaryAddress == 0)
						{
							Channel& channel0 = channels[0];
							channel0.Close();
							channel0.cursor = 0;
						}
						tcbmState = TCBM_STATE_OPEN;
					break;
					case 0xE0: // CLOSE
						CloseFile(secondaryAddress);
						if (secondaryAddress == 15)
						{
							ProcessCommand();
						}
						tcbmState = TCBM_STATE_IDLE;
					break;
					case 0x60: // SECOND
						if (secondaryAddress == 15)
						{
							tcbmState = TCBM_STATE_OPEN; // Keep receiving command
						}
						else if (secondaryAddress == 1)
						{
							// For SAVE: filename was received on channel 0, copy it to channel 1
							Channel& channel0 = channels[0];
							Channel& channel1 = channels[1];
							memcpy(channel1.command, channel0.command, sizeof(channel0.command));
							OpenFile(); // Now OpenFile() will use channel1.command with the filename
							tcbmState = TCBM_STATE_SAVE;
						}
						else if (secondaryAddress == 0)
						{
							// For LOAD: filename was received on channel 0, already in channel0.command
							OpenFile();
							tcbmState = TCBM_STATE_LOAD;
						}
						else
						{
							// Other channels - copy from channel 0 if available
							Channel& channel0 = channels[0];
							Channel& channelX = channels[secondaryAddress];
							memcpy(channelX.command, channel0.command, sizeof(channel0.command));
							OpenFile();
							tcbmState = TCBM_STATE_SAVE;
						}
					break;
					default:
						tcbmState = TCBM_STATE_IDLE;
					break;
				}
			}
			else if (cmdByte == 0x3F) // UNLISTEN
			{
				if (deviceRole == DEVICE_ROLE_LISTEN)
				{
					deviceRole = DEVICE_ROLE_PASSIVE;
					tcbmState = TCBM_STATE_IDLE;
				}
			}
			else if (cmdByte == 0x40 + deviceID) // TALK to our device
			{
				deviceRole = DEVICE_ROLE_TALK;
				
				// Read secondary address command (should be 0x82 = SECOND)
				cmd = ReadTCBMCommandByteBlock();
				if (cmd != TCBM_CODE_SECOND)
				{
					tcbmState = TCBM_STATE_IDLE;
					break;
				}
				
				u8 secondary = ReadTCBMDataByte();
				secondaryAddress = secondary & 0x0F;
				
				switch (secondary & 0xF0)
				{
					case 0x70: // SECOND (fastload on channel 0) - fall through
					case 0x60: // SECOND
						if (secondaryAddress == 15)
						{
							// Status request (equivalent to STATE_STAT in tcbm2sd)
							Channel& channelCommand = channels[15];
							if (channelCommand.buffer[0] == '$')
							{
								tcbmState = TCBM_STATE_DIR;
		}
		else
		{
								SendError();
								tcbmState = TCBM_STATE_IDLE;
							}
						}
						else if (secondaryAddress == 0)
						{
							// LOAD - check if filename was received (like file_opened check in tcbm2sd)
							Channel& channel = channels[0];
							if (channel.command[0] != 0) // Filename was received
							{
								if (!channel.open)
								{
									// Open file using filename from channel0.command
									OpenFile();
								}
								
								if (channel.open)
								{
									// Check if directory listing was requested
									if (channel.command[0] == '$' || channel.buffer[0] == '$')
									{
										tcbmState = TCBM_STATE_DIR;
					}
					else
					{
										tcbmState = TCBM_STATE_LOAD;
					}
		}
		else
		{
									// File not found - will be handled in LoadFile()
									tcbmState = TCBM_STATE_LOAD;
		}
	}
	else
	{
								// No filename received yet
								tcbmState = TCBM_STATE_IDLE;
							}
		}
		else
		{
							tcbmState = TCBM_STATE_IDLE;
						}
				break;
					default:
						tcbmState = TCBM_STATE_IDLE;
				break;
			}
		}
			else if (cmdByte == 0x5F) // UNTALK
			{
				if (deviceRole == DEVICE_ROLE_TALK)
				{
					deviceRole = DEVICE_ROLE_PASSIVE;
					tcbmState = TCBM_STATE_IDLE;
				}
			}
		}
		break;
		
		case TCBM_STATE_OPEN:
		{
			// Receiving filename/command on channel 15
			u8 cmd = ReadTCBMCommandByte();
			if (cmd == 0) break; // No command yet
			
			if (cmd == TCBM_CODE_RECV)
			{
				u8 byte = ReadTCBMDataByte();
				Channel& channel = channels[secondaryAddress];
				if (!channel.WriteFull())
				{
					channel.buffer[channel.cursor++] = byte;
				}
			}
			else if (cmd == TCBM_CODE_COMMAND)
			{
				u8 cmdByte = ReadTCBMDataByte();
				if (cmdByte == 0x3F) // UNLISTEN
				{
					// Command complete
					Channel& channel = channels[secondaryAddress];
					if (channel.cursor > 1)
					{
						if (channel.buffer[channel.cursor - 1] == 0x0d) channel.cursor -= 1;
						else if (channel.cursor > 2 && channel.buffer[channel.cursor - 2] == 0x0d) channel.cursor -= 2;
					}
					if (!channel.WriteFull())
						channel.buffer[channel.cursor++] = 0;
					
					// Copy buffer to command for OpenFile() to use (like IEC does)
					memcpy(channel.command, channel.buffer, channel.cursor);
					
					if (secondaryAddress == 15)
					{
						receivedCommand = true;
					}
					else if (secondaryAddress == 0)
					{
						// File opened on channel 0 - filename is ready
						// This will be used when SECOND is received on channel 1
					}
					tcbmState = TCBM_STATE_IDLE;
				}
			}
		}
		break;

		case TCBM_STATE_SAVE:
		{
			// Receiving data to save - use TCBM-aware SaveFile
			SaveFile(); // This handles TCBM_CODE_RECV protocol
			}
		break;

		case TCBM_STATE_LOAD:
		{
			// Wait for TCBM_CODE_SEND, then send one byte
			LoadFile(); // TCBM-aware version handles command bytes
			}
		break;
		
		case TCBM_STATE_DIR:
		{
			// Wait for TCBM_CODE_SEND, then send one byte of directory
			LoadDirectory(); // TCBM-aware version handles command bytes
		}
					break;
				}
	
	return updateAction;
}

// TCBM-aware LoadFile: waits for TCBM_CODE_SEND before sending each byte
void TCBM_Commands::LoadFile()
{
	Channel& channel = channels[secondaryAddress];
	
	// Check if we need to open file first
	if (!channel.open)
	{
		OpenFile();
		if (!channel.open)
		{
			// File not found - send error and wait for UNTALK
			u8 cmd = ReadTCBMCommandByteBlock();
			if (cmd == TCBM_CODE_SEND)
			{
				WriteSerialPortByte(13, false); // Send CR
			}
			tcbmState = TCBM_STATE_IDLE;
			return;
		}
	}
	
	// Wait for TCBM_CODE_SEND command byte (blocking)
	u8 cmd = ReadTCBMCommandByte();
	if (cmd == 0) return; // Not ready yet
	
	if (cmd == TCBM_CODE_SEND)
	{
		// Read one byte from file
		u8 byte;
		u32 bytesRead;
		if (f_read(&channel.file, &byte, 1, &bytesRead) == FR_OK && bytesRead > 0)
		{
			bool eoi = (f_tell(&channel.file) >= channel.fileSize);
			WriteSerialPortByte(byte, eoi);
			if (eoi)
			{
				// File complete - wait for UNTALK
				tcbmState = TCBM_STATE_IDLE;
			}
		}
		else
		{
			// EOF reached
			WriteSerialPortByte(0, true); // Send with EOI
			tcbmState = TCBM_STATE_IDLE;
		}
	}
	else if (cmd == TCBM_CODE_COMMAND)
	{
		u8 cmdByte = ReadTCBMDataByte();
		if (cmdByte == 0x5F) // UNTALK
		{
			tcbmState = TCBM_STATE_IDLE;
		}
	}
}

// TCBM-aware SaveFile: waits for TCBM_CODE_RECV before reading each byte
void TCBM_Commands::SaveFile()
{
	Channel& channel = channels[secondaryAddress];

	if (!channel.open || !channel.writing)
	{
		// File should already be opened when SECOND was received on channel 1
		// If it failed to open, we should still try to handle it gracefully
		tcbmState = TCBM_STATE_IDLE;
					return;
	}
	
	// Wait for TCBM_CODE_RECV command byte (non-blocking)
	u8 cmd = ReadTCBMCommandByte();
	if (cmd == 0) return; // Not ready yet
	
	if (cmd == TCBM_CODE_RECV)
	{
		u8 byte = ReadTCBMDataByte();
			channel.buffer[channel.cursor++] = byte;
		
			if (channel.WriteFull())
			{
			u32 bytesWritten;
				if (f_write(&channel.file, channel.buffer, sizeof(channel.buffer), &bytesWritten) != FR_OK)
				{
				// Write error
				tcbmState = TCBM_STATE_IDLE;
				return;
				}
				channel.cursor = 0;
			}
		}
	else if (cmd == TCBM_CODE_COMMAND)
	{
		u8 cmdByte = ReadTCBMDataByte();
		if (cmdByte == 0x3F) // UNLISTEN
		{
			// Flush remaining buffer
			if (channel.cursor > 0)
			{
				u32 bytesWritten;
				f_write(&channel.file, channel.buffer, channel.cursor, &bytesWritten);
			}
			channel.Close();
			tcbmState = TCBM_STATE_IDLE;
		}
	}
}

// TCBM-aware LoadDirectory: waits for TCBM_CODE_SEND before sending each byte
void TCBM_Commands::LoadDirectory()
{
	Channel& channel = channels[secondaryAddress];
	
	// Initialize directory if not already started (for channel 0)
	if (secondaryAddress == 0 && channel.cursor == 0)
	{
		// Build directory into channel buffer
		Commands_Base::LoadDirectory();
		channel.bytesSent = 0;
	}
	
	// Wait for TCBM_CODE_SEND command byte (non-blocking)
	u8 cmd = ReadTCBMCommandByte();
	if (cmd == 0) return; // Not ready yet
	
	if (cmd == TCBM_CODE_SEND)
	{
		if (channel.bytesSent < channel.cursor)
		{
			bool eoi = (channel.bytesSent == channel.cursor - 1);
			WriteSerialPortByte(channel.buffer[channel.bytesSent++], eoi);
			
			if (eoi)
			{
				// Directory complete - wait for UNTALK
				tcbmState = TCBM_STATE_IDLE;
		}
	}
	else
	{
			// Directory complete
			tcbmState = TCBM_STATE_IDLE;
		}
	}
	else if (cmd == TCBM_CODE_COMMAND)
	{
		u8 cmdByte = ReadTCBMDataByte();
		if (cmdByte == 0x5F) // UNTALK
		{
			tcbmState = TCBM_STATE_IDLE;
		}
	}
}

// All other method implementations (Extended, ProcessCommand, FindFirst, SendBuffer,
// SendError, GetFilenameCharacter, AddDirectoryEntry, OpenFile, CloseFile,
// CreateNewDisk, WriteNewDiskInRAM, Enter, CD, MKDir, RMDir, FolderCommand, Copy,
// ChangeDevice, Memory, New, Rename, Scratch, User) are now in Commands_Base and inherited.
