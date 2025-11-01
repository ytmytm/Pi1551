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

#include "commands_base.h"
#include "defs.h"
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

extern unsigned versionMajor;
extern unsigned versionMinor;

extern void Reboot_Pi();
extern void SwitchDrive(const char* drive);
extern int numberOfUSBMassStorageDevices;
extern void DisplayMessage(int x, int y, bool LCD, const char* message, u32 textColour, u32 backgroundColour);

#define DRIVE_NAME_OFFSET_IN_DIR_HEADER 7
#define VERSION_OFFSET_IN_DIR_HEADER 17
static u8 DirectoryHeader[] =
{
	1, 4,	// BASIC start address
	1, 1,	// next line pointer
	0, 0,	// line number 0
	0x12,	// Reverse on
	0x22,	// Quote
	'P', 'I', '1', '5', '4', '1', ' ', 'V', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', // Name (will be updated by SetHeaderVersion)
	0x22,	// Quote
	0x20,	// Space
	'P', 'I', ' ', '2', 'A',	// ID, Dos Disk 2A
	00
};

static const u8 DirectoryBlocksFree[] = {
	1, 1,	// Next line pointer
	0, 0,	// 16bit free blocks value
	'B', 'L', 'O', 'C', 'K', 'S', ' ', 'F', 'R', 'E', 'E', '.',
	0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
	0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00
};

static const u8 filetypes[] = {
	'D', 'E', 'L', // 0
	'S', 'E', 'Q', // 1
	'P', 'R', 'G', // 2
	'U', 'S', 'R', // 3
	'R', 'E', 'L', // 4
	'C', 'B', 'M', // 5
	'D', 'I', 'R', // 6
};

// TODO: When we have different file types we then need to detect these type of wildcards:
// *=S selects only sequential files
// *=P selects program files
// *=R selects relative files
// *=U selects user-files

// ErrorMessage is now a member variable

static u8* InsertNumber(u8* msg, u8 value)
{
	if (value >= 100)
	{
		*msg++ = '0' + value / 100;
		value %= 100;
	}
	*msg++ = '0' + value / 10;
	*msg++ = '0' + value % 10;
	return msg;
}

void Commands_Base::SetHeaderVersion()
{
	// Set drive name (e.g., "PI1541" or "PI1551")
	u8* ptr = DirectoryHeader + DRIVE_NAME_OFFSET_IN_DIR_HEADER;
	const char* driveName = PI_DRIVE_NAME;
	int i = 0;
	while (driveName[i] && i < 16)
	{
		*ptr++ = driveName[i++];
	}
	// Fill remaining space with spaces
	while (i < 16)
	{
		*ptr++ = ' ';
		i++;
	}
	
	// Set version number
	ptr = DirectoryHeader + VERSION_OFFSET_IN_DIR_HEADER;
	ptr = InsertNumber(ptr, versionMajor);
	*ptr++ = '.';
	ptr = InsertNumber(ptr, versionMinor);
}

void Commands_Base::Error(u8 errorCode, u8 track, u8 sector)
{
	char* msg = "UNKNOWN";
	switch (errorCode)
	{
		case ERROR_00_OK:
			msg = " OK";
		break;
		case ERROR_25_WRITE_ERROR:
			msg = "WRITE ERROR";
		break;
		case ERROR_73_DOSVERSION:
			snprintf(errorMessage, sizeof(errorMessage)-1, "%02d,%s V%02d.%02d,%02d,%02d\r", errorCode,
						PI_DRIVE_NAME, versionMajor, versionMinor, track, sector);
			return;
		break;
		case ERROR_30_SYNTAX_ERROR:
		case ERROR_31_SYNTAX_ERROR:
		case ERROR_32_SYNTAX_ERROR:
		case ERROR_33_SYNTAX_ERROR:
		case ERROR_34_SYNTAX_ERROR:
			msg = "SYNTAX ERROR";
		break;
		case ERROR_39_FILE_NOT_FOUND:
			msg = "FILE NOT FOUND";
		break;
		case ERROR_62_FILE_NOT_FOUND:
			msg = "FILE NOT FOUND";
		break;
		case ERROR_63_FILE_EXISTS:
			msg = "FILE EXISTS";
		break;
		default:
			DEBUG_LOG("EC=%d?\r\n", errorCode);
		break;
	}
	snprintf(errorMessage, sizeof(errorMessage)-1, "%02d,%s,%02d,%02d\r", errorCode, msg, track, sector);
}

static inline bool IsDirectory(FILINFO& filInfo)
{
	return (filInfo.fattrib & AM_DIR) == AM_DIR;
}

void Commands_Base::Channel::Close()
{
	if (open)
	{
		if (writing)
		{
			u32 bytesWritten;
			if (f_write(&file, buffer, cursor, &bytesWritten) != FR_OK)
			{
			}
		}
		f_close(&file);
		open = false;
	}
	cursor = 0;
	bytesSent = 0;
}

Commands_Base::Commands_Base()
{
	deviceID = 8;
	Reset();
	starFileName = 0;
	displayingDevices = false;
	lowercaseBrowseModeFilenames = false;
	newDiskType = DiskImage::D64;
}

