// Pi1551 - TCBM browser-mode command handler
// Copyright(C) 2024-2026 Maciej Witkowiak
//
// This file is part of Pi1551.
//
// Pi1551 is free software : you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Pi1551 is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Pi1551. If not, see <http://www.gnu.org/licenses/>.

#include "tcbm_commands.h"

#include "cbm_diskimage.h"
#include "tcbm_bus.h"
#include "m6523.h"
#include "DiskImage.h"
#include "FileBrowser.h"
#include "Petscii.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace
{
    constexpr u8 CMD_LISTEN   = 0x20;
    constexpr u8 CMD_UNLISTEN = 0x3F;
    constexpr u8 CMD_TALK     = 0x40;
    constexpr u8 CMD_UNTALK   = 0x5F;

    constexpr u8 SEC_OPEN     = 0xF0;
    constexpr u8 SEC_CLOSE    = 0xE0;
    constexpr u8 SEC_SECOND   = 0x60;
    constexpr u8 SEC_FAST     = 0x70;

    const char* RoleToString(Commands_Base::DeviceRole role)
    {
        switch (role)
        {
            case Commands_Base::DEVICE_ROLE_LISTEN: return "LIST";
            case Commands_Base::DEVICE_ROLE_TALK:   return "TALK";
            default:                                return "PASS";
        }
    }

}

int  TCBM_Commands::debugWriteStep = 0;
char TCBM_Commands::debugWriteBuffer[64] = { 0 };
char TCBM_Commands::debugLines[DEBUG_LINE_CAPACITY][DEBUG_LINE_LENGTH] = { { 0 } };
int  TCBM_Commands::debugLineCount = 0;
char TCBM_Commands::debugHistory[DEBUG_LINE_CAPACITY][DEBUG_LINE_LENGTH] = { { 0 } };
int  TCBM_Commands::debugHistoryCount = 0;
char TCBM_Commands::lastTimeoutMessage[DEBUG_LINE_LENGTH] = { 0 };

TCBM_Commands::TCBM_Commands()
{
    deviceID = 8;
    ResetStateMachine();
}

void TCBM_Commands::Initialise()
{
    SetHeaderVersion();
    PrepareBrowseIdleBus();
}

void TCBM_Commands::Reset(void)
{
    Commands_Base::Reset();
    ResetStateMachine();
    PrepareBrowseIdleBus();
}

void TCBM_Commands::SimulateIECBegin(void)
{
    ClearPendingImageSelection();
    SetHeaderVersion();
    ResetStateMachine();

    TCBM_Bus::SetDataInput();
    TCBM_Bus::ReadBrowseMode();
    TCBM_Bus::SetStatus(0);
    TCBM_Bus::AssertACK();
}

Commands_Base::UpdateAction TCBM_Commands::SimulateIECUpdate(void)
{
    if (TCBM_Bus::IsReset())
    {
        do
        {
            TCBM_Bus::ReadBrowseMode();
            TCBM_Bus::WaitMicroSeconds(100);
        }
        while (TCBM_Bus::IsReset());
        TCBM_Bus::WaitMicroSeconds(20);
        ResetStateMachine();
        PrepareBrowseIdleBus();
        ExitQuasiMountIfActive();
        return RESET;
    }

    // Honor pending POP_DIR requested by ROM traps before any further processing
    if (pendingPopDir)
    {
        pendingPopDir = false;
        return POP_DIR;
    }

    updateAction = NONE;

    if (selectedImageName[0] != 0) updateAction = IMAGE_SELECTED;

    // Refresh bus snapshot each tick so we see host changes even when idle
    TCBM_Bus::ReadBrowseMode();

    switch (tcbmState)
    {
        case TCBM_STATE_IDLE:
        {
            u8 commandCode = ReadTCBMCommandByte();
            if (commandCode == TCBM_CODE_COMMAND)
            {
                u8 dataByte;
                if (ReadTCBMDataByteBlocking(dataByte))
                    HandleIdleCommand(dataByte);
            }
            else if (commandCode == TCBM_CODE_SECOND || commandCode == TCBM_CODE_RECV)
            {
                u8 discard;
                ReadTCBMDataByteBlocking(discard);
            }
            else if (selectedImageName[0] != 0)
            {
                updateAction = IMAGE_SELECTED;
            }
            break;
        }

        case TCBM_STATE_OPEN:
            ServiceOpenState();
            break;

        case TCBM_STATE_LOAD:
            ServiceLoadState();
            break;

        case TCBM_STATE_SAVE:
            ServiceSaveState();
            break;

        case TCBM_STATE_DIR:
            ServiceDirectoryState();
            break;

		case TCBM_STATE_FASTLOAD:
			ServiceFastLoadState();
			break;

		case TCBM_STATE_FASTDIR:
			ServiceFastDirectoryState();
			break;

		case TCBM_STATE_FAST_BLOCKREAD:
			ServiceFastBlockReadState();
			break;

		case TCBM_STATE_FAST_BLOCKWRITE:
			ServiceFastBlockWriteState();
			break;
    }

    // Check for disk image selection after state machine processing
    // (CD command may have set selectedImageName during state processing)
    if (selectedImageName[0] != 0) updateAction = IMAGE_SELECTED;

    UpdateDebugOverlay();
    return updateAction;
}

const char* TCBM_Commands::GetStateName() const
{
    switch (tcbmState)
    {
        case TCBM_STATE_IDLE: return "IDLE";
        case TCBM_STATE_OPEN: return "OPEN";
        case TCBM_STATE_LOAD: return "LOAD";
        case TCBM_STATE_SAVE: return "SAVE";
		case TCBM_STATE_DIR:  return statusActive ? "STATUS" : "DIR";
		case TCBM_STATE_FASTLOAD: return "FAST LOAD";
		case TCBM_STATE_FASTDIR:  return statusActive ? "FAST STAT" : "FAST DIR";
		case TCBM_STATE_FAST_BLOCKREAD:  return "FAST BR";
		case TCBM_STATE_FAST_BLOCKWRITE: return "FAST BW";
        default:              return "?";
    }
}

const char* const* TCBM_Commands::GetDebugLines(int& lineCount) const
{
    static const char* linePtrs[DEBUG_LINE_CAPACITY];
    for (int i = 0; i < debugLineCount; ++i)
        linePtrs[i] = debugLines[i];
    lineCount = debugLineCount;
    return linePtrs;
}

void TCBM_Commands::AbortBlockingTransfer(const char* reason)
{
    if (reason && reason[0] != '\0')
        PushDebugLine("ABORT: %s", reason);
    ResetStateMachine();
    PrepareBrowseIdleBus();
}

void TCBM_Commands::ResetStateMachine()
{
    tcbmState        = TCBM_STATE_IDLE;
    deviceRole       = DEVICE_ROLE_PASSIVE;
    secondaryAddress = 0;
    activeChannel    = 0;
    statusActive     = false;
    directoryActive  = false;
    pendingPopDir    = false;
    captureOutput    = false;
    captureChannel   = 0;
    captureLength    = 0;
    std::memset(captureBuffer, 0, sizeof(captureBuffer));
    debugWriteStep   = 0;
    debugWriteBuffer[0] = '\0';
    lastTimeoutMessage[0] = '\0';
    debugHistoryCount = 0;
	fastCtx.initialised = false;
	fastCtx.ackLevel = 1;
	fastCtx.expectedDav = 0;
	fastCtx.status = TCBM_STATUS_OK;
	fastRequest.type = FAST_REQ_NONE;
	fastRequest.track = 0;
	fastRequest.sector = 0;
	fastRequest.blockCount = 0;
	fastRequest.filename[0] = '\0';
	blockIoBytesRemaining = 0;
	cbm_image_block_io_end();
	ClearDebugOverlay();
	SetDebugLine(0, "TCBM idle (%s)", RoleToString(deviceRole));
}

