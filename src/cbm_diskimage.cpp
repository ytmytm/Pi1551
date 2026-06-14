// CBM DOS filesystem layer — FatFS port of tcbm2sd diskimage (read-only)

#include "cbm_diskimage.h"

#include "Petscii.h"

#include <cstring>
#include <strings.h>
#include <cctype>

static CbmFsImage s_di;
static CbmImageFile s_imgfile;

static struct CbmImageSession {
	bool active;
	bool filOpen;
	FIL fil;
	CbmFsImage fs;
	CbmImageFile channelFile[16];
	bool channelOpen[16];
	u32 channelFileSize[16];
	char path[256];
	u8 dirEntrySerial;
} s_mount = {};

static struct {
	bool active;
	bool writeMode;
} s_blockIo = {};

static int set_status(CbmFsImage* di, int status, int track, int sector)
{
	di->status = static_cast<char>(status);
	di->statusts.track = static_cast<u8>(track);
	di->statusts.sector = static_cast<u8>(sector);
	return status;
}

int cbm_di_rawname_from_name(u8* rawname, const char* name)
{
	std::memset(rawname, 0xa0, 16);
	int i = 0;
	for (; i < 16 && name[i]; ++i)
		rawname[i] = ascii2petscii(static_cast<u8>(name[i]));
	return i;
}

int cbm_di_name_from_rawname(char* name, const u8* rawname)
{
	int i = 0;
	for (; i < 16 && rawname[i] != 0xa0; ++i)
		name[i] = static_cast<char>(petscii2ascii(rawname[i]));
	name[i] = '\0';
	return i;
}

int cbm_di_tracks(CbmImageType type)
{
	switch (type)
	{
		case CBM_IMG_D64: return 35;
		case CBM_IMG_D64_40: return 40;
		case CBM_IMG_D64_42: return 42;
		case CBM_IMG_D71: return 70;
		case CBM_IMG_D81: return 80;
		case CBM_IMG_D80: return 77;
		case CBM_IMG_D82: return 154;
		default: return 0;
	}
}

int cbm_di_sectors_per_track(CbmImageType type, int track)
{
	switch (type)
	{
		case CBM_IMG_D71:
			if (track > 35)
				track -= 35;
			// fall through
		case CBM_IMG_D64:
		case CBM_IMG_D64_40:
		case CBM_IMG_D64_42:
			if (track < 18) return 21;
			if (track < 25) return 19;
			if (track < 31) return 18;
			return 17;
		case CBM_IMG_D81:
			return 40;
		case CBM_IMG_D82:
			if (track > 77)
				track -= 77;
			// fall through
		case CBM_IMG_D80:
			if (track < 40) return 29;
			if (track < 54) return 27;
			if (track < 65) return 25;
			return 23;
		default:
			return 0;
	}
}

static u32 get_block_num(CbmImageType type, CbmTrackSector ts)
{
	u32 block = 0;

	switch (type)
	{
		case CBM_IMG_D64:
		case CBM_IMG_D64_40:
		case CBM_IMG_D64_42:
			if (ts.track < 18)
				block = (ts.track - 1) * 21;
			else if (ts.track < 25)
				block = (ts.track - 18) * 19 + 17 * 21;
			else if (ts.track < 31)
				block = (ts.track - 25) * 18 + 17 * 21 + 7 * 19;
			else
				block = (ts.track - 31) * 17 + 17 * 21 + 7 * 19 + 6 * 18;
			return block + ts.sector;

		case CBM_IMG_D71:
			if (ts.track > 35)
			{
				block = 683;
				ts.track -= 35;
			}
			if (ts.track < 18)
				block += (ts.track - 1) * 21;
			else if (ts.track < 25)
				block += (ts.track - 18) * 19 + 17 * 21;
			else if (ts.track < 31)
				block += (ts.track - 25) * 18 + 17 * 21 + 7 * 19;
			else
				block += (ts.track - 31) * 17 + 17 * 21 + 7 * 19 + 6 * 18;
			return block + ts.sector;

		case CBM_IMG_D81:
			return (ts.track - 1) * 40 + ts.sector;

		case CBM_IMG_D82:
			block = 0;
			if (ts.track > 77)
			{
				block = 2083;
				ts.track = static_cast<u8>(ts.track - 77);
			}
			// fall through
		case CBM_IMG_D80:
			if (ts.track < 40)
				block += (ts.track - 1) * 29;
			else if (ts.track < 54)
				block += (ts.track - 40) * 27 + 39 * 29;
			else if (ts.track < 65)
				block += (ts.track - 54) * 25 + 39 * 29 + 14 * 27;
			else
				block += (ts.track - 65) * 23 + 39 * 29 + 14 * 27 + 11 * 25;
			return block + ts.sector;

		default:
			return 0;
	}
}