void Commands_Base::Reset(void)
{
	receivedCommand = false;
	receivedEOI = false;
	secondaryAddress = 0;
	selectedImageName[0] = 0;
	atnSequence = ATN_SEQUENCE_IDLE;
	deviceRole = DEVICE_ROLE_PASSIVE;
	commandCode = 0;
	Error(ERROR_00_OK);
	CloseAllChannels();
}

void Commands_Base::CloseAllChannels()
{
	for (int i = 0; i < 15; ++i)
	{
		channels[i].Close();
	}
}

static bool CopyFile(char* filenameNew, char* filenameOld, bool concatenate)
{
	FRESULT res;
	FIL fpIn;
	bool success = false;
	res = f_open(&fpIn, filenameOld, FA_READ);
	if (res == FR_OK)
	{
		FIL fpOut;
		u8 mode = FA_WRITE;
		if (!concatenate) mode |= FA_CREATE_ALWAYS;
		else mode |= FA_OPEN_APPEND;

		res = f_open(&fpOut, filenameNew, mode);
		if (res == FR_OK)
		{
			char buffer[1024];
			u32 bytes;

			success = true;
			do
			{
				f_read(&fpIn, buffer, sizeof(buffer), &bytes);
				if (bytes > 0)
				{
					// TODO: Should check for disk full.
					if (f_write(&fpOut, buffer, bytes, &bytes) != FR_OK)
					{
						success = false;
						break;
					}
				}
			} while (bytes != 0);

			f_close(&fpOut);
		}
		f_close(&fpIn);
	}
	return success;
}

static const char* ParseName(const char* text, char* name, bool convert, bool includeSpace = false)
{
	char* ptrOut = name;
	const char* ptr = text;
	*name = 0;

	if (isspace(*ptr & 0x7f) || *ptr == ',' || *ptr == '=' || *ptr == ':')
	{
		ptr++;
	}

	// TODO: Should not do this - should use command length to test for the end of a command (use indicies instead of pointers?)
	while (*ptr != '\0')
	{
		if (!isspace(*ptr & 0x7f))
			break;
		ptr++;
	}
	if (*ptr != 0)
	{
		while (*ptr != '\0')
		{
			if ((!includeSpace && isspace(*ptr & 0x7f)) || *ptr == ',' || *ptr == '=' || *ptr == ':')
				break;
			if (convert) *ptrOut++ = petscii2ascii(*ptr++);
			else *ptrOut++ = *ptr++;
		}
	}
	*ptrOut = 0;
	return ptr;
}

static const char* ParseNextName(const char* text, char* name, bool convert)
{
	char* ptrOut = name;
	const char* ptr;
	*name = 0;

	// TODO: looking for these is bad for binary parameters (binary parameter commands should not come through here)
	ptr = strchr(text, ':');
	if (ptr == 0) ptr = strchr(text, '=');
	if (ptr == 0) ptr = strchr(text, ',');

	if (ptr)
		return ParseName(ptr, name, convert);
	*ptrOut = 0;
	return ptr;
}

static bool ParseFilenames(const char* text, char* filenameA, char* filenameB, Commands_Base* cmd)
{
	bool success = false;
	text = ParseNextName(text, filenameA, true);
	if (text)
	{
		ParseNextName(text, filenameB, true);
		if (filenameB[0] != 0) success = true;
		else cmd->Error(ERROR_34_SYNTAX_ERROR);	// File name could not be found in the command
	}
	else
	{
		cmd->Error(ERROR_31_SYNTAX_ERROR);	// could not parse the command
	}
	return success;
}

static int ParsePartition(char** buf)
{
	int part = 0;

	while ((isdigit(**buf & 0x7f)) || **buf == ' ' || **buf == '@')
	{
		if (isdigit(**buf & 0x7f))	part = part * 10 + (**buf - '0');
		(*buf)++;
	}
	return 0;
}

bool Commands_Base::Enter(DIR& dir, FILINFO& filInfo)
{
	filInfoSelectedImage = filInfo;

	if (DiskImage::IsDiskImageExtention(filInfo.fname))
	{
		strcpy((char*)selectedImageName, filInfo.fname);
		return true;
	}
	else if (IsDirectory(filInfo))
	{
		if (f_chdir(filInfo.fname) == FR_OK) updateAction = DIR_PUSHED;
		else Error(ERROR_62_FILE_NOT_FOUND);
	}
	return false;
}

bool Commands_Base::FindFirst(DIR& dir, const char* matchstr, FILINFO& filInfo)
{
	char pattern[256];
	FRESULT res;

	// CBM-FileBrowser can only determine if it is a disk image if the extention is in the name.
	// So for files that are too long we stomp the last 4 characters with the image extention and pattern match it.
	// This basically changes a file name from something like
	// SOMELONGDISKIMAGENAME.D64 to SOMELONGDISKIMAGENAME*.D64
	// so the actual SOMELONGDISKIMAGENAMETHATISWAYTOOLONGFORCBMFILEBROWSERTODISPLAY.D64 will be found.
	bool diskImage = DiskImage::IsDiskImageExtention(matchstr);
	strcpy(pattern, matchstr);
	if (strlen(pattern) > CBM_NAME_LENGTH_MINUS_D64)
	{
		char* ext = strrchr(matchstr, '.');
		if (ext && diskImage)
		{
			char* ptr = strrchr(pattern, '.');
			*ptr++ = '*';
			for (int i = 0; i < 4; i++)
			{
				*ptr++ = *ext++;
			}
			*ptr = 0;
		}
		else
		{
			// For folders we do the same except we need to change the last character to a *
			int len = strlen(matchstr);
			if (len >= CBM_NAME_LENGTH)
				pattern[CBM_NAME_LENGTH - 1] = '*';
		}
	}
	//DEBUG_LOG("Pattern %s -> %s\r\n", matchstr, pattern);
	res = f_findfirst(&dir, &filInfo, ".", (const TCHAR*)pattern);
	//DEBUG_LOG("found file %s\r\n", filInfo.fname);
	if (res != FR_OK || filInfo.fname[0] == 0)
	{
		//Error(ERROR_62_FILE_NOT_FOUND);
		return false;
	}
	else
	{
		return true;
	}
}

