#include "tcbm2sd_protocol.h"
#include "cbm_diskimage.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
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

struct SectorReaderState
{
	std::vector<std::pair<uint8_t, uint8_t> > reads;
	bool fileImage;
	bool hasWrittenSector;
	u8 writtenTrack;
	u8 writtenSector;
	u8 writtenData[256];

	SectorReaderState() : fileImage(false), hasWrittenSector(false),
		writtenTrack(0), writtenSector(0) {}
};

static bool ReadSyntheticSector(void* context, u8 track, u8 sector, u8* buffer)
{
	SectorReaderState* state = static_cast<SectorReaderState*>(context);
	state->reads.push_back(std::make_pair(track, sector));
	if (state->hasWrittenSector && track == state->writtenTrack && sector == state->writtenSector)
	{
		std::copy(state->writtenData, state->writtenData + 256, buffer);
		return true;
	}
	if (state->fileImage)
	{
		std::fill(buffer, buffer + 256, 0);
		if (track == 18 && sector == 0)
		{
			buffer[0] = 18;
			buffer[1] = 1;
		}
		else if (track == 18 && sector == 1)
		{
			buffer[2] = 0x82;
			buffer[3] = 1;
			buffer[4] = 0;
			std::fill(buffer + 5, buffer + 21, 0xa0);
			buffer[5] = 0xc7;
			buffer[6] = 0xc1;
			buffer[7] = 0xcd;
			buffer[8] = 0xc5;
			buffer[30] = 1;
		}
		else if (track == 1 && sector == 0)
		{
			buffer[0] = 0;
			buffer[1] = 5;
			buffer[2] = 0x01;
			buffer[3] = 0x08;
			buffer[4] = 0xaa;
			buffer[5] = 0x55;
		}
		return true;
	}
	for (unsigned i = 0; i < 256; ++i)
		buffer[i] = static_cast<u8>((track * 7 + sector * 13 + i) & 0xff);
	return true;
}

static bool WriteSyntheticSector(void* context, u8 track, u8 sector, const u8* buffer)
{
	SectorReaderState* state = static_cast<SectorReaderState*>(context);
	state->hasWrittenSector = true;
	state->writtenTrack = track;
	state->writtenSector = sector;
	std::copy(buffer, buffer + 256, state->writtenData);
	return true;
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
		else if (command == "emulation-u0")
		{
			unsigned inImage = 0;
			input >> inImage;
			const std::vector<uint8_t> bytes = ReadBytes(input);
			const FastRequest request = ParseU0(bytes.empty() ? 0 : &bytes[0], bytes.size(), inImage != 0);
			std::cout << "intercept=" << (CanInterceptU0InEmulation(request) ? 1 : 0)
				<< " type=" << RequestName(request.type) << '\n';
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
		else if (command == "callback-block-read")
		{
			unsigned track = 0;
			unsigned sector = 0;
			unsigned count = 0;
			input >> track >> sector >> count;
			SectorReaderState state;
			bool ok = cbm_image_mount_d64_sector_reader(
				"memory.d64", ReadSyntheticSector, &state);
			state.reads.clear(); // Ignore BAM reads performed while mounting.
			u32 bytes = 0;
			ok = ok && cbm_image_block_io_begin(static_cast<u8>(track),
				static_cast<u8>(sector), static_cast<u8>(count), bytes, false);
			std::vector<u8> data;
			for (u32 i = 0; ok && i < bytes; ++i)
			{
				u8 value = 0;
				ok = cbm_image_block_io_read_byte(value);
				data.push_back(value);
			}
			cbm_image_block_io_end();
			cbm_image_unmount();
			std::cout << "ok=" << (ok ? 1 : 0) << " bytes=" << data.size() << " reads=";
			for (size_t i = 0; i < state.reads.size(); ++i)
			{
				if (i) std::cout << ',';
				std::cout << unsigned(state.reads[i].first) << '/' << unsigned(state.reads[i].second);
			}
			if (!data.empty())
				std::cout << " first=" << unsigned(data.front()) << " last=" << unsigned(data.back());
			std::cout << '\n';
		}
		else if (command == "callback-block-write")
		{
			SectorReaderState state;
			bool ok = cbm_image_mount_d64_sector_reader(
				"memory.d64", ReadSyntheticSector, &state, WriteSyntheticSector);
			u32 bytes = 0;
			ok = ok && cbm_image_block_io_begin(1, 0, 1, bytes, true);
			for (u32 i = 0; ok && i < bytes; ++i)
				ok = cbm_image_block_io_write_byte(static_cast<u8>(i * 37 + 11));
			cbm_image_block_io_end();
			u32 readBytes = 0;
			ok = ok && cbm_image_block_io_begin(1, 0, 1, readBytes, false);
			for (u32 i = 0; ok && i < readBytes; ++i)
			{
				u8 value = 0;
				ok = cbm_image_block_io_read_byte(value)
					&& value == static_cast<u8>(i * 37 + 11);
			}
			cbm_image_block_io_end();
			cbm_image_unmount();
			std::cout << "ok=" << (ok ? 1 : 0) << " bytes=" << readBytes << '\n';
		}
		else if (command == "callback-file-read")
		{
			SectorReaderState state;
			state.fileImage = true;
			bool ok = cbm_image_mount_d64_sector_reader(
				"memory.d64", ReadSyntheticSector, &state);
			u32 size = 0;
			ok = ok && cbm_image_open_file(0, "GAME", size);
			std::vector<u8> data;
			u8 value = 0;
			while (ok && cbm_image_read_channel_byte(0, value))
				data.push_back(value);
			cbm_image_unmount();
			std::cout << "ok=" << (ok ? 1 : 0) << " data=";
			for (size_t i = 0; i < data.size(); ++i)
				std::cout << std::hex << std::setw(2) << std::setfill('0') << unsigned(data[i]);
			std::cout << std::dec << '\n';
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
