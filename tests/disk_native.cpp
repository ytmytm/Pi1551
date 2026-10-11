#include "DiskImage.h"
#include "cbm_diskimage.h"
#include <cstdio>
#include <cstring>
#include <string>
extern void NativeFatFSFail(const std::string& operation);
extern "C" void SetACTLed(int) {}
static bool Sector(void* context,u8 track,u8 sector,u8* bytes) {
    return static_cast<DiskImage*>(context)->GetDecodedSector(track,sector,bytes);
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL disk:%d %s\n",__LINE__,#x); return 1; } } while(0)
int main(int argc,char** argv) {
    CHECK(argc == 2);
    FILINFO info = {}; strncpy(info.fname,argv[1],sizeof(info.fname)-1);
    unsigned size = DiskImage::CreateNewDiskInRAM("TEST","00"); CHECK(size == 174848);
    info.fsize = size;
    DiskImage* disk = new DiskImage(); CHECK(disk->OpenD64(&info,DiskImage::readBuffer,size));
    CHECK(disk->WriteD64());
    unsigned char original = disk->GetNextByte(0,10);
    disk->SetBit(0,10,7,!(original & 0x80)); CHECK(disk->IsDirty());
    NativeFatFSFail("write"); CHECK(!disk->Flush()); CHECK(disk->IsDirty());
    NativeFatFSFail("sync"); CHECK(!disk->Flush()); CHECK(disk->IsDirty());
    disk->SetReadOnly(true); CHECK(disk->Flush()); CHECK(!disk->IsDirty()); CHECK(disk->GetReadOnly());
    disk->SetReadOnly(false);
    disk->SetBit(0,10,7,(original & 0x80) != 0); CHECK(disk->IsDirty());
    CHECK(disk->Flush()); CHECK(!disk->IsDirty());
    CHECK(cbm_image_mount(argv[1])); FIL* originalFile = cbm_image_get_fs()->file;
    CHECK(!cbm_image_mount("/no-such-file.d81")); CHECK(cbm_image_is_mounted()); CHECK(cbm_image_get_fs()->file == originalFile);
    NativeFatFSFail("sync"); CHECK(!cbm_image_sync()); CHECK(cbm_image_is_mounted()); CHECK(cbm_image_sync());
    cbm_image_unmount(); CHECK(!cbm_image_is_mounted());
    disk->Close();
    unsigned char program[] = {0x01,0x10,0x60,0x00};
    strcpy(info.fname,"/1551/HELLO.PRG"); info.fsize = sizeof(program);
    CHECK(disk->OpenPRG(&info,program,sizeof(program)));
    CHECK(cbm_image_mount_d64_sector_reader(info.fname,Sector,disk));
    u32 fileSize = 0; CHECK(cbm_image_open_file(2,"HELLO.PRG",fileSize)); CHECK(fileSize == sizeof(program));
    u8 byte; CHECK(cbm_image_read_channel_byte(2,byte)); CHECK(byte == 1);
    CHECK(cbm_image_read_channel_byte(2,byte)); CHECK(byte == 0x10);
    cbm_image_unmount(); disk->Close(); delete disk;
    puts("PASS actual D64 encoder, dirty retention, write/sync failures and runtime protection");
    return 0;
}