// Append this to commands_base.cpp - Shared command methods

bool Commands_Base::SendBuffer(Channel& channel, bool eoi)
{
	for (u32 i = 0; i < channel.cursor; ++i)
	{
		u8 finalbyte = eoi && (channel.bytesSent == (channel.fileSize - 1));
		if (WriteSerialPortByte(channel.buffer[i], finalbyte))
		{
			return true;
		}
		channel.bytesSent++;
	}
	channel.cursor = 0;
	return false;
}

void Commands_Base::SendError()
{
	int len = strlen(errorMessage);
	int index = 0;
	bool finalByte;
	do
	{
		finalByte = index == len;
		if (WriteSerialPortByte(errorMessage[index++], finalByte))
			break;
	}
	while (!finalByte);
}

u8 Commands_Base::GetFilenameCharacter(u8 value)
{
	if (lowercaseBrowseModeFilenames)
		value = tolower(value);

	return ascii2petscii(value);
}

void Commands_Base::AddDirectoryEntry(Channel& channel, const char* name, u16 blocks, int fileType)
{
	u8* data = channel.buffer + channel.cursor;
	const u32 dirEntryLength = DIRECTORY_ENTRY_SIZE;
	int i = 0;
	int index = 0;
	bool diskImage = DiskImage::IsDiskImageExtention(name);

	memset(data, ' ', dirEntryLength);
	data[dirEntryLength - 1] = 0;

	data[index++] = 0x01;
	data[index++] = 0x01;
	data[index++] = blocks & 0xff;
	data[index++] = blocks >> 8;

	if (blocks < 1000)
		index++;
	if (blocks < 100)
		index++;
	if (blocks < 10)
		index++;

	data[index++] = '"';

	if (strlen(name) > CBM_NAME_LENGTH && diskImage)
	{
		const char* extName = strrchr(name, '.');

		do
		{
			data[index + i++] = GetFilenameCharacter(*name++);
		}
		while (!(*name == 0x22 || *name == 0 || i == CBM_NAME_LENGTH_MINUS_D64));

		for (int extIndex = 0; extIndex < 4; ++extIndex)
		{
			data[index + i++] = GetFilenameCharacter(*extName++);
		}
	}
	else
	{
		do
		{
			data[index + i++] = GetFilenameCharacter(*name++);
		}
		while (!(*name == 0x22 || *name == 0 || i == CBM_NAME_LENGTH));
	}
	data[index + i] = '"';
	index++;
	index += CBM_NAME_LENGTH;
	index++;

	for (i = 0; i < 3; ++i)
	{
		data[index++] = filetypes[fileType * 3 + i];
	}
	channel.cursor += dirEntryLength;
}

struct greater
{
	bool operator()(const FileBrowser::BrowsableList::Entry& lhs, const FileBrowser::BrowsableList::Entry& rhs) const
	{
		if (strcasecmp(lhs.filImage.fname, "..") == 0)
			return true;
		else if (strcasecmp(rhs.filImage.fname, "..") == 0)
			return false;
		else if (((lhs.filImage.fattrib & AM_DIR) && (rhs.filImage.fattrib & AM_DIR)) || (!(lhs.filImage.fattrib & AM_DIR) && !(rhs.filImage.fattrib & AM_DIR)))
			return strcasecmp(lhs.filImage.fname, rhs.filImage.fname) < 0;
		else if ((lhs.filImage.fattrib & AM_DIR) && !(rhs.filImage.fattrib & AM_DIR))
			return true;
		else
			return false;
	}
};

