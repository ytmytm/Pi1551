// Pi1551 - TCBM browser-mode command handler
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

#ifndef TCBM_COMMANDS_H
#define TCBM_COMMANDS_H

#include "commands_base.h"
#include "tcbm_bus.h"

// TCBM Bus Command Codes (controller -> device), see pagetable article and tcbm2sd reference.
#define TCBM_CODE_COMMAND 0x81
#define TCBM_CODE_SECOND  0x82
#define TCBM_CODE_RECV    0x83
#define TCBM_CODE_SEND    0x84

	// TCBM Status codes (device -> controller)
	static constexpr u8 TCBM_STATUS_OK   = 0x00;
	static constexpr u8 TCBM_STATUS_RECV = 0x01;
	static constexpr u8 TCBM_STATUS_SEND = 0x02;
	static constexpr u8 TCBM_STATUS_EOI  = 0x03;

class TCBM_Commands : public Commands_Base
{
public:
	TCBM_Commands();
	void Initialise();

	void Reset(void);
	void SimulateIECBegin(void);
	UpdateAction SimulateIECUpdate(void);

	// TCBM-specific state machine (not using ATN sequences)
		enum TCBMState
	{
		TCBM_STATE_IDLE,		  // Waiting for command byte
		TCBM_STATE_OPEN,		  // Receiving filename/command on channel 15
		TCBM_STATE_LOAD,		  // Sending data to computer (standard handshake)
		TCBM_STATE_SAVE,		  // Receiving data from computer (standard handshake)
		TCBM_STATE_DIR,		  // Sending directory listing / status (standard handshake)
		TCBM_STATE_FASTLOAD,	  // Sending data using fast handshake
		TCBM_STATE_FASTDIR,	  // Sending directory/status using fast handshake
		TCBM_STATE_FAST_BLOCKREAD,	// Fast transfer of raw blocks from disk image
		TCBM_STATE_FAST_BLOCKWRITE	// Fast transfer of raw blocks to disk image
	};

    // Diagnostic accessors
    TCBMState GetState() const { return tcbmState; }
    bool IsTransferActive() const { return tcbmState != TCBM_STATE_IDLE; }
    const char* GetStateName() const;

    // Debug overlay (rendered on HDMI screen)
    static constexpr int DEBUG_LINE_CAPACITY = 24;
    static constexpr int DEBUG_LINE_LENGTH  = 96;
    const char* const* GetDebugLines(int& lineCount) const;

    // Quick handshake state hint for overlay
    static int  debugWriteStep;
    static char debugWriteBuffer[64];

	// Emulation-mode traps (1551 ROM) delegate fast transfers to browser handler
	bool InterceptEmulationU0Command(const u8* data, size_t length);
	void HandleEmulationFastTalkHandoff(u8 channel);
	void RunBrowserModeTransferUntilIdle();
	void RestoreAfterEmulationFastHandoff();

	// Allow traps to request a directory pop without exposing CD()
	void RequestPopDir();

protected:
	bool WriteSerialPortByte(u8 data, bool eoi) override;
	bool WriteSerialPortByteWithStatus(u8 data, u8 status);
	void LoadFile() override;
	void SaveFile() override;
	void LoadDirectory() override;

    // ReadSerialPortByte not used by TCBM (uses dedicated helpers)
    void ReadBrowseMode() { TCBM_Bus::ReadBrowseMode(); }
    bool IsReset() { return TCBM_Bus::IsReset(); }

    // Helper functions to read TCBM command/data bytes
    u8   ReadTCBMCommandByte();                  // Non-blocking poll, returns 0 if no command
	bool ReadTCBMCommandByteBlocking(u8& value); // Blocking with timeout (5s)
	bool ReadTCBMDataByteBlocking(u8& value, u8 status = TCBM_STATUS_OK);    // Blocking with timeout (5s)

    // Timing helpers (1µs polling, 5s timeout)
    bool WaitForDAVState(bool asserted, const char* stage, u32 timeoutUs = COMMAND_TIMEOUT_US);