void TCBM_Commands::ClearDebugOverlay()
{
    for (int i = 0; i < DEBUG_LINE_CAPACITY; ++i)
        debugLines[i][0] = '\0';
    debugLineCount = 0;
}

void TCBM_Commands::PushDebugLine(const char* fmt, ...)
{
    if (DEBUG_LINE_CAPACITY == 0)
        return;

    if (debugHistoryCount < DEBUG_LINE_CAPACITY)
        ++debugHistoryCount;

    for (int i = debugHistoryCount - 1; i > 0; --i)
        std::strncpy(debugHistory[i], debugHistory[i - 1], DEBUG_LINE_LENGTH);

    va_list args;
    va_start(args, fmt);
    std::vsnprintf(debugHistory[0], DEBUG_LINE_LENGTH, fmt, args);
    va_end(args);
}

void TCBM_Commands::SetDebugLine(int index, const char* fmt, ...)
{
    if (index < 0 || index >= DEBUG_LINE_CAPACITY)
        return;

    va_list args;
    va_start(args, fmt);
    std::vsnprintf(debugLines[index], DEBUG_LINE_LENGTH, fmt, args);
    va_end(args);

    if (index + 1 > debugLineCount)
        debugLineCount = index + 1;
}

void TCBM_Commands::UpdateDebugOverlay()
{
    TCBM_Bus::ReadBrowseMode();
    ClearDebugOverlay();

    SetDebugLine(0, "STATE:%-6s ROLE:%-4s DEV:%d SEC:%02u", GetStateName(), RoleToString(deviceRole), deviceID, secondaryAddress);

    bool dav = TCBM_Bus::IsDAVAsserted();
    bool ack = TCBM_Bus::GetPI_ACK();
    u8   status = TCBM_Bus::GetPI_Status();
    u8   dio    = TCBM_Bus::GetPI_Data();
    SetDebugLine(1, "BUS  DAV:%d ACK:%d ST:$%02X DIO:$%02X", dav ? 1 : 0, ack ? 1 : 0, status & 0x03, dio);

    if (debugWriteStep > 0 && debugWriteBuffer[0] != '\0')
        SetDebugLine(2, "WRITE[%d] %s", debugWriteStep, debugWriteBuffer);

    if (lastTimeoutMessage[0] != '\0')
        SetDebugLine(3, "%s", lastTimeoutMessage);

    const Channel& channel0 = channels[0];
    if (channel0.command[0] != '\0')
        SetDebugLine(4, "CH0 CMD: %s", reinterpret_cast<const char*>(channel0.command));

    if (channels[15].command[0] != '\0')
        SetDebugLine(5, "CMD CH15: %s", reinterpret_cast<const char*>(channels[15].command));

    if (statusActive)
        SetDebugLine(6, "STATUS: %s", errorMessage);

    if (directoryActive)
    {
        const Channel& dirChannel = channels[secondaryAddress];
        SetDebugLine(7, "DIR   size:%u sent:%u", dirChannel.cursor, dirChannel.bytesSent);
    }

    if (channels[secondaryAddress].open)
    {
        const Channel& ch = channels[secondaryAddress];
        FSIZE_t pos = f_tell(const_cast<FIL*>(&ch.file));
        SetDebugLine(8, "FILE  pos:%lu size:%lu", static_cast<unsigned long>(pos), static_cast<unsigned long>(ch.fileSize));
    }
    else if (lastOpenPath[0] != '\0')
        SetDebugLine(8, "OPEN: %s", lastOpenPath);

    int lineIndex = debugLineCount;
    for (int i = 0; i < debugHistoryCount && lineIndex < DEBUG_LINE_CAPACITY; ++i, ++lineIndex)
        SetDebugLine(lineIndex, "%s", debugHistory[i]);
}

void TCBM_Commands::NoteTimeout(const char* what)
{
    std::snprintf(lastTimeoutMessage, DEBUG_LINE_LENGTH, "TIMEOUT waiting for %s", what);
}

bool TCBM_Commands::WaitForDAVState(bool asserted, const char* stage, u32 timeoutUs)
{
    u32 waited = 0;
    while ((TCBM_Bus::IsDAVAsserted() ? true : false) != asserted)
    {
        if (TCBM_Bus::IsReset())
            return false;

        if (waited >= timeoutUs)
        {
            NoteTimeout(stage);
            return false;
        }

        TCBM_Bus::ReadBrowseMode();
        TCBM_Bus::WaitMicroSeconds(POLL_DELAY_US);
        waited += POLL_DELAY_US;
    }

    TCBM_Bus::ReadBrowseMode();
    return true;
}

u8 TCBM_Commands::ReadTCBMCommandByte()
{
    TCBM_Bus::ReadBrowseMode();
    if (!TCBM_Bus::IsDAVAsserted())
        return 0;

    TCBM_Bus::ReadBrowseMode();
    u8 first = TCBM_Bus::GetPI_Data();
    TCBM_Bus::ReadBrowseMode();
    u8 second = TCBM_Bus::GetPI_Data();

    if (first != second)
        return 0;

    if (!(second & 0x80))
        return 0;

    if (second != TCBM_CODE_COMMAND && second != TCBM_CODE_SECOND &&
        second != TCBM_CODE_RECV    && second != TCBM_CODE_SEND)
        return 0;

    TCBM_Bus::ReleaseACK();
    TCBM_Bus::ReadBrowseMode();
    return second;
}

bool TCBM_Commands::ReadTCBMCommandByteBlocking(u8& value)
{
    if (!WaitForDAVState(true, "DAV=1 (command)"))
        return false;

    for (unsigned attempt = 0; attempt < MAX_COMMAND_STABLE_ATTEMPTS; ++attempt)
    {
        if (TCBM_Bus::IsReset())
            return false;

        TCBM_Bus::ReadBrowseMode();
        u8 first = TCBM_Bus::GetPI_Data();
        TCBM_Bus::ReadBrowseMode();
        u8 second = TCBM_Bus::GetPI_Data();

        if (first == second && (second & 0x80) &&
            (second == TCBM_CODE_COMMAND || second == TCBM_CODE_SECOND ||
             second == TCBM_CODE_RECV    || second == TCBM_CODE_SEND))
        {
            TCBM_Bus::ReleaseACK();
            TCBM_Bus::ReadBrowseMode();
            value = second;
            return true;
        }

        if (!WaitForDAVState(true, "DAV=1 (stable command)"))
            return false;
    }

    return false;
}

bool TCBM_Commands::ReadTCBMDataByteBlocking(u8& value, u8 status)
{
    if (!WaitForDAVState(false, "DAV=0 (data ready)"))
        return false;

    TCBM_Bus::ReadBrowseMode();
    value = TCBM_Bus::GetPI_Data();

	TCBM_Bus::SetStatus(status);
	TCBM_Bus::AssertACK();

	if (!WaitForDAVState(true, "DAV=1 (data ack)"))
	{
		TCBM_Bus::SetStatus(TCBM_STATUS_OK);
		return false;
	}

	TCBM_Bus::SetStatus(TCBM_STATUS_OK);
	TCBM_Bus::ReadBrowseMode();
	return true;
}