static u8* get_ts_addr(CbmFsImage* di, CbmTrackSector ts)
{
	if (di->sectorReader)
	{
		if (!di->sectorReader(di->sectorReaderContext, ts.track, ts.sector, di->image))
			std::memset(di->image, 0, sizeof(di->image));
		return di->image;
	}
	if (!di->file)
		return di->image;
	f_lseek(di->file, get_block_num(di->type, ts) * 256);
	UINT br = 0;
	f_read(di->file, di->image, 256, &br);
	(void)br;
	return di->image;
}

static int verify_next_ts(CbmFsImage* di, CbmTrackSector ts)
{
	if (ts.track == 0 || ts.track > cbm_di_tracks(di->type))
		return 0;
	if (ts.sector >= cbm_di_sectors_per_track(di->type, ts.track))
		return 0;
	return 1;
}

static CbmTrackSector next_ts_in_chain(CbmFsImage* di, CbmTrackSector ts)
{
	u8* p = get_ts_addr(di, ts);
	CbmTrackSector newts;
	newts.track = p[0];
	newts.sector = p[1];
	if (!verify_next_ts(di, newts))
	{
		newts.track = 0;
		newts.sector = 0;
	}
	return newts;
}

u8* cbm_di_title(CbmFsImage* di)
{
	switch (di->type)
	{
		case CBM_IMG_D81:
			return get_ts_addr(di, di->dir) + 4;
		case CBM_IMG_D80:
		case CBM_IMG_D82:
			return get_ts_addr(di, di->dir) + 6;
		default:
			return get_ts_addr(di, di->dir) + 144;
	}
}

int cbm_di_track_blocks_free(CbmFsImage* di, int track)
{
	u8* bam = nullptr;

	switch (di->type)
	{
		case CBM_IMG_D64:
		case CBM_IMG_D64_40:
		case CBM_IMG_D64_42:
		case CBM_IMG_D71:
			bam = get_ts_addr(di, di->bam);
			if (di->type == CBM_IMG_D71 && track >= 36)
				return bam[track + 185];
			return bam[track * 4];

		case CBM_IMG_D81:
			if (track <= 40)
				bam = get_ts_addr(di, di->bam);
			else
			{
				bam = get_ts_addr(di, di->bam2);
				track -= 40;
			}
			return bam[track * 6 + 10];

		case CBM_IMG_D80:
		case CBM_IMG_D82:
		{
			CbmTrackSector ts = di->bam;
			if (track > 150) { ts.sector = static_cast<u8>(ts.sector + 9); track -= 150; }
			if (track > 100) { ts.sector = static_cast<u8>(ts.sector + 6); track -= 100; }
			if (track > 50)  { ts.sector = static_cast<u8>(ts.sector + 3); track -= 50; }
			bam = get_ts_addr(di, ts);
			return bam[track * 5 + 1];
		}

		default:
			return 0;
	}
}

static int blocks_free(CbmFsImage* di)
{
	int blocks = 0;
	for (int track = 1; track <= cbm_di_tracks(di->type); ++track)
	{
		if (track != di->dir.track)
			blocks += cbm_di_track_blocks_free(di, track);
	}
	return blocks;
}

static bool detect_image_type(FIL* file, CbmFsImage* di)
{
	FSIZE_t size = f_size(file);
	switch (size)
	{
		case 174848:
		case 175531:
			di->type = CBM_IMG_D64;
			di->bam.track = 18; di->bam.sector = 0;
			di->dir = di->bam;
			return true;
		case 196608:
		case 197376:
			di->type = CBM_IMG_D64_40;
			di->bam.track = 18; di->bam.sector = 0;
			di->dir = di->bam;
			return true;
		case 205312:
		case 206114:
			di->type = CBM_IMG_D64_42;
			di->bam.track = 18; di->bam.sector = 0;
			di->dir = di->bam;
			return true;
		case 349696:
		case 351062:
			di->type = CBM_IMG_D71;
			di->bam.track = 18; di->bam.sector = 0;
			di->bam2.track = 53; di->bam2.sector = 0;
			di->dir = di->bam;
			return true;
		case 819200:
		case 822400:
			di->type = CBM_IMG_D81;
			di->bam.track = 40; di->bam.sector = 1;
			di->bam2.track = 40; di->bam2.sector = 2;
			di->dir.track = 40; di->dir.sector = 0;
			return true;
		case 533248:
			di->type = CBM_IMG_D80;
			di->bam.track = 38; di->bam.sector = 0;
			di->bam2.track = 38; di->bam2.sector = 3;
			di->dir.track = 39; di->dir.sector = 0;
			return true;
		case 1066496:
			di->type = CBM_IMG_D82;
			di->bam.track = 38; di->bam.sector = 0;
			di->bam2.track = 38; di->bam2.sector = 3;
			di->dir.track = 39; di->dir.sector = 0;
			return true;
		default:
			return false;
	}
}

