#include "tcbm2sd_protocol.h"

#include <stdio.h>
#include <string.h>

namespace Tcbm2sdProtocol
{

void FormatDosVersionStatus(char* output, size_t outputSize,
	const char* driveName, unsigned versionMajor, unsigned versionMinor,
	uint8_t track, uint8_t sector)
{
	if (!output || outputSize == 0)
		return;
	snprintf(output, outputSize, "73,%s V%02u.%02u (TCBM2SD COMPAT),%02u,%02u\r",
		driveName ? driveName : "", versionMajor, versionMinor,
		static_cast<unsigned>(track), static_cast<unsigned>(sector));
}

static uint8_t ToPetscii(uint8_t c)
{
	if (c == 0x5f)
		return 0x7e;
	if (c == 0x7e)
		return 0x5f;
	if (c >= 0x80 + 'a' && c <= 0x80 + 'z')
		c = static_cast<uint8_t>(c - 0xa0);
	if (c >= 0x80 + 'A' && c <= 0x80 + 'Z')
		c = static_cast<uint8_t>(c - 0x80);
	if (c >= 'a' && c <= 'z')
		c = static_cast<uint8_t>(c - 0x20);
	return c;
}

FastRequest ParseU0(const uint8_t* data, size_t length, bool inImage)
{
	FastRequest request;
	memset(&request, 0, sizeof(request));
	request.type = FAST_REQUEST_NONE;

	if (!data || length < 2 || data[0] != 'U' || data[1] != '0')
		return request;

	if (length >= 4 && data[2] == '>' && (data[3] == 8 || data[3] == 9))
	{
		request.type = FAST_REQUEST_SET_DEVICE;
		request.device = data[3];
		return request;
	}

	if (length < 3)
	{
		request.type = FAST_REQUEST_INVALID;
		return request;
	}

	const uint8_t mode = data[2] & 0x3f;
	if (mode == 0x1f)
	{
		size_t in = 3;
		if (in + 1 < length && data[in] == '0' && data[in + 1] == ':')
			in += 2;
		else if (in < length && data[in] == ':')
			++in;

		size_t out = 0;
		while (in < length && out + 1 < sizeof(request.filename))
		{
			const uint8_t value = data[in++];
			if (value == 0 || value == 0x0d)
				break;
			request.filename[out++] = static_cast<char>(ToPetscii(value));
		}
		request.filename[out] = '\0';
		request.type = out ? FAST_REQUEST_FILENAME : FAST_REQUEST_INVALID;
		return request;
	}

	if (!inImage)
	{
		request.type = FAST_REQUEST_INVALID;
		return request;
	}

	if (mode == 0x3f)
	{
		if (length < 5)
		{
			request.type = FAST_REQUEST_INVALID;
			return request;
		}
		request.type = FAST_REQUEST_TRACK_SECTOR;
		request.track = data[3];
		request.sector = data[4];
		return request;
	}

	// Unlike masked fast-load modes, block commands are exact byte values in
	// the Arduino reference implementation.
	if (data[2] == 0x00 || data[2] == 0x02)
	{
		if (length < 6 || data[5] == 0)
		{
			request.type = FAST_REQUEST_INVALID;
			return request;
		}
		request.type = data[2] == 0x00 ? FAST_REQUEST_BLOCK_READ : FAST_REQUEST_BLOCK_WRITE;
		request.track = data[3];
		request.sector = data[4];
		request.blockCount = data[5];
		return request;
	}

	request.type = FAST_REQUEST_INVALID;
	return request;
}

bool CanInterceptU0InEmulation(const FastRequest& request)
{
	switch (request.type)
	{
		case FAST_REQUEST_FILENAME:
		case FAST_REQUEST_TRACK_SECTOR:
		case FAST_REQUEST_BLOCK_READ:
		case FAST_REQUEST_BLOCK_WRITE:
			return true;
		default:
			return false;
	}
}

TalkDecision DecodeTalkSecondary(uint8_t secondary, bool pendingU0,
	uint8_t firstOpenByte, bool channelOpen)
{
	TalkDecision decision = { TALK_TRANSFER_NONE, false };
	const uint8_t kind = secondary & 0xf0;
	const uint8_t channel = secondary & 0x0f;
	if (kind != 0x60 && kind != 0x70)
		return decision;

	decision.fast = kind == 0x70;
	if (channel == 15)
		decision.transfer = TALK_TRANSFER_STATUS;
	else if (decision.fast && pendingU0)
		decision.transfer = TALK_TRANSFER_PENDING_U0;
	else if (firstOpenByte == '$' || (channel == 0 && firstOpenByte == 0 && !channelOpen))
		decision.transfer = TALK_TRANSFER_DIRECTORY;
	else if (firstOpenByte != 0 || channelOpen)
		decision.transfer = TALK_TRANSFER_FILE;
	else
		decision.transfer = TALK_TRANSFER_ERROR;
	return decision;
}

FastHandshake BeginFastRead()
{
	FastHandshake state = { 1, 0 };
	return state;
}

FastHandshake BeginFastWrite()
{
	FastHandshake state = { 1, 1 };
	return state;
}

FastHandshakeStep NextFastStep(FastHandshake& state)
{
	state.ack ^= 1;
	state.expectedDav ^= 1;
	FastHandshakeStep step = { state.ack, state.expectedDav };
	return step;
}

} // namespace Tcbm2sdProtocol