void TCBM_Commands::HandleIdleCommand(u8 commandByte)
{
    switch (commandByte)
    {
        case CMD_LISTEN:
        {
            deviceRole = DEVICE_ROLE_LISTEN;

            u8 command;
            if (!ReadTCBMCommandByteBlocking(command) || command != TCBM_CODE_SECOND)
                return;

            u8 secondary;
            if (!ReadTCBMDataByteBlocking(secondary))
                return;

            HandleListenSecondary(secondary);
            break;
        }

        case CMD_UNLISTEN:
            deviceRole = DEVICE_ROLE_PASSIVE;
            tcbmState = TCBM_STATE_IDLE;
            break;

        case CMD_TALK:
        {
            deviceRole = DEVICE_ROLE_TALK;

            u8 command;
            if (!ReadTCBMCommandByteBlocking(command) || command != TCBM_CODE_SECOND)
                return;

            u8 secondary;
            if (!ReadTCBMDataByteBlocking(secondary))
                return;

            HandleTalkSecondary(secondary);
            break;
        }

        case CMD_UNTALK:
            deviceRole = DEVICE_ROLE_PASSIVE;
            tcbmState = TCBM_STATE_IDLE;
            break;

        default:
            PushDebugLine("Unhandled CMD byte $%02X", commandByte);
            break;
    }
}

void TCBM_Commands::HandleListenSecondary(u8 secondaryByte)
{
    u8 channel = secondaryByte & 0x0F;
    secondaryAddress = channel;
    activeChannel = channel;

    switch (secondaryByte & 0xF0)
    {
        case SEC_OPEN:
            EnterOpenState(channel);
            break;

        case SEC_CLOSE:
            if (channel == 15 && tcbmState == TCBM_STATE_OPEN && channels[15].cursor > 0)
                FinaliseOpenState(15);
            else
                CloseFile(channel);
            tcbmState = TCBM_STATE_IDLE;
            deviceRole = DEVICE_ROLE_PASSIVE;
            break;

        case SEC_SECOND:
            if (channel == 15)
            {
                EnterOpenState(15);
            }
            else if (channel == 1)
            {
                std::memcpy(channels[1].command, channels[0].command, sizeof(channels[1].command));
				PrepareSaveChannel(1);
            }
            else
            {
				PrepareLoadChannel(channel, false);
            }
            break;

        default:
            PushDebugLine("Unhandled LISTEN secondary $%02X", secondaryByte);
            break;
    }
}

void TCBM_Commands::HandleTalkSecondary(u8 secondaryByte)
{
    u8 channel = secondaryByte & 0x0F;
    secondaryAddress = channel;
    activeChannel = channel;

	bool fastMode = (secondaryByte & 0xF0) == SEC_FAST;
	switch (secondaryByte & 0xF0)
	{
		case SEC_FAST:
		case SEC_SECOND:
		{
			if (channel == 15)
			{
				PrepareStatusResponse(fastMode);
			}
			else
			{
				Channel& ch = channels[channel];
				if (fastMode && fastRequest.type != FAST_REQ_NONE)
				{
					PrepareLoadChannel(channel, true);
				}
				else if (ch.command[0] == '$' || (channel == 0 && ch.command[0] == '\0' && !ch.open))
				{
					PrepareDirectoryResponse(channel, fastMode);
				}
				else if (ch.command[0] != '\0' || ch.open)
				{
					PrepareLoadChannel(channel, fastMode);
				}
				else
				{
					PushDebugLine("TALK ch %u with no open file", channel);
					Error(ERROR_62_FILE_NOT_FOUND);
					tcbmState = TCBM_STATE_IDLE;
					deviceRole = DEVICE_ROLE_PASSIVE;
				}
			}
			break;
		}

		default:
			tcbmState = TCBM_STATE_IDLE;
			deviceRole = DEVICE_ROLE_PASSIVE;
			break;
	}
}

void TCBM_Commands::EnterOpenState(u8 channel)
{
    Channel& ch = channels[channel];
    ch.Close();
    ch.cursor = 0;
    ch.bytesSent = 0;
    std::memset(ch.buffer, 0, sizeof(ch.buffer));
    std::memset(ch.command, 0, sizeof(ch.command));
    tcbmState = TCBM_STATE_OPEN;
    activeChannel = channel;
    PushDebugLine("OPEN channel %u", channel);
}

void TCBM_Commands::FinaliseOpenState(u8 channel)
{
    Channel& ch = channels[channel];

    while (ch.cursor > 0 && ch.buffer[ch.cursor - 1] == 0x0D)
    {
        --ch.cursor;
    }

	if (channel == 15 && HandleU0Command(ch))
	{
		ch.cursor = 0;
		ch.bytesSent = 0;
		if (fastRequest.type != FAST_REQ_NONE)
			channels[0].command[0] = '\0';
		return;
	}

    size_t copyLen = std::min<size_t>(ch.cursor, sizeof(ch.command) - 1);
    std::memcpy(ch.command, ch.buffer, copyLen);
    ch.command[copyLen] = '\0';

    if (channel == 15)
    {
        channels[15].cursor = static_cast<u32>(copyLen);
        ProcessCommand();
        channels[15].cursor = 0;
    }

    ch.cursor = 0;
    ch.bytesSent = 0;
    PushDebugLine("OPEN text: %s", reinterpret_cast<const char*>(ch.command));
}

bool TCBM_Commands::PrepareLoadChannel(u8 channel, bool fastMode)
{
	secondaryAddress = channel;
	Channel& ch = channels[channel];

	if (fastMode && fastRequest.type != FAST_REQ_FILENAME
		&& (cbm_image_is_mounted() || mountedImagePath[0] != '\0'))
	{
		EnsureCbmImageModeFromMounted();
	}

	if (fastMode)
	{
		if (fastRequest.type == FAST_REQ_FILENAME)
		{
			ApplyPendingFastFilename(channel);

			if (cbm_image_is_mounted())
			{
				if (ch.open)
					ch.Close();
				CloseCbmImageChannel(channel);
				OpenCbmImageFile(channel);

				if (!ch.open)
				{
					PrepareFastLoadError(channel);
					return true;
				}

				fastRequest.type = FAST_REQ_NONE;
				ch.bytesSent = 0;
				statusActive = false;
				directoryActive = false;
				fastCtx.initialised = false;
				fastCtx.ackLevel = 1;
				fastCtx.expectedDav = 0;
				fastCtx.status = TCBM_STATUS_OK;
				tcbmState = TCBM_STATE_FASTLOAD;
				PushDebugLine("FAST LOAD image %s", reinterpret_cast<const char*>(ch.command));
				return true;
			}
		}
		else if (fastRequest.type == FAST_REQ_TRACK_SECTOR)
		{
			if (!cbm_image_is_mounted() && !EnsureCbmImageModeFromMounted())
			{
				PushDebugLine("FAST LOAD TS no image");
				Error(ERROR_74_DRlVE_NOT_READY);
				PrepareFastLoadError(channel);
				return true;
			}

			if (ch.open)
				ch.Close();
			CloseCbmImageChannel(channel);

			u32 fileSize = 0;
			if (!cbm_image_open_ts(channel, fastRequest.track, fastRequest.sector, fileSize))
			{
				Error(ERROR_62_FILE_NOT_FOUND);
				PrepareFastLoadError(channel);
				return true;
			}

			u8 track = fastRequest.track;
			u8 sector = fastRequest.sector;
			fastRequest.type = FAST_REQ_NONE;

			ch.open = true;
			ch.cursor = 0;
			ch.bytesSent = 0;
			ch.fileSize = fileSize;
			statusActive = false;
			directoryActive = false;
			fastCtx.initialised = false;
			fastCtx.ackLevel = 1;
			fastCtx.expectedDav = 0;
			fastCtx.status = TCBM_STATUS_OK;
			tcbmState = TCBM_STATE_FASTLOAD;
			PushDebugLine("FAST LOAD TS %u/%u", track, sector);
			return true;
		}
		else if (fastRequest.type == FAST_REQ_BLOCK_READ)
		{
			if (!PrepareFastBlockRead())
			{
				PrepareFastLoadError(channel);
				return true;
			}
			return true;
		}
		else if (fastRequest.type == FAST_REQ_BLOCK_WRITE)
		{
			if (!PrepareFastBlockWrite())
			{
				PrepareFastLoadError(channel);
				return true;
			}
			return true;
		}
	}

	if (ch.open)
		ch.Close();
	OpenFile();

	if (!ch.open)
	{
		Error(ERROR_62_FILE_NOT_FOUND);
		ch.cursor = 0;
		ch.bytesSent = 0;
		ch.fileSize = 0;
		statusActive = false;
		directoryActive = false;
		if (fastMode)
		{
			PrepareFastLoadError(channel);
			PushDebugLine("FAST LOAD error channel %u", channel);
			return true;
		}
		else
		{
			tcbmState = TCBM_STATE_IDLE;
			deviceRole = DEVICE_ROLE_PASSIVE;
			if (lastOpenPath[0] != '\0')
				PushDebugLine("LOAD fail: %s", lastOpenPath);
			else
				PushDebugLine("LOAD fail ch %u cmd:%s", channel, reinterpret_cast<const char*>(ch.command));
		}
		return false;
	}

	ch.bytesSent = 0;
	statusActive = false;
	directoryActive = false;
	if (ch.open)
	{
		if (IsCbmImageModeActive())
			ch.fileSize = GetCbmImageChannelFileSize(channel);
		else
		{
			ch.fileSize = static_cast<u32>(f_size(&ch.file));
			f_lseek(&ch.file, 0);
		}
	}
	if (fastMode)
	{
		fastCtx.initialised = false;
		fastCtx.ackLevel = 1;
		fastCtx.expectedDav = 0;
		fastCtx.status = TCBM_STATUS_OK;
		tcbmState = TCBM_STATE_FASTLOAD;
		fastRequest.type = FAST_REQ_NONE;
		PushDebugLine("FAST LOAD channel %u", channel);
	}
	else
	{
		tcbmState = TCBM_STATE_LOAD;
		PushDebugLine("LOAD channel %u", channel);
	}
	return true;
}

