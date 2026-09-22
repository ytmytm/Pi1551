// Protocol primitives shared by the Pi1551 implementation and native tests.
// The state transitions mirror tcbm2sd.ino send_data_stream()/state_fastblock().

#ifndef TCBM2SD_PROTOCOL_H
#define TCBM2SD_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

namespace Tcbm2sdProtocol
{

enum FastRequestType
{
	FAST_REQUEST_NONE,
	FAST_REQUEST_FILENAME,
	FAST_REQUEST_TRACK_SECTOR,
	FAST_REQUEST_BLOCK_READ,
	FAST_REQUEST_BLOCK_WRITE,
	FAST_REQUEST_SET_DEVICE,
	FAST_REQUEST_INVALID
};

struct FastRequest
{
	FastRequestType type;
	uint8_t track;
	uint8_t sector;
	uint8_t blockCount;
	uint8_t device;
	char filename[64];
};

// Parse a binary command-channel payload. NOT a U0 command is reported as
// FAST_REQUEST_NONE; malformed or context-inappropriate U0 is INVALID.
FastRequest ParseU0(const uint8_t* data, size_t length, bool inImage);

// Only read-side TCBM2SD requests are safe to execute while the emulated
// drive is frozen. Unknown/malformed U0 commands and block writes must remain
// visible to the 1551 ROM.
bool CanInterceptU0InEmulation(const FastRequest& request);

void FormatDosVersionStatus(char* output, size_t outputSize,
	const char* driveName, unsigned versionMajor, unsigned versionMinor,
	uint8_t track, uint8_t sector);

enum TalkTransfer
{
	TALK_TRANSFER_NONE,
	TALK_TRANSFER_STATUS,
	TALK_TRANSFER_PENDING_U0,
	TALK_TRANSFER_DIRECTORY,
	TALK_TRANSFER_FILE,
	TALK_TRANSFER_ERROR
};

struct TalkDecision
{
	TalkTransfer transfer;
	bool fast;
};

TalkDecision DecodeTalkSecondary(uint8_t secondary, bool pendingU0,
	uint8_t firstOpenByte, bool channelOpen);

struct FastHandshake
{
	uint8_t ack;
	uint8_t expectedDav;
};

struct FastHandshakeStep
{
	uint8_t ack;
	uint8_t expectedDav;
};

// Fast reads start by waiting for DAV=0. Fast writes perform that wait as the
// first alternating step. Both start from ACK=1, matching the Arduino code.
FastHandshake BeginFastRead();
FastHandshake BeginFastWrite();
FastHandshakeStep NextFastStep(FastHandshake& state);

} // namespace Tcbm2sdProtocol

#endif