void Commands_Base::LoadDirectory()
{
	DIR dir;
	char* ext;
	FRESULT res;

	Channel& channel = channels[0];

	SetHeaderVersion();
	memcpy(channel.buffer, DirectoryHeader, sizeof(DirectoryHeader));
	channel.cursor = sizeof(DirectoryHeader);

	FileBrowser::BrowsableList::Entry entry;
	std::vector<FileBrowser::BrowsableList::Entry> entries;

	if (displayingDevices)
	{
		FileBrowser::RefreshDevicesEntries(entries, true);
	}
	else
	{
		res = f_opendir(&dir, ".");
		if (res == FR_OK)
		{
			do
			{
				res = f_readdir(&dir, &entry.filImage);
				ext = strrchr(entry.filImage.fname, '.');
				if (res == FR_OK && entry.filImage.fname[0] != 0 && !(ext && strcasecmp(ext, ".png") == 0) && (entry.filImage.fname[0] != '.'))
					entries.push_back(entry);
			} while (res == FR_OK && entry.filImage.fname[0] != 0);
			f_closedir(&dir);

			std::sort(entries.begin(), entries.end(), greater());
		}
	}

	for (u32 i = 0; i < entries.size(); ++i)
	{
		FILINFO* filInfo = &entries[i].filImage;
		const char* fileName = filInfo->fname;

		if (!channel.CanFit(DIRECTORY_ENTRY_SIZE))
			SendBuffer(channel, false);

		if (filInfo->fattrib & AM_DIR) AddDirectoryEntry(channel, fileName, 0, 6);
		else AddDirectoryEntry(channel, fileName, filInfo->fsize / 256 + 1, 2);
	}

	SendBuffer(channel, false);

	memcpy(channel.buffer, DirectoryBlocksFree, sizeof(DirectoryBlocksFree));

	FATFS* fs;
	DWORD fre_clust, fre_sect, free_blocks;
	res = f_getfree("", &fre_clust, &fs);
	if (res == FR_OK)
	{
		fre_sect = fre_clust * fs->csize;
		free_blocks = fre_sect << 1;

		if (free_blocks > 0x10000)
		{
			channel.buffer[2] = 0xff;
			channel.buffer[3] = 0xff;
		}
		else
		{
			channel.buffer[2] = free_blocks & 0xff;
			channel.buffer[3] = (free_blocks >> 8) & 0xff;;
		}
	}
	else
	{
		channel.buffer[2] = 0;
		channel.buffer[3] = 0;
	}
	channel.cursor = sizeof(DirectoryBlocksFree);
	
	channel.filInfo.fsize = channel.bytesSent + channel.cursor;
	channel.fileSize = (u32)channel.filInfo.fsize;
	SendBuffer(channel, true);
}

void Commands_Base::LoadFile()
{
	Channel& channel = channels[secondaryAddress];

	if (channel.filInfo.fname[0] != 0)
	{
		FSIZE_t size = f_size(&channel.file);
		FSIZE_t sizeRemaining = size;
		u32 bytesRead;
		channel.fileSize = (u32)channel.filInfo.fsize;

		char* ext = strrchr((char*)channel.filInfo.fname, '.');
		if (ext && toupper((char)ext[1]) == 'P' && isdigit(ext[2]) && isdigit(ext[3]))
		{
			bool validP00 = false;

			f_read(&channel.file, channel.buffer, 26, &bytesRead);
			if (bytesRead > 0)
			{
				if (strncmp((const char*)channel.buffer, "C64File", 7) == 0)
				{
					validP00 = channel.buffer[0x19] == 0;
					sizeRemaining -= bytesRead;
					channel.bytesSent += bytesRead;
				}

				if (!validP00)
					f_lseek(&channel.file, 0);
			}
		}
		else if (ext && toupper((char)ext[1]) == 'T' && ext[2] == '6' && ext[3] == '4')
		{
			bool validT64 = false;

			f_read(&channel.file, channel.buffer, sizeof(channel.buffer), &bytesRead);

			if (bytesRead > 0)
			{
				if ((memcmp(channel.buffer, "C64 tape image file", 20) == 0) || (memcmp(channel.buffer, "C64s tape image file", 21) == 0))
				{
					DEBUG_LOG("T64\r\n");
					u16 version = channel.buffer[0x20] | (channel.buffer[0x21] << 8);
					u16 entries = channel.buffer[0x22] | (channel.buffer[0x23] << 8);
					u16 entriesUsed = channel.buffer[0x24] | (channel.buffer[0x25] << 8);
					char name[25] = { 0 };
					strncpy(name, (const char*)(channel.buffer + 0x28), 24);

					DEBUG_LOG("%x %d %d %s\r\n", version, entries, entriesUsed, name);

					u16 entryIndex;

					for (entryIndex = 0; entryIndex < entriesUsed; ++entryIndex)
					{
						char nameEntry[17] = { 0 };
						int offset = 0x40 + entryIndex * 32;
						u8 type = channel.buffer[offset];
						u8 fileType = channel.buffer[offset + 1];
						u16 startAddress = channel.buffer[offset + 2] | (channel.buffer[offset + 3] << 8);
						u16 endAddress = channel.buffer[offset + 4] | (channel.buffer[offset + 5] << 8);
						u32 fileOffset = channel.buffer[offset + 8] | (channel.buffer[offset + 9] << 8) | (channel.buffer[offset + 10] << 16) | (channel.buffer[offset + 11] << 24);
						strncpy(nameEntry, (const char*)(channel.buffer + offset + 0x10), 16);

						DEBUG_LOG("%d %02x %04x %04x %0x8 %s\r\n", type, fileType, startAddress, endAddress, fileOffset, nameEntry);

						channel.bytesSent = 0;
						channel.buffer[0] = startAddress & 0xff;
						channel.buffer[1] = (startAddress >> 8) & 0xff;
	
						validT64 = true;
						sizeRemaining = endAddress - startAddress;
						channel.fileSize = sizeRemaining + 2;
						channel.bytesSent = 0;
						channel.cursor = 2;

						SendBuffer(channel, false);

						f_lseek(&channel.file, fileOffset);

						break; // For now only load the first file.
					}
				}

				if (!validT64)
					f_lseek(&channel.file, 0);
			}
		}

		do
		{
			f_read(&channel.file, channel.buffer, sizeof(channel.buffer), &bytesRead);
			if (bytesRead > 0)
			{
				sizeRemaining -= bytesRead;
				channel.cursor = bytesRead;
				if (SendBuffer(channel, sizeRemaining <= 0))
					return;
			}
		}
		while (bytesRead > 0);
	}
	else
	{
		Error(ERROR_62_FILE_NOT_FOUND);
	}
}