bool TCBM_Commands::PrepareSaveChannel(u8 channel)
{
    secondaryAddress = channel;
    Channel& ch = channels[channel];

    ch.cursor = 0;
    ch.bytesSent = 0;
    ch.writing = 1;

    OpenFile();
    if (!ch.open)
    {
		Error(ERROR_25_WRITE_ERROR);
		ch.cursor = 0;
		ch.bytesSent = 0;
		ch.fileSize = 0;
		statusActive = false;
		directoryActive = false;
		tcbmState = TCBM_STATE_SAVE;
		PushDebugLine("SAVE error channel %u", channel);
		return false;
    }

    tcbmState = TCBM_STATE_SAVE;
    statusActive = false;
    directoryActive = false;
    PushDebugLine("SAVE channel %u", channel);
    return true;
}

void TCBM_Commands::PrepareDirectoryResponse(u8 channel, bool fastMode)
{
    Channel& ch = channels[channel];
    ch.cursor = 0;
    ch.bytesSent = 0;
    captureOutput = true;
    captureChannel = channel;
    captureLength = 0;

    Commands_Base::LoadDirectory();

    captureOutput = false;

    size_t copyLength = std::min<size_t>(captureLength, sizeof(ch.buffer));
    if (captureLength > sizeof(ch.buffer))
    {
        PushDebugLine("DIR truncated (%u>%zu)", captureLength, sizeof(ch.buffer));
    }

    std::memcpy(ch.buffer, captureBuffer, copyLength);
    ch.cursor = static_cast<u32>(copyLength);
    ch.bytesSent = 0;
    ch.fileSize = ch.cursor;
    ch.filInfo.fsize = ch.cursor;
    directoryActive = true;
    statusActive = false;

    if (copyLength > 0)
    {
        size_t preview = std::min<size_t>(copyLength, static_cast<size_t>(32));
        char previewBuf[128] = { 0 };
        size_t offset = 0;
        for (size_t i = 0; i < preview && offset + 4 < sizeof(previewBuf); ++i)
        {
            offset += std::snprintf(previewBuf + offset, sizeof(previewBuf) - offset, "%02X ", ch.buffer[i]);
        }
        PushDebugLine("DIR hdr: %s", previewBuf);
    }
	if (fastMode)
	{
		fastCtx.initialised = false;
		fastCtx.ackLevel = 1;
		fastCtx.expectedDav = 0;
		fastCtx.status = TCBM_STATUS_OK;
		tcbmState = TCBM_STATE_FASTDIR;
		fastRequest.type = FAST_REQ_NONE;
		PushDebugLine("FAST DIR prepared channel %u bytes:%u", channel, ch.cursor);
	}
	else
	{
		tcbmState = TCBM_STATE_DIR;
		PushDebugLine("DIR prepared channel %u bytes:%u", channel, ch.cursor);
	}
}

void TCBM_Commands::PrepareStatusResponse(bool fastMode)
{
    Channel& ch = channels[15];
    std::size_t len = std::strlen(errorMessage);
    len = std::min(len, sizeof(ch.buffer) - 1);
    std::memcpy(ch.buffer, errorMessage, len);
    ch.buffer[len] = '\0';
    ch.cursor = static_cast<u32>(len);
    ch.bytesSent = 0;
    statusActive = true;
    directoryActive = false;
	if (fastMode)
	{
		fastCtx.initialised = false;
		fastCtx.ackLevel = 1;
		fastCtx.expectedDav = 0;
		fastCtx.status = TCBM_STATUS_OK;
		tcbmState = TCBM_STATE_FASTDIR;
		fastRequest.type = FAST_REQ_NONE;
		PushDebugLine("FAST STATUS prepared (len %u)", ch.cursor);
	}
	else
	{
		tcbmState = TCBM_STATE_DIR;
		PushDebugLine("STATUS prepared (len %u)", ch.cursor);
	}
}

void TCBM_Commands::PrepareBrowseIdleBus()

{
	TCBM_Bus::SetDataInput();
	TCBM_Bus::ReadBrowseMode();
	TCBM_Bus::SetStatus(TCBM_STATUS_OK);
	TCBM_Bus::AssertACK();
	TCBM_Bus::ForceDevFromDeviceId(deviceID);
	SetDebugLine(0, "TCBM idle (%s)", RoleToString(deviceRole));
}

void TCBM_Commands::AppendCommandByte(Channel& channel, u8 byte)
{
    if (channel.cursor < sizeof(channel.buffer))
        channel.buffer[channel.cursor++] = byte;
}

