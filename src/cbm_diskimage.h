// CBM DOS filesystem access inside D64/D71/D81/D80/D82 images
// Based on diskimage 0.95 by Per Olofsson, adapted for Pi1551 / FatFS.
// D80/D82 support from tcbm2sd Arduino port.

#ifndef CBM_DISKIMAGE_H
#define CBM_DISKIMAGE_H

#include "ff.h"
#include "types.h"

typedef enum CbmImageType {
	CBM_IMG_D64 = 1,
	CBM_IMG_D64_40,
	CBM_IMG_D64_42,
	CBM_IMG_D71,
	CBM_IMG_D81,
	CBM_IMG_D80,
	CBM_IMG_D82
} CbmImageType;

typedef enum CbmFileType {
	CBM_T_DEL = 0,
	CBM_T_SEQ,
	CBM_T_PRG,
	CBM_T_USR,
	CBM_T_REL,
	CBM_T_CBM,
	CBM_T_DIR
} CbmFileType;

typedef struct CbmTrackSector {
	u8 track;
	u8 sector;
} CbmTrackSector;

typedef struct CbmFsImage {
	CbmImageType type;
	u8 image[256];
	CbmTrackSector bam;
	CbmTrackSector bam2;
	CbmTrackSector dir;
	int blocksfree;
	char status;
	CbmTrackSector statusts;
	FIL* file;
} CbmFsImage;

typedef struct CbmRawDirEntry {
	CbmTrackSector nextts;
	u8 type;
	CbmTrackSector startts;
	u8 rawname[16];
	CbmTrackSector relsidets;
	u8 relrecsize;
	u8 unused[4];
	CbmTrackSector replacetemp;
	u8 sizelo;
	u8 sizehi;
} CbmRawDirEntry;

typedef struct CbmImageFile {
	CbmFsImage* diskimage;
	CbmRawDirEntry* rawdirentry;
	u32 position;
	CbmTrackSector ts;
	CbmTrackSector nextts;
	u8* buffer;
	int bufptr;
	int buflen;
	u16 blocks;
} CbmImageFile;

CbmImageType CbmImagePathGetType(const char* path);
bool CbmImagePathSupported(const char* path);
bool CbmImagePathQuasiMountOnly(const char* path);

bool cbm_image_mount(const char* path);
void cbm_image_unmount();
bool cbm_image_is_mounted();
CbmFsImage* cbm_image_get_fs();
u8 cbm_image_dir_entry_serial();
void cbm_image_reset_dir_entry_serial();
void cbm_image_set_dir_entry_serial(u8 value);

void cbm_image_close_channel(u8 channel);
void cbm_image_close_all_channels();
bool cbm_image_open_file(u8 channel, const char* filename, u32& fileSizeOut);
bool cbm_image_open_ts(u8 channel, u8 track, u8 sector, u32& fileSizeOut);
bool cbm_image_read_channel_byte(u8 channel, u8& data);
u32 cbm_image_channel_file_size(u8 channel);
u32 cbm_image_channel_position(u8 channel);
bool cbm_image_channel_at_eof(u8 channel);
void cbm_image_set_channel_position(u8 channel, u32 position);

typedef struct CbmImageDirListEntry {
	char name[17];
	u32 size;
	u8 type;
} CbmImageDirListEntry;

int cbm_image_collect_dir_entries(CbmImageDirListEntry* entries, int maxEntries);
const char* cbm_image_get_mounted_path();

CbmFsImage* cbm_di_load_image(FIL* file);
void cbm_di_unload_image(CbmFsImage* di);

CbmImageFile* cbm_di_open(CbmFsImage* di, const char* rawname, CbmFileType type);
CbmImageFile* cbm_di_open_ts(CbmFsImage* di, u8 track, u8 sector);
void cbm_di_close(CbmImageFile* imgfile);

int cbm_di_read(CbmImageFile* imgfile, u8* buffer, int len);

int cbm_di_sectors_per_track(CbmImageType type, int track);
int cbm_di_tracks(CbmImageType type);
u8* cbm_di_title(CbmFsImage* di);
int cbm_di_track_blocks_free(CbmFsImage* di, int track);

int cbm_di_rawname_from_name(u8* rawname, const char* name);
int cbm_di_name_from_rawname(char* name, const u8* rawname);

u32 cbm_image_ts_byte_offset(u8 track, u8 sector);
bool cbm_image_block_io_begin(u8 track, u8 sector, u8 blockCount, u32& bytesOut, bool write);
bool cbm_image_block_io_read_byte(u8& data);
bool cbm_image_block_io_write_byte(u8 data);
void cbm_image_block_io_end();

#endif