void Commands_Base::SaveFile()
{
	u32 bytesWritten;
	u8 byte;

	Channel& channel = channels[secondaryAddress];
	if (channel.open && channel.writing)
	{
		while (!ReadSerialPortByte(byte, false))
		{
			channel.buffer[channel.cursor++] = byte;
			if (channel.WriteFull())
			{
				if (f_write(&channel.file, channel.buffer, sizeof(channel.buffer), &bytesWritten) != FR_OK)
				{
				}
				channel.cursor = 0;
			}
		}
	}
}

void Commands_Base::CloseFile(u8 secondary)
{
	Channel& channel = channels[secondary];
	channel.Close();
}

void Commands_Base::Copy(void)
{
	char filenameNew[256];
	char filenameToCopy[256];
	Channel& channel = channels[15];

	FILINFO filInfo;
	FRESULT res;
	const char* text = (char*)channel.buffer;

	text = ParseNextName(text, filenameNew, true);

	if (filenameNew[0] != 0)
	{
		res = f_stat(filenameNew, &filInfo);
		if (res == FR_NO_FILE)
		{
			int fileCount = 0;
			do
			{
				text = ParseNextName(text, filenameToCopy, true);
				if (filenameToCopy[0] != 0)
				{
					res = f_stat(filenameToCopy, &filInfo);
					if (res == FR_OK)
					{
						if (!IsDirectory(filInfo))
						{
							if (CopyFile(filenameNew, filenameToCopy, fileCount != 0)) updateAction = REFRESH;
							else Error(ERROR_25_WRITE_ERROR);
						}
					}
					else
					{
						Error(ERROR_62_FILE_NOT_FOUND);
					}
				}
				fileCount++;
			} while (filenameToCopy[0] != 0);
		}
		else
		{
			DEBUG_LOG("Copy file exists\r\n");
			Error(ERROR_63_FILE_EXISTS);
		}
	}
	else
	{
		Error(ERROR_34_SYNTAX_ERROR);
	}
}

void Commands_Base::ChangeDevice(void)
{
	Channel& channel = channels[15];
	const char* text = (char*)channel.buffer;

	if (strlen(text) > 2)
	{
		int deviceIndex = atoi(text + 2);

		if (deviceIndex == 0)
		{
			SwitchDrive("SD:");
			displayingDevices = false;
			updateAction = DEVICE_SWITCHED;
		}
		else if ((deviceIndex - 1) < numberOfUSBMassStorageDevices)
		{
			char USBDriveId[16];
			sprintf(USBDriveId, "USB%02d:", deviceIndex);
			SwitchDrive(USBDriveId);
			displayingDevices = false;
			updateAction = DEVICE_SWITCHED;
		}
		else
		{
			Error(ERROR_74_DRlVE_NOT_READY);
		}
	}
	else
	{
		Error(ERROR_31_SYNTAX_ERROR);
	}
}

void Commands_Base::Memory(void)
{
	Channel& channel = channels[15];
	char* text = (char*)channel.buffer;
	u16 address;
	int length;
	u8 bytes = 1;
	u8* ptr;

	if (channel.cursor > 2)
	{
		char code = toupper(channel.buffer[2]);
		if (code == 'R' || code == 'W' || code == 'E')
		{
			ptr = (u8*)strchr(text, ':');
			if (ptr == 0) ptr = (u8*)&channel.buffer[3];
			else ptr++;

			length = channel.cursor - 3;

			address = (u16)((u8)(ptr[1]) << 8) | (u16)ptr[0];
			if (length > 2)
			{
				bytes = ptr[2];
				if (bytes == 0)
					bytes = 1;
			}

			switch (code)
			{
				case 'R':
					DEBUG_LOG("M-R %04x %d\r\n", address, bytes);
				break;
				case 'W':
					DEBUG_LOG("M-W %04x %d\r\n", address, bytes);
				break;
				case 'E':
					DEBUG_LOG("M-E %04x\r\n", address);
				break;
			}
		}
		else
		{
			Error(ERROR_31_SYNTAX_ERROR);
		}
	}
}

void Commands_Base::New(void)
{
	Channel& channel = channels[15];
	char filenameNew[256];
	char ID[256];

	if (ParseFilenames((char*)channel.buffer, filenameNew, ID, this))
	{
		int ret = CreateNewDisk(filenameNew, ID, true);

		if (ret==0)
			updateAction = REFRESH;
		else
			Error(ret);
	}
}

void Commands_Base::Rename(void)
{
	Channel& channel = channels[15];
	char filenameNew[256];
	char filenameOld[256];

	if (ParseFilenames((char*)channel.buffer, filenameNew, filenameOld, this))
	{
		FRESULT res;
		FILINFO filInfo;

		res = f_stat(filenameNew, &filInfo);
		if (res == FR_NO_FILE)
		{
			res = f_stat(filenameOld, &filInfo);
			if (res == FR_OK)
			{
				f_rename(filenameOld, filenameNew);
			}
			else
			{
				Error(ERROR_62_FILE_NOT_FOUND);
			}
		}
		else
		{
			Error(ERROR_63_FILE_EXISTS);
		}
	}
}