bool TCBM_Commands::HandleU0Command(Channel& channel)
{
	if (channel.cursor < 2)
		return false;

	const u8* data = channel.buffer;
	size_t length = channel.cursor;

	if (data[0] != 'U' || data[1] != '0')
		return false;

	if (length >= 4 && data[2] == '>' && (data[3] == 8 || data[3] == 9))
	{
		u8 id = data[3];
		SetDeviceId(id);
		Error(ERROR_00_OK);
		fastRequest.type = FAST_REQ_NONE;
		PushDebugLine("U0> set device %u", id);
		return true;
	}

	if (length < 3)
	{
		Error(ERROR_30_SYNTAX_ERROR);
		fastRequest.type = FAST_REQ_NONE;
		return true;
	}

	u8 mode = data[2] & 0x3F;
	switch (mode)
	{
		case 0x1F:
		{
			bool ok = ExtractU0Filename(data + 3, length - 3);
			if (ok)
			{
				fastRequest.type = FAST_REQ_FILENAME;
				Error(ERROR_00_OK);
				PushDebugLine("U0 fastload filename %s", fastRequest.filename);
			}
			else
			{
				fastRequest.type = FAST_REQ_NONE;
				Error(ERROR_30_SYNTAX_ERROR);
			}
			return true;
		}

		case 0x3F:
		{
			if (length < 5)
			{
				Error(ERROR_30_SYNTAX_ERROR);
				fastRequest.type = FAST_REQ_NONE;
				return true;
			}
			if (!cbm_image_is_mounted() && mountedImagePath[0] == '\0')
			{
				Error(ERROR_30_SYNTAX_ERROR);
				fastRequest.type = FAST_REQ_NONE;
				return true;
			}
			fastRequest.type = FAST_REQ_TRACK_SECTOR;
			fastRequest.track = data[3];
			fastRequest.sector = data[4];
			Error(ERROR_00_OK);
			PushDebugLine("U0 fastload T/S %u/%u", fastRequest.track, fastRequest.sector);
			return true;
		}

		case 0x00:
		{
			if (length < 6)
			{
				Error(ERROR_30_SYNTAX_ERROR);
				fastRequest.type = FAST_REQ_NONE;
				return true;
			}
			if (!cbm_image_is_mounted() && mountedImagePath[0] == '\0')
			{
				Error(ERROR_30_SYNTAX_ERROR);
				fastRequest.type = FAST_REQ_NONE;
				return true;
			}
			fastRequest.type = FAST_REQ_BLOCK_READ;
			fastRequest.track = data[3];
			fastRequest.sector = data[4];
			fastRequest.blockCount = data[5];
			Error(ERROR_00_OK);
			PushDebugLine("U0 fast block read %u/%u count %u", fastRequest.track, fastRequest.sector, fastRequest.blockCount);
			return true;
		}

		case 0x02:
		{
			if (length < 6)
			{
				Error(ERROR_30_SYNTAX_ERROR);
				fastRequest.type = FAST_REQ_NONE;
				return true;
			}
			if (!cbm_image_is_mounted() && mountedImagePath[0] == '\0')
			{
				Error(ERROR_30_SYNTAX_ERROR);
				fastRequest.type = FAST_REQ_NONE;
				return true;
			}
			fastRequest.type = FAST_REQ_BLOCK_WRITE;
			fastRequest.track = data[3];
			fastRequest.sector = data[4];
			fastRequest.blockCount = data[5];
			Error(ERROR_00_OK);
			PushDebugLine("U0 fast block write %u/%u count %u", fastRequest.track, fastRequest.sector, fastRequest.blockCount);
			return true;
		}

		default:
			fastRequest.type = FAST_REQ_NONE;
			Error(ERROR_31_SYNTAX_ERROR);
			return true;
	}
}

bool TCBM_Commands::ExtractU0Filename(const u8* data, size_t length)
{
	fastRequest.filename[0] = '\0';
	if (length == 0)
		return false;

	// Skip optional drive prefix 0:
	size_t in = 0;
	if (length >= 2 && data[0] == '0' && data[1] == ':')
		in = 2;
	else if (data[0] == ':')
		in = 1;

	size_t out = 0;
	for (; in < length && out + 1 < FAST_FILENAME_MAX; ++in)
	{
		u8 value = data[in];
		if (value == 0 || value == 0x0D)
			break;
		fastRequest.filename[out++] = static_cast<char>(petscii2ascii(value));
	}
	fastRequest.filename[out] = '\0';
	return out > 0;
}

void TCBM_Commands::ApplyPendingFastFilename(u8 channel)
{
	if (fastRequest.type != FAST_REQ_FILENAME)
		return;

	Channel& ch = channels[channel];
	ch.Close();
	std::memset(ch.command, 0, sizeof(ch.command));

	if (fastRequest.filename[0] == '\0')
		return;

	std::snprintf(reinterpret_cast<char*>(ch.command), sizeof(ch.command), "0:%s", fastRequest.filename);
	ch.open = false;
}

void TCBM_Commands::PrepareFastLoadError(u8 channel)
{
	Channel& ch = channels[channel];
	ch.open = false;
	ch.cursor = 0;
	ch.bytesSent = 0;
	ch.fileSize = 0;
	statusActive = false;
	directoryActive = false;
	fastCtx.initialised = false;
	fastCtx.ackLevel = 1;
	fastCtx.expectedDav = 0;
	fastCtx.status = TCBM_STATUS_SEND;
	tcbmState = TCBM_STATE_FASTLOAD;
	fastRequest.type = FAST_REQ_NONE;
}

void TCBM_Commands::ServiceOpenState()
{
    while (tcbmState == TCBM_STATE_OPEN)
    {
        if (TCBM_Bus::IsReset())
        {
            ResetStateMachine();
            PrepareBrowseIdleBus();
            return;
        }

        u8 command = ReadTCBMCommandByte();
        if (command == 0)
        {
            if (!ReadTCBMCommandByteBlocking(command))
                return;
        }

        switch (command)
        {
            case TCBM_CODE_RECV:
            {
                u8 byte;
                if (ReadTCBMDataByteBlocking(byte))
                    AppendCommandByte(channels[activeChannel], byte);
                break;
            }

            case TCBM_CODE_COMMAND:
            {
                u8 data;
                if (!ReadTCBMDataByteBlocking(data))
                    return;

                if (data == CMD_UNLISTEN)
                {
                    FinaliseOpenState(activeChannel);
                    tcbmState = TCBM_STATE_IDLE;
                    deviceRole = DEVICE_ROLE_PASSIVE;
                    return;
                }
                break;
            }

            default:
                return;
        }
    }
}

void TCBM_Commands::ServiceLoadState()
{
    Channel& ch = channels[secondaryAddress];

    while (tcbmState == TCBM_STATE_LOAD)
    {
        if (TCBM_Bus::IsReset())
        {
            ResetStateMachine();
            PrepareBrowseIdleBus();
            return;
        }

        u8 command = ReadTCBMCommandByte();
        if (command == 0)
        {
            if (!ReadTCBMCommandByteBlocking(command))
                return;
        }

        switch (command)
        {
            case TCBM_CODE_SEND:
            {
                if (!ch.open)
                {
                    WriteSerialPortByteWithStatus(0x0D, TCBM_STATUS_SEND);
                    tcbmState = TCBM_STATE_IDLE;
                    deviceRole = DEVICE_ROLE_PASSIVE;
                    return;
                }

                u8 byte = 0;
                u32 bytesRead = 0;
                if (IsCbmImageModeActive())
                {
                    if (!ReadCbmImageChannelByte(secondaryAddress, byte))
                    {
                        WriteSerialPortByte(0x0D, true);
                        CloseCbmImageChannel(secondaryAddress);
                        ch.open = false;
                        tcbmState = TCBM_STATE_IDLE;
                        deviceRole = DEVICE_ROLE_PASSIVE;
                        return;
                    }
                    bytesRead = 1;
                }
                else
                {
                    FRESULT res = f_read(&ch.file, &byte, 1, &bytesRead);
                    if (res != FR_OK)
                    {
                        WriteSerialPortByteWithStatus(0x0D, TCBM_STATUS_SEND);
                        Error(ERROR_74_DRlVE_NOT_READY);
                        ch.Close();
                        tcbmState = TCBM_STATE_IDLE;
                        deviceRole = DEVICE_ROLE_PASSIVE;
                        return;
                    }
                }

                if (bytesRead == 0)
                {
                    WriteSerialPortByte(0x0D, true);
                    if (IsCbmImageModeActive())
                        CloseCbmImageChannel(secondaryAddress);
                    ch.Close();
                    tcbmState = TCBM_STATE_IDLE;
                    deviceRole = DEVICE_ROLE_PASSIVE;
                    return;
                }

                bool eoi = false;
                if (IsCbmImageModeActive())
                {
                    u32 size = GetCbmImageChannelFileSize(secondaryAddress);
                    u32 pos = GetCbmImageChannelPosition(secondaryAddress);
                    if (size != 0xFFFFFFFF)
                        eoi = (size > 0 && pos >= size);
                }
                else
                {
                    eoi = (f_tell(&ch.file) >= ch.fileSize);
                }
                WriteSerialPortByte(byte, eoi);
                if (eoi)
                {
                    if (IsCbmImageModeActive())
                        CloseCbmImageChannel(secondaryAddress);
                    ch.Close();
                    tcbmState = TCBM_STATE_IDLE;
                    deviceRole = DEVICE_ROLE_PASSIVE;
                    return;
                }
                break;
            }

            case TCBM_CODE_COMMAND:
            {
                u8 data;
                if (!ReadTCBMDataByteBlocking(data))
                    return;

                if (data == CMD_UNTALK)
                {
                    if (ch.open)
                    {
                        if (IsCbmImageModeActive())
                            CloseCbmImageChannel(secondaryAddress);
                        ch.Close();
                    }
                    tcbmState = TCBM_STATE_IDLE;
                    deviceRole = DEVICE_ROLE_PASSIVE;
                    return;
                }
                break;
            }

            default:
                return;
        }
    }
}

