// Pi1551 - Shared SD browser command handling
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

#ifndef COMMANDS_BASE_H
#define COMMANDS_BASE_H

#include "ff.h"
#include "debug.h"
#include "DiskImage.h"
#include "Petscii.h"
#include "FileBrowser.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <algorithm>

#define CBM_NAME_LENGTH 16
#define CBM_NAME_LENGTH_MINUS_D64 CBM_NAME_LENGTH-4

#define DIRECTORY_ENTRY_SIZE 32

#define ERROR_00_OK 0
#define ERROR_25_WRITE_ERROR 25
#define ERROR_30_SYNTAX_ERROR 30
#define ERROR_31_SYNTAX_ERROR 31
#define ERROR_32_SYNTAX_ERROR 32
#define ERROR_33_SYNTAX_ERROR 33
#define ERROR_34_SYNTAX_ERROR 34
#define ERROR_39_FILE_NOT_FOUND 39
#define ERROR_62_FILE_NOT_FOUND 62
#define ERROR_63_FILE_EXISTS 63
#define ERROR_73_DOSVERSION 73
#define ERROR_74_DRlVE_NOT_READY 74

class Commands_Base
{
public:
	enum UpdateAction
	{
		NONE,
		IMAGE_SELECTED,
		DIR_PUSHED,
		POP_DIR,
		POP_TO_ROOT,
		REFRESH,
		DEVICEID_CHANGED,
		DEVICE_SWITCHED,
		RESET
	};

	enum DeviceRole
	{
		DEVICE_ROLE_PASSIVE,
		DEVICE_ROLE_LISTEN,
		DEVICE_ROLE_TALK
	};

	Commands_Base();
	virtual ~Commands_Base() {}

	virtual void Initialise() {}

	void SetDeviceId(u8 id) { deviceID = id; }
	u8 GetDeviceId() { return deviceID; }
	u8 GetSecondaryAddress() { return secondaryAddress; }
	DeviceRole GetDeviceRole() { return deviceRole; }

	void SetLowercaseBrowseModeFilenames(bool value) { lowercaseBrowseModeFilenames = value; }
	void SetNewDiskType(DiskImage::DiskType type) { newDiskType = type; }
	void SetStarFileName(const char* fileName) { starFileName = fileName; }

	virtual void Reset(void);
	virtual void SimulateIECBegin(void) = 0;
	virtual UpdateAction SimulateIECUpdate(void) = 0;

	const char* GetNameOfImageSelected() const { return selectedImageName; }
	const FILINFO* GetImageSelected() const { return &filInfoSelectedImage; }

	void SetHeaderVersion();
	int CreateNewDisk(char* filenameNew, char* ID, bool automount);

	void SetDisplayingDevices(bool displayingDevices) { this->displayingDevices = displayingDevices; }

protected:
	enum ATNSequence 
	{
		ATN_SEQUENCE_IDLE,
		ATN_SEQUENCE_ATN,
		ATN_SEQUENCE_RECEIVE_COMMAND_CODE,
		ATN_SEQUENCE_HANDLE_COMMAND_CODE,
		ATN_SEQUENCE_COMPLETE
	};

	struct Channel
	{
		u8 buffer[0x1000];
		u8 command[0x100];

		FILINFO filInfo;
		FIL file;
		u32 cursor;
		u32 bytesSent;
		u32 open : 1;
		u32 writing : 1;
		u32 fileSize;

		void Close();
		bool WriteFull() const { return cursor >= sizeof(buffer); }
		bool CanFit(u32 bytes) const { return bytes <= sizeof(buffer) - cursor; }
	};

	// Pure virtual methods that must be implemented by derived classes
	virtual bool WriteSerialPortByte(u8 data, bool eoi) = 0;
	virtual void ReadBrowseMode() = 0;
	virtual bool IsReset() = 0;

	// Optional method for reading serial port bytes (used by IEC, not TCBM)
	// Returns true if ATN detected (should abort), false otherwise
	virtual bool ReadSerialPortByte(u8& byte, bool eoi = false)
	{
		// Default implementation - unused by TCBM
		byte = 0;
		return false;
	}

	// Protocol-specific methods that may be overridden
	virtual bool CheckATN(void) { return false; }
	virtual void OnATNSequenceComplete() {}
	virtual bool HandleSpecialOpenFile(Channel& channel) { return false; } // Return true if handled

	virtual void Listen();
	virtual void Talk();
	virtual void LoadFile();
	virtual void SaveFile();

	void AddDirectoryEntry(Channel& channel, const char* name, u16 blocks, int fileType);
	virtual void LoadDirectory();
	void OpenFile();
	void CloseFile(u8 secondary);
	void CloseAllChannels();
	void SendError();
public:
	void Error(u8 errorCode, u8 track = 0, u8 sector = 0);
protected:

	bool Enter(DIR& dir, FILINFO& filInfo);
	bool FindFirst(DIR& dir, const char* matchstr, FILINFO& filInfo);
	static inline bool IsDirectory(FILINFO& filInfo) { return (filInfo.fattrib & AM_DIR) == AM_DIR; }

	void FolderCommand(void);
	void CD(int partition, char* filename);
	void MKDir(int partition, char* filename);
	void RMDir(void);

	void Copy(void);
	void New(void);
	void Rename(void);
	void Scratch(void);
	void ChangeDevice(void);

	void Memory(void);
	void User(void);
	void Extended(void);

	void ProcessCommand(void);

	bool SendBuffer(Channel& channel, bool eoi);

	u8 GetFilenameCharacter(u8 value);

	int WriteNewDiskInRAM(char* filenameNew, bool automount, unsigned length);

	// Shared state
	UpdateAction updateAction;
	u8 commandCode;
	bool receivedCommand : 1;
	bool receivedEOI : 1;
	
	u8 deviceID;
	u8 secondaryAddress;
	ATNSequence atnSequence;
	DeviceRole deviceRole;

	struct TimerMicroSeconds
	{
		TimerMicroSeconds()
		{
			count = 0;
			timeout = 0;
		}

		void Start(u32 amount)
		{
			count = 0;
			timeout = amount;
		}

		inline bool TimedOut() { return count >= timeout; }

		bool Tick()
		{
			// Derived classes should implement timing via their bus WaitMicroSeconds in protocol-specific code
			// This just increments the counter
			count++;
			return TimedOut();
		}

		u32 count;
		u32 timeout;
	};

	TimerMicroSeconds timer;

	Channel channels[16];

	char selectedImageName[256];
	FILINFO filInfoSelectedImage;

	const char* starFileName;

	bool displayingDevices;
	bool lowercaseBrowseModeFilenames;
	DiskImage::DiskType newDiskType;

	char errorMessage[64];
};

#endif // COMMANDS_BASE_H