void Commands_Base::Scratch(void)
{
	Channel& channel = channels[15];
	DIR dir;
	FILINFO filInfo;
	FRESULT res;
	char filename[256];
	const char* text = (const char*)channel.buffer;

	text = ParseNextName(text, filename, true);
	while (filename[0])
	{
		res = f_findfirst(&dir, &filInfo, ".", (const TCHAR*)filename);
		while (res == FR_OK && filInfo.fname[0])
		{
			if (filInfo.fname[0] != 0 && !IsDirectory(filInfo))
			{
				f_unlink(filInfo.fname);
			}
			res = f_findnext(&dir, &filInfo);
			updateAction = REFRESH;
		}
		text = ParseNextName(text, filename, true);
	}
}

void Commands_Base::User(void)
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
			// VIC20 timing handling is IEC-specific, so derived classes handle this
			Error(ERROR_73_DOSVERSION);
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
			Error(ERROR_31_SYNTAX_ERROR);
		break;
	}
}

void Commands_Base::Extended(void)
{
	Channel& channel = channels[15];

	switch (toupper(channel.buffer[1]))
	{
		case '?':
			Error(ERROR_73_DOSVERSION);
		break;
		default:
			Error(ERROR_31_SYNTAX_ERROR);
		break;
	}
}

void Commands_Base::ProcessCommand(void)
{
	Error(ERROR_00_OK);

	Channel& channel = channels[15];

	if (channel.cursor > 0 && channel.buffer[channel.cursor - 1] == 0x0d)
		channel.cursor--;

	if (channel.cursor == 0)
	{
		Error(ERROR_30_SYNTAX_ERROR);
	}
	else
	{
		if (toupper(channel.buffer[0]) != 'X' && toupper(channel.buffer[1]) == 'D')
		{
			FolderCommand();
			return;
		}

		switch (toupper(channel.buffer[0]))
		{
			case 'B':
				Error(ERROR_31_SYNTAX_ERROR);
			break;
			case 'C':
				if (channel.buffer[1] == 'P')
					ChangeDevice();
				else
					Copy();
			break;
			case 'D':
				Error(ERROR_31_SYNTAX_ERROR);
			break;
			case 'G':
				Error(ERROR_31_SYNTAX_ERROR);
			break;
			case 'I':
				// Initialise
			break;
			case 'M':
				Memory();
			break;
			case 'N':
				New();
			break;
			case 'P':
				Error(ERROR_31_SYNTAX_ERROR);
			break;
			case 'R':
				Rename();
			break;
			case 'S':
				if (channel.buffer[1] == '-')
				{
					Error(ERROR_31_SYNTAX_ERROR);
					break;
				}
				Scratch();
			break;
			case 'T':
				Error(ERROR_31_SYNTAX_ERROR);
			break;
			case 'U':
				User();
			break;
			case 'V':
			break;
			case 'W':
			break;
			case 'X':
				Extended();
			break;
			case '/':
			break;
			default:
				Error(ERROR_31_SYNTAX_ERROR);
			break;
		}
	}
}

void Commands_Base::CD(int partition, char* filename)
{
	char filenameEdited[256];

	if (filename[0] == '/' && filename[1] == '/')
		sprintf(filenameEdited, "\\\\1541\\%s", filename + 2);
	else
		strcpy(filenameEdited, filename);

	int len = strlen(filenameEdited);

	for (int i = 0; i < len; i++)
	{
		if (filenameEdited[i] == '/')
			filenameEdited[i] = '\\';
		filenameEdited[i] = petscii2ascii(filenameEdited[i]);
	}

	DEBUG_LOG("CD %s\r\n", filenameEdited);
	if (filenameEdited[0] == '_' && len == 1)
	{
		updateAction = POP_DIR;
	}
	else
	{
		if (displayingDevices)
		{
			if (strncmp(filename, "SD", 2) == 0)
			{
				SwitchDrive("SD:");
				displayingDevices = false;
				updateAction = DEVICE_SWITCHED;
			}
			else
			{
				for (int USBDriveIndex = 0; USBDriveIndex < numberOfUSBMassStorageDevices; ++USBDriveIndex)
				{
					char USBDriveId[16];
					sprintf(USBDriveId, "USB%02d:", USBDriveIndex + 1);

					if (strncmp(filename, USBDriveId, 5) == 0)
					{
						SwitchDrive(USBDriveId);
						displayingDevices = false;
						updateAction = DEVICE_SWITCHED;
					}
				}
			}
		}
		else
		{
			DIR dir;
			FILINFO filInfo;

			char path[256] = { 0 };
			char* pattern = strrchr(filenameEdited, '\\');

			if (pattern)
			{
				int len = pattern - filenameEdited;
				strncpy(path, filenameEdited, len);

				pattern++;

				if ((f_stat(path, &filInfo) != FR_OK) || !IsDirectory(filInfo))
				{
					Error(ERROR_62_FILE_NOT_FOUND);
				}
				else
				{
					char cwd[1024];
					if (f_getcwd(cwd, 1024) == FR_OK)
					{
						f_chdir(path);

						bool found = f_findfirst(&dir, &filInfo, ".", pattern) == FR_OK && filInfo.fname[0] != 0;

						if (found)
						{
							if (DiskImage::IsDiskImageExtention(filInfo.fname))
							{
								if (f_stat(filInfo.fname, &filInfoSelectedImage) == FR_OK)
								{
									strcpy((char*)selectedImageName, filInfo.fname);
								}
								else
								{
									f_chdir(cwd);
									Error(ERROR_62_FILE_NOT_FOUND);
								}
							}
							else
							{
								if (f_chdir(filInfo.fname) != FR_OK)
								{
									Error(ERROR_62_FILE_NOT_FOUND);
									f_chdir(cwd);
								}
								else
								{
									updateAction = DIR_PUSHED;
								}
							}
						}
						else
						{
							Error(ERROR_62_FILE_NOT_FOUND);
							f_chdir(cwd);
						}
					}
				}
			}
			else
			{
				bool found = FindFirst(dir, filenameEdited, filInfo);

				if (found)
				{
					if (DiskImage::IsDiskImageExtention(filInfo.fname))
					{
						if (f_stat(filInfo.fname, &filInfoSelectedImage) == FR_OK)
							strcpy((char*)selectedImageName, filInfo.fname);
						else
							Error(ERROR_62_FILE_NOT_FOUND);
					}
					else
					{
						if (f_chdir(filInfo.fname) != FR_OK)
							Error(ERROR_62_FILE_NOT_FOUND);
						else
							updateAction = DIR_PUSHED;
					}
				}
				else
				{
					Error(ERROR_62_FILE_NOT_FOUND);
				}
			}
		}
	}
}