void TCBM_Commands::ServiceSaveState()
{
    Channel& ch = channels[secondaryAddress];

    u8 command;
    if (!ReadTCBMCommandByteBlocking(command))
    {
        AbortBlockingTransfer("SAVE command");
        return;
    }

    if (command == TCBM_CODE_RECV)
    {
		if (!ch.open)
		{
			u8 discard;
			if (!ReadTCBMDataByteBlocking(discard, TCBM_STATUS_RECV))
			{
				AbortBlockingTransfer("SAVE rejected");
				return;
			}
			PushDebugLine("SAVE rejected byte");
			tcbmState = TCBM_STATE_IDLE;
			deviceRole = DEVICE_ROLE_PASSIVE;
			return;
		}

        u8 byte;
        if (!ReadTCBMDataByteBlocking(byte))
        {
            AbortBlockingTransfer("SAVE data");
            return;
        }

        if (ch.cursor < sizeof(ch.buffer))
            ch.buffer[ch.cursor++] = byte;

        if (ch.WriteFull())
        {
            u32 written = 0;
            f_write(&ch.file, ch.buffer, sizeof(ch.buffer), &written);
            ch.cursor = 0;
        }
    }
    else if (command == TCBM_CODE_COMMAND)
    {
        u8 data;
        if (!ReadTCBMDataByteBlocking(data))
        {
            AbortBlockingTransfer("SAVE UNLISTEN");
            return;
        }

        if (data == CMD_UNLISTEN)
        {
            if (ch.cursor > 0)
            {
                u32 written = 0;
                f_write(&ch.file, ch.buffer, ch.cursor, &written);
                ch.cursor = 0;
            }
            ch.Close();
            tcbmState = TCBM_STATE_IDLE;
            deviceRole = DEVICE_ROLE_PASSIVE;
        }
    }
}

void TCBM_Commands::ServiceDirectoryState()
{
    Channel& ch = channels[secondaryAddress];

    u8 command;
    if (!ReadTCBMCommandByteBlocking(command))
    {
        AbortBlockingTransfer("DIR command");
        return;
    }

    if (command == TCBM_CODE_SEND)
    {
        if (ch.bytesSent < ch.cursor)
        {
            bool eoi = (ch.bytesSent == ch.cursor - 1);
            WriteSerialPortByte(ch.buffer[ch.bytesSent++], eoi);
            if (eoi)
            {
                tcbmState = TCBM_STATE_IDLE;
                deviceRole = DEVICE_ROLE_PASSIVE;
            }
        }
        else
        {
            WriteSerialPortByte(0x0D, true);
            tcbmState = TCBM_STATE_IDLE;
            deviceRole = DEVICE_ROLE_PASSIVE;
        }
    }
    else if (command == TCBM_CODE_COMMAND)
    {
        u8 data;
        if (!ReadTCBMDataByteBlocking(data))
        {
            AbortBlockingTransfer("DIR UNTALK");
            return;
        }

        if (data == CMD_UNTALK)
        {
            tcbmState = TCBM_STATE_IDLE;
            deviceRole = DEVICE_ROLE_PASSIVE;
        }
    }
}

bool TCBM_Commands::InitialiseFastHandshake(const char* stage)
{
	if (fastCtx.initialised)
		return true;

	fastCtx.ackLevel = 1;
	fastCtx.expectedDav = 0;
	fastCtx.status = TCBM_STATUS_OK;

	TCBM_Bus::SetDataInput();
	TCBM_Bus::SetStatus(TCBM_STATUS_OK);
	TCBM_Bus::AssertACK();

	if (!WaitForDAVState(false, stage))
		return false;

	fastCtx.initialised = true;
	return true;
}

bool TCBM_Commands::FastSendByte(u8 data)
{
	TCBM_Bus::SetData(data);
	TCBM_Bus::SetStatus(fastCtx.status);
	fastCtx.ackLevel ^= 1;
	if (fastCtx.ackLevel)
		TCBM_Bus::AssertACK();
	else
		TCBM_Bus::ReleaseACK();

	fastCtx.expectedDav ^= 1;
	return WaitForDAVState(fastCtx.expectedDav != 0, "FAST DAV", COMMAND_TIMEOUT_US);
}

bool TCBM_Commands::FastSendBlockByte(u8 data)
{
	return FastSendByte(data);
}

bool TCBM_Commands::FastReceiveBlockByte(u8& data)
{
	fastCtx.ackLevel ^= 1;
	if (fastCtx.ackLevel)
		TCBM_Bus::AssertACK();
	else
		TCBM_Bus::ReleaseACK();

	fastCtx.expectedDav ^= 1;
	if (!WaitForDAVState(fastCtx.expectedDav != 0, "FAST block write DAV", COMMAND_TIMEOUT_US))
		return false;

	TCBM_Bus::ReadBrowseMode();
	data = TCBM_Bus::GetPI_Data();
	TCBM_Bus::SetStatus(fastCtx.status);
	if (fastCtx.ackLevel)
		TCBM_Bus::AssertACK();
	else
		TCBM_Bus::ReleaseACK();
	return true;
}

bool TCBM_Commands::FinaliseFastHandshake()
{
	if (!fastCtx.initialised)
		return true;

	TCBM_Bus::SetStatus(fastCtx.status);
	TCBM_Bus::SetDataInput();
	TCBM_Bus::AssertACK();
	bool ok = WaitForDAVState(true, "FAST final DAV=1", COMMAND_TIMEOUT_US);
	TCBM_Bus::SetStatus(TCBM_STATUS_OK);
	fastCtx.initialised = false;
	fastCtx.status = TCBM_STATUS_OK;
	fastCtx.ackLevel = 1;
	fastCtx.expectedDav = 0;
	return ok;
}

bool TCBM_Commands::LoadFastByte(u8& data, bool& eoi)
{
	Channel& ch = channels[secondaryAddress];
	eoi = false;

	if (!ch.open)
	{
		fastCtx.status = TCBM_STATUS_SEND;
		return false;
	}

	if (IsCbmImageModeActive())
	{
		if (!ReadCbmImageChannelByte(secondaryAddress, data))
		{
			fastCtx.status = TCBM_STATUS_EOI;
			return false;
		}
		u32 size = GetCbmImageChannelFileSize(secondaryAddress);
		u32 pos = GetCbmImageChannelPosition(secondaryAddress);
		if (size != 0xFFFFFFFF)
			eoi = (size > 0 && pos >= size);
		fastCtx.status = eoi ? TCBM_STATUS_EOI : TCBM_STATUS_OK;
		return true;
	}

	u32 bytesRead = 0;
	FRESULT res = f_read(&ch.file, &data, 1, &bytesRead);
	if (res != FR_OK)
	{
		fastCtx.status = TCBM_STATUS_SEND;
		return false;
	}

	if (bytesRead == 0)
	{
		fastCtx.status = TCBM_STATUS_EOI;
		return false;
	}

	eoi = (f_tell(&ch.file) >= ch.fileSize);
	fastCtx.status = eoi ? TCBM_STATUS_EOI : TCBM_STATUS_OK;
	return true;
}