CbmImageType CbmImagePathGetType(const char* path)
{
	FIL fp;
	if (f_open(&fp, path, FA_READ) != FR_OK)
		return static_cast<CbmImageType>(0);

	CbmFsImage probe;
	probe.file = &fp;
	if (!detect_image_type(&fp, &probe))
	{
		f_close(&fp);
		return static_cast<CbmImageType>(0);
	}
	f_close(&fp);
	return probe.type;
}

bool CbmImagePathSupported(const char* path)
{
	return CbmImagePathGetType(path) != 0;
}

static bool has_quasi_mount_extension(const char* path)
{
	const char* ext = strrchr(path, '.');
	if (!ext)
		return false;

	if (strcasecmp(ext, ".d71") == 0)
		return true;
	if (strcasecmp(ext, ".d81") == 0)
		return true;
	if (strcasecmp(ext, ".d80") == 0)
		return true;
	if (strcasecmp(ext, ".d82") == 0)
		return true;
	return false;
}

bool CbmImagePathQuasiMountOnly(const char* path)
{
	if (!has_quasi_mount_extension(path))
		return false;

	switch (CbmImagePathGetType(path))
	{
		case CBM_IMG_D71:
		case CBM_IMG_D81:
		case CBM_IMG_D80:
		case CBM_IMG_D82:
			return true;
		default:
			return false;
	}
}

void cbm_image_close_channel(u8 channel)
{
	if (channel >= 16 || !s_mount.channelOpen[channel])
		return;
	cbm_di_close(&s_mount.channelFile[channel]);
	s_mount.channelOpen[channel] = false;
	s_mount.channelFileSize[channel] = 0;
}

void cbm_image_close_all_channels()
{
	for (int i = 0; i < 16; ++i)
		cbm_image_close_channel(static_cast<u8>(i));
}

void cbm_image_unmount()
{
	cbm_image_close_all_channels();
	if (s_mount.filOpen)
	{
		f_close(&s_mount.fil);
		s_mount.filOpen = false;
	}
	if (s_mount.active)
		cbm_di_unload_image(&s_mount.fs);
	s_mount.active = false;
	s_mount.path[0] = '\0';
	s_mount.dirEntrySerial = 0;
}

bool cbm_image_mount(const char* path)
{
	if (!path || path[0] == '\0')
		return false;

	if (s_mount.active && strcasecmp(path, s_mount.path) == 0)
		return true;

	cbm_image_unmount();

	if (f_open(&s_mount.fil, path, FA_READ) != FR_OK)
		return false;

	if (!cbm_di_load_image(&s_mount.fil))
	{
		f_close(&s_mount.fil);
		return false;
	}

	s_mount.fs = s_di;
	s_mount.filOpen = true;
	s_mount.active = true;
	strncpy(s_mount.path, path, sizeof(s_mount.path) - 1);
	s_mount.path[sizeof(s_mount.path) - 1] = '\0';
	return true;
}

bool cbm_image_mount_d64_sector_reader(const char* path, CbmImageSectorReader reader, void* context)
{
	if (!reader || !context)
		return false;

	if (s_mount.active && path && path[0] != '\0' && strcasecmp(path, s_mount.path) == 0)
		return true;

	cbm_image_unmount();

	std::memset(&s_mount.fs, 0, sizeof(s_mount.fs));
	s_mount.fs.type = CBM_IMG_D64;
	s_mount.fs.bam.track = 18;
	s_mount.fs.bam.sector = 0;
	s_mount.fs.dir.track = 18;
	s_mount.fs.dir.sector = 0;
	s_mount.fs.file = nullptr;
	s_mount.fs.sectorReader = reader;
	s_mount.fs.sectorReaderContext = context;
	s_mount.fs.blocksfree = blocks_free(&s_mount.fs);
	set_status(&s_mount.fs, 254, 0, 0);

	s_mount.filOpen = false;
	s_mount.active = true;
	if (path)
	{
		strncpy(s_mount.path, path, sizeof(s_mount.path) - 1);
		s_mount.path[sizeof(s_mount.path) - 1] = '\0';
	}
	return true;
}