    // LISTEN/TALK helpers
    void HandleIdleCommand(u8 commandByte);
	void HandleListenSecondary(u8 secondaryByte);
	void HandleTalkSecondary(u8 secondaryByte);
	void EnterOpenState(u8 channel);
	void FinaliseOpenState(u8 channel);
	bool PrepareLoadChannel(u8 channel, bool fastMode = false);
	bool PrepareSaveChannel(u8 channel);
	void PrepareDirectoryResponse(u8 channel, bool fastMode = false);
	void PrepareStatusResponse(bool fastMode = false);
	bool PrepareFastBlockRead();
	bool PrepareFastBlockWrite();
	void PrepareBrowseIdleBus();

    // State handlers
	void ServiceOpenState();
	void ServiceLoadState();
	void ServiceSaveState();
	void ServiceDirectoryState();
	void ServiceFastLoadState();
	void ServiceFastDirectoryState();
	void ServiceFastBlockReadState();
	void ServiceFastBlockWriteState();

    // Debug helpers
	void ResetStateMachine();
	void AbortBlockingTransfer(const char* reason);
	void ClearDebugOverlay();
	void PushDebugLine(const char* fmt, ...);
	void SetDebugLine(int index, const char* fmt, ...);
	void UpdateDebugOverlay();
	void NoteTimeout(const char* what);
	void AppendCommandByte(Channel& channel, u8 byte);
	bool HandleU0Command(Channel& channel);
	bool ExtractU0Filename(const u8* data, size_t length);
	void ApplyPendingFastFilename(u8 channel);
	void PrepareFastLoadError(u8 channel);
	bool InitialiseFastHandshake(const char* stage);
	bool FinaliseFastHandshake();
	bool FastSendByte(u8 data);
	bool FastSendBlockByte(u8 data);
	bool FastReceiveBlockByte(u8& data);
	bool LoadFastByte(u8& data, bool& eoi);
	bool LoadFastDirectoryByte(u8& data, bool& eoi);

	static constexpr u32 COMMAND_TIMEOUT_US = 5000000u; // 5 seconds
	static constexpr u32 POLL_DELAY_US      = 1;         // 1 µs granularity
	static constexpr unsigned MAX_COMMAND_STABLE_ATTEMPTS = 100000u;
	static constexpr size_t FAST_FILENAME_MAX = 64;

    // Debug storage
    static char debugLines[DEBUG_LINE_CAPACITY][DEBUG_LINE_LENGTH];
    static int  debugLineCount;
    static char debugHistory[DEBUG_LINE_CAPACITY][DEBUG_LINE_LENGTH];
    static int  debugHistoryCount;
    static char lastTimeoutMessage[DEBUG_LINE_LENGTH];

	// TCBM-specific members
	TCBMState tcbmState;
	// Note: secondaryAddress (from base class) tracks the currently active channel
	u8  activeChannel;
	bool statusActive;
	bool directoryActive;
	bool pendingPopDir;
	bool captureOutput;
	u8  captureChannel;
	u32 captureLength;
	static constexpr size_t CAPTURE_BUFFER_SIZE = 0x1000;
	u8  captureBuffer[CAPTURE_BUFFER_SIZE];

	struct FastHandshakeContext
	{
		bool initialised;
		u8   ackLevel;
		u8   expectedDav;
		u8   status;
		bool prefetched;
		u8   prefetchedByte;
	} fastCtx;

	enum FastRequestType
	{
		FAST_REQ_NONE,
		FAST_REQ_FILENAME,
		FAST_REQ_TRACK_SECTOR,
		FAST_REQ_BLOCK_READ,
		FAST_REQ_BLOCK_WRITE
	};

	struct FastRequest
	{
		FastRequestType type;
		u8 track;
		u8 sector;
		u8 blockCount;
		char filename[FAST_FILENAME_MAX];
	} fastRequest;

	u32 blockIoBytesRemaining;
};
#endif