bool TCBM_Commands::LoadFastDirectoryByte(u8& data, bool& eoi)
{
	Channel& ch = channels[secondaryAddress];
	eoi = false;

	if (ch.bytesSent < ch.cursor)
	{
		data = ch.buffer[ch.bytesSent++];
		eoi = (ch.bytesSent >= ch.cursor);
		fastCtx.status = eoi ? TCBM_STATUS_EOI : TCBM_STATUS_OK;
		return true;
	}

	fastCtx.status = TCBM_STATUS_EOI;
	return false;
}

void TCBM_Commands::ServiceFastLoadState()
{
	if (!InitialiseFastHandshake("FAST load init"))
	{
		fastCtx.status = TCBM_STATUS_SEND;
		Error(ERROR_74_DRlVE_NOT_READY);
		FinaliseFastHandshake();
		tcbmState = TCBM_STATE_IDLE;
		deviceRole = DEVICE_ROLE_PASSIVE;
		return;
	}

	while (true)
	{
		if (TCBM_Bus::IsReset())
		{
			AbortBlockingTransfer("FAST load reset");
			return;
		}

		if (fastCtx.status != TCBM_STATUS_OK)
		{
			if (FinaliseFastHandshake())
			{
				tcbmState = TCBM_STATE_IDLE;
				deviceRole = DEVICE_ROLE_PASSIVE;
			}
			else
			{
				NoteTimeout("FAST load finalise");
				tcbmState = TCBM_STATE_IDLE;
				deviceRole = DEVICE_ROLE_PASSIVE;
			}
			return;
		}

		u8 data = 0;
		bool eoi = false;
		if (!LoadFastByte(data, eoi))
		{
			if (fastCtx.status == TCBM_STATUS_OK)
				fastCtx.status = TCBM_STATUS_EOI;
			continue;
		}

		if (!FastSendByte(data))
		{
			NoteTimeout("FAST load byte");
			fastCtx.status = TCBM_STATUS_SEND;
			Error(ERROR_74_DRlVE_NOT_READY);
			FinaliseFastHandshake();
			tcbmState = TCBM_STATE_IDLE;
			deviceRole = DEVICE_ROLE_PASSIVE;
			return;
		}

		if (fastCtx.status == TCBM_STATUS_EOI)
		{
			// Next iteration will finalise handshake
			continue;
		}
	}
}

void TCBM_Commands::ServiceFastDirectoryState()
{
	if (!InitialiseFastHandshake("FAST dir init"))
	{
		fastCtx.status = TCBM_STATUS_SEND;
		Error(ERROR_74_DRlVE_NOT_READY);
		FinaliseFastHandshake();
		tcbmState = TCBM_STATE_IDLE;
		deviceRole = DEVICE_ROLE_PASSIVE;
		return;
	}

	while (true)
	{
		if (TCBM_Bus::IsReset())
		{
			AbortBlockingTransfer("FAST load reset");
			return;
		}

		if (fastCtx.status != TCBM_STATUS_OK)
		{
			if (FinaliseFastHandshake())
			{
				tcbmState = TCBM_STATE_IDLE;
				deviceRole = DEVICE_ROLE_PASSIVE;
			}
			else
			{
				NoteTimeout("FAST dir finalise");
				tcbmState = TCBM_STATE_IDLE;
				deviceRole = DEVICE_ROLE_PASSIVE;
			}
			return;
		}

		u8 data = 0;
		bool eoi = false;
		if (!LoadFastDirectoryByte(data, eoi))
		{
			if (fastCtx.status == TCBM_STATUS_OK)
				fastCtx.status = TCBM_STATUS_EOI;
			continue;
		}

		if (!FastSendByte(data))
		{
			NoteTimeout("FAST dir byte");
			fastCtx.status = TCBM_STATUS_SEND;
			Error(ERROR_74_DRlVE_NOT_READY);
			FinaliseFastHandshake();
			tcbmState = TCBM_STATE_IDLE;
			deviceRole = DEVICE_ROLE_PASSIVE;
			return;
		}

		if (fastCtx.status == TCBM_STATUS_EOI)
			continue;
	}
}

void TCBM_Commands::ServiceFastBlockReadState()
{
	if (!InitialiseFastHandshake("FAST block read init"))
	{
		fastCtx.status = TCBM_STATUS_SEND;
		Error(ERROR_74_DRlVE_NOT_READY);
		cbm_image_block_io_end();
		FinaliseFastHandshake();
		tcbmState = TCBM_STATE_IDLE;
		deviceRole = DEVICE_ROLE_PASSIVE;
		return;
	}

	while (true)
	{
		if (TCBM_Bus::IsReset())
		{
			cbm_image_block_io_end();
			AbortBlockingTransfer("FAST block read reset");
			return;
		}

		if (fastCtx.status != TCBM_STATUS_OK)
		{
			cbm_image_block_io_end();
			if (FinaliseFastHandshake())
			{
				tcbmState = TCBM_STATE_IDLE;
				deviceRole = DEVICE_ROLE_PASSIVE;
			}
			else
			{
				NoteTimeout("FAST block read finalise");
				tcbmState = TCBM_STATE_IDLE;
				deviceRole = DEVICE_ROLE_PASSIVE;
			}
			return;
		}

		u8 data = 0;
		if (!cbm_image_block_io_read_byte(data))
		{
			fastCtx.status = TCBM_STATUS_SEND;
			Error(ERROR_74_DRlVE_NOT_READY);
			continue;
		}

		if (blockIoBytesRemaining > 0)
			--blockIoBytesRemaining;
		if (blockIoBytesRemaining == 0)
			fastCtx.status = TCBM_STATUS_EOI;

		if (!FastSendBlockByte(data))
		{
			NoteTimeout("FAST block read byte");
			fastCtx.status = TCBM_STATUS_SEND;
			Error(ERROR_74_DRlVE_NOT_READY);
			cbm_image_block_io_end();
			FinaliseFastHandshake();
			tcbmState = TCBM_STATE_IDLE;
			deviceRole = DEVICE_ROLE_PASSIVE;
			return;
		}

		if (fastCtx.status == TCBM_STATUS_EOI)
			continue;
	}
}

void TCBM_Commands::ServiceFastBlockWriteState()
{
	if (!fastCtx.initialised)
	{
		fastCtx.ackLevel = 1;
		fastCtx.expectedDav = 1;
		fastCtx.status = TCBM_STATUS_OK;
		TCBM_Bus::SetDataInput();
		TCBM_Bus::SetStatus(TCBM_STATUS_OK);
		TCBM_Bus::AssertACK();
		fastCtx.initialised = true;
	}

	while (true)
	{
		if (TCBM_Bus::IsReset())
		{
			cbm_image_block_io_end();
			AbortBlockingTransfer("FAST block write reset");
			return;
		}

		if (fastCtx.status != TCBM_STATUS_OK)
		{
			cbm_image_block_io_end();
			if (FinaliseFastHandshake())
			{
				tcbmState = TCBM_STATE_IDLE;
				deviceRole = DEVICE_ROLE_PASSIVE;
			}
			else
			{
				NoteTimeout("FAST block write finalise");
				tcbmState = TCBM_STATE_IDLE;
				deviceRole = DEVICE_ROLE_PASSIVE;
			}
			return;
		}

		u8 data = 0;
		if (!FastReceiveBlockByte(data))
		{
			NoteTimeout("FAST block write byte");
			fastCtx.status = TCBM_STATUS_SEND;
			Error(ERROR_74_DRlVE_NOT_READY);
			cbm_image_block_io_end();
			FinaliseFastHandshake();
			tcbmState = TCBM_STATE_IDLE;
			deviceRole = DEVICE_ROLE_PASSIVE;
			return;
		}

		if (!cbm_image_block_io_write_byte(data))
		{
			fastCtx.status = TCBM_STATUS_SEND;
			Error(ERROR_74_DRlVE_NOT_READY);
			continue;
		}

		if (blockIoBytesRemaining > 0)
			--blockIoBytesRemaining;
		if (blockIoBytesRemaining == 0)
			fastCtx.status = TCBM_STATUS_EOI;
	}
}

