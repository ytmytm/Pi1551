#include "tcbm2sd_protocol.h"
#include "cbm_diskimage.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace Tcbm2sdProtocol;

static std::vector<uint8_t> ReadBytes(std::istringstream& input)
{
	std::vector<uint8_t> bytes;
	unsigned value;
	while (input >> std::hex >> value)
		bytes.push_back(static_cast<uint8_t>(value));
	return bytes;
}

static const char* RequestName(FastRequestType type)
{
	switch (type)
	{
		case FAST_REQUEST_NONE: return "none";
		case FAST_REQUEST_FILENAME: return "filename";
		case FAST_REQUEST_TRACK_SECTOR: return "track-sector";
		case FAST_REQUEST_BLOCK_READ: return "block-read";
		case FAST_REQUEST_BLOCK_WRITE: return "block-write";
		case FAST_REQUEST_SET_DEVICE: return "set-device";
		default: return "invalid";
	}
}

static const char* TransferName(TalkTransfer transfer)
{
	switch (transfer)
	{
		case TALK_TRANSFER_NONE: return "none";
		case TALK_TRANSFER_STATUS: return "status";
		case TALK_TRANSFER_PENDING_U0: return "pending-u0";
		case TALK_TRANSFER_DIRECTORY: return "directory";
		case TALK_TRANSFER_FILE: return "file";
		default: return "error";
	}
}

static CbmImageType ImageType(const std::string& name)
{
	if (name == "d64") return CBM_IMG_D64;
	if (name == "d71") return CBM_IMG_D71;
	if (name == "d81") return CBM_IMG_D81;
	if (name == "d80") return CBM_IMG_D80;
	if (name == "d82") return CBM_IMG_D82;
	return static_cast<CbmImageType>(0);
}

static void PrintReadTrace(const std::vector<uint8_t>& bytes)
{
	FastHandshake state = BeginFastRead();
	std::cout << "ack=1 wait-dav=0 dir=out";
	for (size_t i = 0; i < bytes.size(); ++i)
	{
		const FastHandshakeStep step = NextFastStep(state);
		const unsigned status = i + 1 == bytes.size() ? 3 : 0;
		std::cout << " | status=" << status << " data="
			<< std::hex << std::setw(2) << std::setfill('0') << unsigned(bytes[i])
			<< std::dec << " ack=" << unsigned(step.ack)
			<< " wait-dav=" << unsigned(step.expectedDav);
	}
	std::cout << " | final-status=" << (bytes.empty() ? 2 : 3)
		<< " dir=in ack=1 wait-dav=1 status=0\n";
}

static void PrintWriteTrace(const std::vector<uint8_t>& bytes)
{
	FastHandshake state = BeginFastWrite();
	std::cout << "ack=1 dir=in";
	for (size_t i = 0; i < bytes.size(); ++i)
	{
		const FastHandshakeStep step = NextFastStep(state);
		const unsigned status = i + 1 == bytes.size() ? 3 : 0;
		// Ordering is intentional: ACK acknowledges data only after DAV and read.
		std::cout << " | wait-dav=" << unsigned(step.expectedDav)
			<< " read=" << std::hex << std::setw(2) << std::setfill('0') << unsigned(bytes[i])
			<< std::dec << " status=" << status << " ack=" << unsigned(step.ack);
	}
	std::cout << " | final-status=" << (bytes.empty() ? 2 : 3)
		<< " dir=in ack=1 wait-dav=1 status=0\n";
}

int main()
{
	std::string line;
	while (std::getline(std::cin, line))
	{
		std::istringstream input(line);
		std::string command;
		input >> command;
		if (command == "read")
			PrintReadTrace(ReadBytes(input));
		else if (command == "write")
			PrintWriteTrace(ReadBytes(input));
		else if (command == "u0")
		{
			unsigned inImage = 0;
			input >> inImage;
			const std::vector<uint8_t> bytes = ReadBytes(input);
			const FastRequest request = ParseU0(bytes.empty() ? 0 : &bytes[0], bytes.size(), inImage != 0);
			std::cout << "type=" << RequestName(request.type)
				<< " track=" << unsigned(request.track)
				<< " sector=" << unsigned(request.sector)
				<< " count=" << unsigned(request.blockCount)
				<< " device=" << unsigned(request.device)
				<< " filename=" << request.filename << '\n';
		}
		else if (command == "geometry")
		{
			std::string name;
			unsigned track = 0;
			unsigned sector = 0;
			input >> name >> track >> sector;
			const CbmImageType type = ImageType(name);
			CbmTrackSector ts = { static_cast<u8>(track), static_cast<u8>(sector) };
			std::cout << "valid=" << (cbm_di_valid_ts(type, ts.track, ts.sector) ? 1 : 0)
				<< " tracks=" << cbm_di_tracks(type)
				<< " sectors=" << cbm_di_sectors_per_track(type, track)
				<< " block=" << cbm_di_block_num(type, ts)
				<< " bytes=" << cbm_di_data_size(type) << '\n';
		}
		else if (command == "talk")
		{
			unsigned secondary = 0;
			unsigned pending = 0;
			unsigned first = 0;
			unsigned open = 0;
			input >> std::hex >> secondary >> std::dec >> pending >> std::hex >> first >> std::dec >> open;
			const TalkDecision decision = DecodeTalkSecondary(
				static_cast<uint8_t>(secondary), pending != 0, static_cast<uint8_t>(first), open != 0);
			std::cout << "transfer=" << TransferName(decision.transfer)
				<< " fast=" << (decision.fast ? 1 : 0) << '\n';
		}
		else if (command == "block-roundtrip")
		{
			std::string path;
			unsigned track = 0;
			unsigned sector = 0;
			input >> path >> track >> sector;
			bool ok = cbm_image_mount(path.c_str());
			u32 bytes = 0;
			ok = ok && cbm_image_block_io_begin(
				static_cast<u8>(track), static_cast<u8>(sector), 1, bytes, true);
			for (u32 i = 0; ok && i < bytes; ++i)
				ok = cbm_image_block_io_write_byte(static_cast<u8>((i * 37 + track + sector) & 0xff));
			cbm_image_block_io_end();

			u32 readBytes = 0;
			ok = ok && cbm_image_block_io_begin(
				static_cast<u8>(track), static_cast<u8>(sector), 1, readBytes, false);
			for (u32 i = 0; ok && i < readBytes; ++i)
			{
				u8 value = 0;
				ok = cbm_image_block_io_read_byte(value)
					&& value == static_cast<u8>((i * 37 + track + sector) & 0xff);
			}
			cbm_image_block_io_end();
			cbm_image_unmount();
			std::cout << "ok=" << (ok ? 1 : 0) << " bytes=" << readBytes << '\n';
		}
		else if (command == "status73")
		{
			char status[96];
			FormatDosVersionStatus(status, sizeof(status), "PI1551", 1, 25, 0, 0);
			for (const char* p = status; *p; ++p)
				std::cout << (*p == '\r' ? "\\r" : std::string(1, *p));
			std::cout << '\n';
		}
		else if (!command.empty())
			std::cout << "error=unknown-command\n";
	}
	return 0;
}