bool cbm_image_is_mounted()
{
	return s_mount.active;
}

CbmFsImage* cbm_image_get_fs()
{
	return s_mount.active ? &s_mount.fs : nullptr;
}

u8 cbm_image_dir_entry_serial()
{
	return s_mount.dirEntrySerial;
}

void cbm_image_reset_dir_entry_serial()
{
	s_mount.dirEntrySerial = 0;
}

void cbm_image_set_dir_entry_serial(u8 value)
{
	s_mount.dirEntrySerial = value;
}

bool cbm_image_open_file(u8 channel, const char* filename, u32& fileSizeOut)
{
	if (channel >= 16 || !s_mount.active)
		return false;

	cbm_image_close_channel(channel);

	CbmImageFile opened = {};
	CbmImageFile* img = cbm_di_open(&s_mount.fs, filename, CBM_T_PRG);
	if (!img)
	{
		fileSizeOut = 0;
		return false;
	}

	opened = *img;
	s_mount.channelFile[channel] = opened;
	s_mount.channelOpen[channel] = true;
	// Byte length is unknown until EOF (dir entry size is block count, not bytes).
	(void)opened;
	fileSizeOut = 0xFFFFFFFF;
	s_mount.channelFileSize[channel] = fileSizeOut;
	return true;
}

bool cbm_image_open_ts(u8 channel, u8 track, u8 sector, u32& fileSizeOut)
{
	if (channel >= 16 || !s_mount.active)
		return false;

	cbm_image_close_channel(channel);

	CbmImageFile opened = {};
	CbmImageFile* img = cbm_di_open_ts(&s_mount.fs, track, sector);
	if (!img)
	{
		fileSizeOut = 0;
		return false;
	}

	opened = *img;
	s_mount.channelFile[channel] = opened;
	s_mount.channelOpen[channel] = true;
	fileSizeOut = 0xFFFFFFFF;
	s_mount.channelFileSize[channel] = fileSizeOut;
	return true;
}

bool cbm_image_read_channel_byte(u8 channel, u8& data)
{
	if (channel >= 16 || !s_mount.channelOpen[channel])
		return false;
	if (cbm_di_read(&s_mount.channelFile[channel], &data, 1) != 1)
		return false;
	return true;
}

u32 cbm_image_channel_file_size(u8 channel)
{
	if (channel >= 16)
		return 0;
	return s_mount.channelFileSize[channel];
}

u32 cbm_image_channel_position(u8 channel)
{
	if (channel >= 16)
		return 0;
	return s_mount.channelFile[channel].position;
}

bool cbm_image_channel_at_eof(u8 channel)
{
	if (channel >= 16 || !s_mount.channelOpen[channel])
		return true;

	CbmImageFile& file = s_mount.channelFile[channel];
	return file.nextts.track == 0 && file.bufptr >= file.buflen;
}

void cbm_image_set_channel_position(u8 channel, u32 position)
{
	if (channel >= 16)
		return;
	s_mount.channelFile[channel].position = position;
}

const char* cbm_image_get_mounted_path()
{
	return s_mount.active ? s_mount.path : nullptr;
}

int cbm_image_collect_dir_entries(CbmImageDirListEntry* entries, int maxEntries)
{
	if (!entries || maxEntries <= 0 || !s_mount.active)
		return 0;

	CbmImageFile* dirFile = cbm_di_open(&s_mount.fs, "$", CBM_T_PRG);
	if (!dirFile)
		return 0;

	u8 skip[254];
	cbm_di_read(dirFile, skip, sizeof(skip));

	int count = 0;
	u8 serial = 0;
	for (;;)
	{
		u8 chunk = 32;
		serial++;
		if (serial == 8)
		{
			chunk = 30;
			serial = 0;
		}

		u8 entry[32];
		int got = cbm_di_read(dirFile, entry, chunk);
		if (got < 30)
			break;
		if (entry[0] == 0)
			continue;
		if (count >= maxEntries)
			break;

		cbm_di_name_from_rawname(entries[count].name, entry + 3);
		entries[count].size = static_cast<u32>((entry[29] << 8) | entry[28]);
		entries[count].type = entry[0] & 7;
		count++;
	}

	cbm_di_close(dirFile);
	return count;
}