void Commands_Base::MKDir(int partition, char* filename)
{
	char filenameEdited[256];

	if (filename[0] == '/' && filename[1] == '/')
		sprintf(filenameEdited, "\\\\1541\\%s", filename + 2);
	else
		strcpy(filenameEdited, filename);
	int len = strlen(filenameEdited);

	for (int i = 0; i < len; i++)
	{
		if (filenameEdited[i] == '/')
			filenameEdited[i] = '\\';

		filenameEdited[i] = petscii2ascii(filenameEdited[i]);
	}

	f_mkdir(filenameEdited);
	updateAction = REFRESH;
}

void Commands_Base::RMDir(void)
{
	DIR dir;
	FILINFO filInfo;
	FRESULT res;
	char filename[256];
	Channel& channel = channels[15];

	const char* text = (char*)channel.buffer;

	text = ParseNextName(text, filename, true);
	if (filename[0])
	{
		res = f_findfirst(&dir, &filInfo, ".", (const TCHAR*)filename);
		if (res == FR_OK)
		{
			if (filInfo.fname[0] != 0 && IsDirectory(filInfo))
			{
				DEBUG_LOG("rmdir %s\r\n", filInfo.fname);
				f_unlink(filInfo.fname);
				updateAction = REFRESH;
			}
		}
		else
		{
			Error(ERROR_62_FILE_NOT_FOUND);
		}
	}
	else
	{
		Error(ERROR_34_SYNTAX_ERROR);
	}
}

void Commands_Base::FolderCommand(void)
{
	Channel& channel = channels[15];

	switch (toupper(channel.buffer[0]))
	{
		case 'M':
		{
			char* in = (char*)channel.buffer;
			int part;

			part = ParsePartition(&in);
			if (part > 0)
			{
				return;
			}
			in += 2;
			if (*in == ':')
				in++;
			MKDir(part, in);
		}
		break;
		case 'C':
		{
			char* in = (char*)channel.buffer;
			int part;

			part = ParsePartition(&in);
			if (part > 0)
			{
				return;
			}
			in += 2;
			if (*in == ':')
				in++;
			CD(part, in);
		}
		break;
		case 'R':
			RMDir();
		break;
		default:
			Error(ERROR_31_SYNTAX_ERROR);
		break;
	}
}

int Commands_Base::CreateNewDisk(char* filenameNew, char* ID, bool automount)
{
	DisplayMessage(240, 280, false, "Creating new disk", RGBA(0xff, 0xff, 0xff, 0xff), RGBA(0xff, 0, 0, 0xff));
	DisplayMessage(0, 0, true, "Creating new disk", RGBA(0xff, 0xff, 0xff, 0xff), RGBA(0xff, 0, 0, 0xff));

	switch (newDiskType)
	{
		case DiskImage::D64:
			if (!(strstr(filenameNew, ".d64") || strstr(filenameNew, ".D64")))
				strcat(filenameNew, ".d64");
		break;
		case DiskImage::G64:
			if (!(strstr(filenameNew, ".g64") || strstr(filenameNew, ".G64")))
				strcat(filenameNew, ".g64");
		break;
		default:
			return ERROR_25_WRITE_ERROR;
		break;
	}

	unsigned length = DiskImage::CreateNewDiskInRAM(filenameNew, ID);

	return WriteNewDiskInRAM(filenameNew, automount, length);
}