bool TCBM_Commands::PrepareFastBlockRead()
{
	if (!cbm_image_is_mounted() && !EnsureCbmImageModeFromMounted())
	{
		Error(ERROR_74_DRlVE_NOT_READY);
		fastRequest.type = FAST_REQ_NONE;
		return false;
	}

	u32 bytes = 0;
	if (!cbm_image_block_io_begin(fastRequest.track, fastRequest.sector, fastRequest.blockCount, bytes, false))
	{
		Error(ERROR_74_DRlVE_NOT_READY);
		fastRequest.type = FAST_REQ_NONE;
		return false;
	}

	u8 track = fastRequest.track;
	u8 sector = fastRequest.sector;
	u8 count = fastRequest.blockCount;
	blockIoBytesRemaining = bytes;
	fastRequest.type = FAST_REQ_NONE;

	fastCtx.initialised = false;
	fastCtx.ackLevel = 1;
	fastCtx.expectedDav = 0;
	fastCtx.status = TCBM_STATUS_OK;
	tcbmState = TCBM_STATE_FAST_BLOCKREAD;
	PushDebugLine("FAST BLOCK READ %u/%u x%u (%u bytes)", track, sector, count, bytes);
	return true;
}

bool TCBM_Commands::PrepareFastBlockWrite()
{
	if (!cbm_image_is_mounted() && !EnsureCbmImageModeFromMounted())
	{
		Error(ERROR_74_DRlVE_NOT_READY);
		fastRequest.type = FAST_REQ_NONE;
		return false;
	}

	u32 bytes = 0;
	if (!cbm_image_block_io_begin(fastRequest.track, fastRequest.sector, fastRequest.blockCount, bytes, true))
	{
		Error(ERROR_74_DRlVE_NOT_READY);
		fastRequest.type = FAST_REQ_NONE;
		return false;
	}

	u8 track = fastRequest.track;
	u8 sector = fastRequest.sector;
	u8 count = fastRequest.blockCount;
	blockIoBytesRemaining = bytes;
	fastRequest.type = FAST_REQ_NONE;

	fastCtx.initialised = false;
	fastCtx.ackLevel = 1;
	fastCtx.expectedDav = 1;
	fastCtx.status = TCBM_STATUS_OK;
	tcbmState = TCBM_STATE_FAST_BLOCKWRITE;
	PushDebugLine("FAST BLOCK WRITE %u/%u x%u (%u bytes)", track, sector, count, bytes);
	return true;
}

bool TCBM_Commands::WriteSerialPortByte(u8 data, bool eoi)
{
    return WriteSerialPortByteWithStatus(data, eoi ? TCBM_STATUS_EOI : TCBM_STATUS_OK);
}

bool TCBM_Commands::WriteSerialPortByteWithStatus(u8 data, u8 status)
{
    if (captureOutput)
    {
        if (captureLength < sizeof(captureBuffer))
            captureBuffer[captureLength++] = data;
        return false;
    }

    debugWriteStep = 1;
    std::snprintf(debugWriteBuffer, sizeof(debugWriteBuffer), "$%02X st=$%02X", data, status);

    if (!WaitForDAVState(false, "DAV=0 (host ready)"))
        return true;

    TCBM_Bus::SetData(data);
    TCBM_Bus::SetStatus(status);
    TCBM_Bus::AssertACK();

    debugWriteStep = 2;
    std::snprintf(debugWriteBuffer, sizeof(debugWriteBuffer), "DATA=$%02X ACK=1", data);

    if (!WaitForDAVState(true, "DAV=1 (host received)"))
        return true;

    TCBM_Bus::SetDataInput();
    TCBM_Bus::SetStatus(TCBM_STATUS_OK);
    TCBM_Bus::ReleaseACK();

    debugWriteStep = 3;
    std::snprintf(debugWriteBuffer, sizeof(debugWriteBuffer), "ACK=0 return");

    if (!WaitForDAVState(false, "DAV=0 (host next)"))
        return true;

    TCBM_Bus::AssertACK();
    if (!WaitForDAVState(true, "DAV=1 (final)"))
        return true;

    debugWriteStep = 4;
    std::snprintf(debugWriteBuffer, sizeof(debugWriteBuffer), "COMPLETE");

    return false;
}

void TCBM_Commands::LoadFile()
{
    ServiceLoadState();
}

void TCBM_Commands::SaveFile()
{
    ServiceSaveState();
}

void TCBM_Commands::LoadDirectory()
{
    ServiceDirectoryState();
}

void TCBM_Commands::RequestPopDir()
{
    // Defer POP_DIR so SimulateIECUpdate can return it once we're back in browse mode
    pendingPopDir = true;
    // Cancel any pending image selection so we don't auto-mount again
    selectedImageName[0] = 0;
    filInfoSelectedImage.fname[0] = 0;
}

bool TCBM_Commands::InterceptEmulationU0Command(const u8* data, size_t length)
{
	if (length < 2 || data[0] != 'U' || data[1] != '0')
		return false;

	Channel& ch = channels[15];
	ch.cursor = 0;
	size_t copyLen = std::min(length, sizeof(ch.buffer));
	std::memcpy(ch.buffer, data, copyLen);
	ch.cursor = static_cast<u32>(copyLen);
	if (HandleU0Command(ch))
	{
		if (fastRequest.type != FAST_REQ_NONE)
			channels[0].command[0] = '\0';
	}
	return true;
}

void TCBM_Commands::HandleEmulationFastTalkHandoff(u8 channel)
{
	EnsureCbmImageModeFromMounted();
	secondaryAddress = channel;
	const bool fastMode = true;

	if (channel == 15)
	{
		PrepareStatusResponse(fastMode);
	}
	else
	{
		Channel& ch = channels[channel];
		if (fastMode && fastRequest.type != FAST_REQ_NONE)
		{
			PrepareLoadChannel(channel, true);
		}
		else if (ch.command[0] == '$' || (channel == 0 && ch.command[0] == '\0'))
		{
			if (ch.command[0] != '$')
			{
				ch.command[0] = '$';
				ch.command[1] = '\0';
			}
			PrepareDirectoryResponse(channel, fastMode);
		}
		else
		{
			PrepareLoadChannel(channel, fastMode);
		}
	}
}

void TCBM_Commands::RestoreAfterEmulationFastHandoff()
{
	if (mountedImagePath[0] != '\0' && PathUsesSectorEmulation(mountedImagePath))
		DeactivateCbmImageMode();
}

void TCBM_Commands::RunBrowserModeTransferUntilIdle()
{
	m6523* savedTpi = TCBM_Bus::TPI;
	TCBM_Bus::TPI = 0;
	PrepareBrowseIdleBus();

	while (IsTransferActive())
	{
		if (TCBM_Bus::IsReset())
		{
			AbortBlockingTransfer("FAST handoff reset");
			break;
		}
		SimulateIECUpdate();
	}

	RestoreAfterEmulationFastHandoff();

	TCBM_Bus::TPI = savedTpi;
	if (savedTpi)
		TCBM_Bus::port = savedTpi->GetPortA();
	TCBM_Bus::RefreshOuts1551();
}