CbmFsImage* cbm_di_load_image(FIL* file)
{
	if (!file)
		return nullptr;

	CbmFsImage* di = &s_di;
	std::memset(di, 0, sizeof(*di));
	di->file = file;
	if (!detect_image_type(file, di))
	{
		di->file = nullptr;
		return nullptr;
	}

	di->blocksfree = blocks_free(di);
	set_status(di, 254, 0, 0);
	return di;
}

void cbm_di_unload_image(CbmFsImage* di)
{
	if (!di)
		return;
	di->file = nullptr;
	di->sectorReader = nullptr;
	di->sectorReaderContext = nullptr;
	di->status = 0;
}

static int match_pattern(const u8* rawpattern, const u8* rawname)
{
	for (int i = 0; i < 16; ++i)
	{
		if (rawpattern[i] == '*')
			return 1;
		if (rawname[i] == 0xa0)
			return rawpattern[i] == 0xa0 ? 1 : 0;
		if (rawpattern[i] == '?')
			continue;
		u8 patternChar = rawpattern[i];
		u8 nameChar = rawname[i];
		if (patternChar == nameChar)
			continue;
		char patternAscii = static_cast<char>(petscii2ascii(patternChar));
		char nameAscii = static_cast<char>(petscii2ascii(nameChar));
		if (patternAscii >= 'A' && patternAscii <= 'Z'
			&& nameAscii >= 'A' && nameAscii <= 'Z'
			&& toupper(static_cast<unsigned char>(patternAscii)) == toupper(static_cast<unsigned char>(nameAscii)))
			continue;
		return 0;
	}
	return 1;
}

static CbmRawDirEntry* find_file_entry(CbmFsImage* di, u8* rawpattern, CbmFileType type)
{
	CbmTrackSector ts;
	switch (di->type)
	{
		case CBM_IMG_D80:
		case CBM_IMG_D82:
			ts.track = di->dir.track;
			ts.sector = 1;
			break;
		default:
			ts = next_ts_in_chain(di, di->dir);
			break;
	}

	while (ts.track)
	{
		u8* buffer = get_ts_addr(di, ts);
		for (int offset = 0; offset < 256; offset += 32)
		{
			CbmRawDirEntry* rde = reinterpret_cast<CbmRawDirEntry*>(buffer + offset);
			if ((rde->type & ~0x40) == (type | 0x80))
			{
				if (match_pattern(rawpattern, rde->rawname))
					return rde;
			}
		}
		ts = next_ts_in_chain(di, ts);
	}
	return nullptr;
}

CbmImageFile* cbm_di_open(CbmFsImage* di, const char* rawname, CbmFileType type)
{
	set_status(di, 255, 0, 0);

	CbmImageFile* imgfile = &s_imgfile;
	CbmRawDirEntry* rde = nullptr;
	u16 blocks = 0;
	u8* p;

	if (strcmp("$", rawname) == 0)
	{
		imgfile->ts = di->dir;
		p = get_ts_addr(di, di->dir);
		imgfile->buffer = p + 2;
		switch (di->type)
		{
			case CBM_IMG_D80:
			case CBM_IMG_D82:
				imgfile->nextts.track = di->dir.track;
				imgfile->nextts.sector = 1;
				break;
			default:
				imgfile->nextts.track = p[0];
				imgfile->nextts.sector = p[1];
				break;
		}
		imgfile->buflen = 254;
	}
	else
	{
		u8 pattern[16];
		cbm_di_rawname_from_name(pattern, rawname);
		rde = find_file_entry(di, pattern, type);
		if (!rde)
		{
			set_status(di, 62, 0, 0);
			return nullptr;
		}
		blocks = static_cast<u16>((rde->sizehi << 8) | rde->sizelo);
		imgfile->ts = rde->startts;
		if (imgfile->ts.track > cbm_di_tracks(di->type))
			return nullptr;
		p = get_ts_addr(di, rde->startts);
		imgfile->buffer = p + 2;
		imgfile->nextts.track = p[0];
		imgfile->nextts.sector = p[1];
		if (imgfile->nextts.track == 0)
			imgfile->buflen = imgfile->nextts.sector - 1;
		else
			imgfile->buflen = 254;
	}

	imgfile->diskimage = di;
	imgfile->rawdirentry = rde;
	imgfile->blocks = blocks;
	imgfile->position = 0;
	imgfile->bufptr = 0;
	set_status(di, 0, 0, 0);
	return imgfile;
}