int Commands_Base::WriteNewDiskInRAM(char* filenameNew, bool automount, unsigned length)
{
	FILINFO filInfo;
	FRESULT res;

	res = f_stat(filenameNew, &filInfo);
	if (res == FR_NO_FILE)
	{
		DiskImage diskImage;
		diskImage.OpenD64((const FILINFO*)0, (unsigned char*)DiskImage::readBuffer, length);

		switch (newDiskType)
		{
			case DiskImage::D64:
				if (!diskImage.WriteD64(filenameNew))
					return ERROR_25_WRITE_ERROR;
			break;
			case DiskImage::G64:
				if (!diskImage.WriteG64(filenameNew))
					return ERROR_25_WRITE_ERROR;
			break;
			default:
				return ERROR_25_WRITE_ERROR;
			break;
		}

		if (automount && f_stat(filenameNew, &filInfo) == FR_OK)
		{
			DIR dir;
			Enter(dir, filInfo);
		}
		return(ERROR_00_OK);
	}
	else
	{
		return(ERROR_63_FILE_EXISTS);
	}
}

void Commands_Base::OpenFile()
{
	u8 secondary = secondaryAddress;
	Channel& channel = channels[secondary];
	if (channel.command[0] == '#')
	{
		// Direct access is unsupported. Allow derived classes to handle special cases
		if (HandleSpecialOpenFile(channel))
		{
			return;
		}
	}
	else if (channel.command[0] == '$')
	{
	}
	else
	{
		if (!channel.open)
		{
			bool found = false;
			DIR dir;
			FRESULT res;
			const char* text;
			char filename[256];
			char filetype[8];
			char filemode[8];
			bool needFileToExist = true;
			bool writing = false;
			u8 mode = FA_READ;

			filetype[0] = 0;
			filemode[0] = 0;

			if (secondary == 1)
				strcat(filemode, "W");

			char* in = (char*)channel.command;
			int part = ParsePartition(&in);
			if (part > 0)
			{
				return;
			}
			if (*in == ':')
				in++;
			else
				in = (char*)channel.command;

			text = ParseName((char*)in, filename, true, true);
			if (text)
			{
				text = ParseNextName(text, filetype, true);
				if (text)
					text = ParseNextName(text, filemode, true);
			}

			if (starFileName && starFileName[0] != 0 && filename[0] == '*')
			{
				char cwd[1024];
				if (f_getcwd(cwd, 1024) == FR_OK)
				{
					const char* folder = strstr(cwd, "/");
					if (folder)
					{
						if (strcasecmp(folder, "/1541") == 0)
						{
							strncpy(filename, starFileName, sizeof(filename) - 1);
						}
					}
				}
			}
			

			if (toupper(filetype[0]) == 'L')
			{
				return;
			}
			else
			{
				switch (toupper(filemode[0]))
				{
					case 'W':
						needFileToExist = false;
						writing = true;
						mode = FA_CREATE_ALWAYS | FA_WRITE;
					break;
					case 'A':
						needFileToExist = true;
						writing = true;
						mode = FA_OPEN_APPEND | FA_WRITE;
					break;
					case 'R':
						needFileToExist = true;
						writing = false;
						mode = FA_READ;
					break;
				}
			}

			channel.writing = writing;

			if (needFileToExist)
			{
				if (FindFirst(dir, filename, channel.filInfo))
				{
					res = FR_OK;
					while ((channel.filInfo.fattrib & AM_DIR) == AM_DIR)
					{
						res = f_findnext(&dir, &channel.filInfo);
					}

					if (res == FR_OK && channel.filInfo.fname[0] != 0)
					{
						found = true;
						res = f_open(&channel.file, channel.filInfo.fname, mode);
						if (res == FR_OK)
							channel.open = true;
					}
				}
				else
				{
					Error(ERROR_62_FILE_NOT_FOUND);
				}

				if (!found)
				{
					DEBUG_LOG("Can't find %s", filename);
					Error(ERROR_62_FILE_NOT_FOUND);
				}
			}
			else
			{
				res = f_open(&channel.file, filename, mode);
				if (res == FR_OK)
				{
					channel.open = true;
					channel.cursor = 0;
					res = f_stat(filename, &channel.filInfo);
				}
			}
		}
	}
}

void Commands_Base::Listen()
{
	u8 byte;

	if ((commandCode & 0x0f) == 0x0f || (commandCode & 0xf0) == 0xf0)
	{
		Channel& channel = channels[15];
		channel.Close();

		while (!ReadSerialPortByte(byte, false))
		{
			if (!channel.WriteFull())
			{
				channel.buffer[channel.cursor++] = byte;
			}
			if (receivedEOI)
				receivedCommand = true;
		}

		if (channel.cursor > 1)
		{
			// Strip any CRs from the command
			if (channel.buffer[channel.cursor - 1] == 0x0d) channel.cursor -= 1;
			else if (channel.cursor > 2 && channel.buffer[channel.cursor - 2] == 0x0d) channel.cursor -= 2;
		}

		// TODO: Should not do this - should use command length to test for the end of a command
		if (!channel.WriteFull())
			channel.buffer[channel.cursor++] = 0;
	}
	else
	{
		OpenFile();
		SaveFile();
	}
}

void Commands_Base::Talk()
{
	if (commandCode == 0x6f)
	{
		SendError();
	}
	else
	{
		Channel& channelCommand = channels[15];

		if (channelCommand.buffer[0] == '$')
		{
			LoadDirectory();
		}
		else
		{
			OpenFile();
			LoadFile();
		}
	}
}