CbmImageFile* cbm_di_open_ts(CbmFsImage* di, u8 track, u8 sector)
{
	set_status(di, 255, 0, 0);

	CbmImageFile* imgfile = &s_imgfile;
	imgfile->ts.track = track;
	imgfile->ts.sector = sector;
	if (imgfile->ts.track > cbm_di_tracks(di->type))
		return nullptr;

	u8* p = get_ts_addr(di, imgfile->ts);
	imgfile->buffer = p + 2;
	imgfile->nextts.track = p[0];
	imgfile->nextts.sector = p[1];
	if (imgfile->nextts.track == 0)
		imgfile->buflen = imgfile->nextts.sector - 1;
	else
		imgfile->buflen = 254;

	imgfile->diskimage = di;
	imgfile->rawdirentry = nullptr;
	imgfile->blocks = 0;
	imgfile->position = 0;
	imgfile->bufptr = 0;
	set_status(di, 0, 0, 0);
	return imgfile;
}

void cbm_di_close(CbmImageFile* imgfile)
{
	(void)imgfile;
}

int cbm_di_read(CbmImageFile* imgfile, u8* buffer, int len)
{
	int counter = 0;

	while (len > 0)
	{
		int bytesleft = imgfile->buflen - imgfile->bufptr;
		if (bytesleft == 0)
		{
			if (!verify_next_ts(imgfile->diskimage, imgfile->nextts))
				return counter;

			imgfile->ts = imgfile->nextts;
			u8* p = get_ts_addr(imgfile->diskimage, imgfile->ts);
			imgfile->buffer = p + 2;
			imgfile->nextts.track = p[0];
			imgfile->nextts.sector = p[1];
			if (imgfile->nextts.track == 0)
				imgfile->buflen = imgfile->nextts.sector > 0 ? imgfile->nextts.sector - 1 : 254;
			else
				imgfile->buflen = 254;
			imgfile->bufptr = 0;
			bytesleft = imgfile->buflen;
		}

		while (bytesleft > 0 && len > 0)
		{
			*buffer++ = imgfile->buffer[imgfile->bufptr++];
			--len;
			--bytesleft;
			++counter;
			++imgfile->position;
		}
	}
	return counter;
}

u32 cbm_image_ts_byte_offset(u8 track, u8 sector)
{
	if (!s_mount.active)
		return 0;

	CbmTrackSector ts;
	ts.track = track;
	ts.sector = sector;
	return get_block_num(s_mount.fs.type, ts) * 256;
}

bool cbm_image_block_io_begin(u8 track, u8 sector, u8 blockCount, u32& bytesOut, bool write)
{
	bytesOut = 0;
	if (!s_mount.active || !s_mount.filOpen || blockCount == 0)
		return false;

	bytesOut = static_cast<u32>(blockCount) << 8;

	if (write)
	{
		f_close(&s_mount.fil);
		if (f_open(&s_mount.fil, s_mount.path, FA_READ | FA_WRITE) != FR_OK)
		{
			s_mount.filOpen = false;
			return false;
		}
		s_mount.fs.file = &s_mount.fil;
	}

	if (f_lseek(&s_mount.fil, cbm_image_ts_byte_offset(track, sector)) != FR_OK)
		return false;

	s_blockIo.active = true;
	s_blockIo.writeMode = write;
	return true;
}

bool cbm_image_block_io_read_byte(u8& data)
{
	if (!s_blockIo.active || s_blockIo.writeMode || !s_mount.filOpen)
		return false;

	UINT br = 0;
	if (f_read(&s_mount.fil, &data, 1, &br) != FR_OK || br != 1)
		return false;
	return true;
}

bool cbm_image_block_io_write_byte(u8 data)
{
	if (!s_blockIo.active || !s_blockIo.writeMode || !s_mount.filOpen)
		return false;

	UINT bw = 0;
	if (f_write(&s_mount.fil, &data, 1, &bw) != FR_OK || bw != 1)
		return false;
	return true;
}

void cbm_image_block_io_end()
{
	if (s_blockIo.writeMode && s_mount.filOpen)
	{
		f_sync(&s_mount.fil);
		f_close(&s_mount.fil);
		if (f_open(&s_mount.fil, s_mount.path, FA_READ) == FR_OK)
		{
			s_mount.fs.file = &s_mount.fil;
			s_mount.filOpen = true;
		}
		else
		{
			s_mount.filOpen = false;
			s_mount.active = false;
		}
	}

	s_blockIo.active = false;
	s_blockIo.writeMode = false;
}
