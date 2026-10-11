#include "DiskImage.h"
#include "cbm_diskimage.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
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
    cbm_image_set_write_protected(true); u32 bytes = 0;
    CHECK(!cbm_image_block_io_begin(1,0,1,bytes,true)); CHECK(bytes == 0);
    CHECK(cbm_image_block_io_begin(1,0,1,bytes,false)); CHECK(bytes == 256); cbm_image_block_io_end();
    cbm_image_set_write_protected(false);
    CHECK(!cbm_image_mount("/no-such-file.d81")); CHECK(cbm_image_is_mounted()); CHECK(cbm_image_get_fs()->file == originalFile);
    NativeFatFSFail("sync"); CHECK(!cbm_image_sync()); CHECK(cbm_image_is_mounted()); CHECK(cbm_image_sync());
    cbm_image_unmount(); CHECK(!cbm_image_is_mounted());
    disk->Close();
    const unsigned lengths[] = {2,4,253,254,255,508,509,60000};
    for (unsigned length : lengths) {
        std::vector<unsigned char> program(length);
        for (unsigned i = 0; i < length; ++i) program[i] = i % 256;
        strcpy(info.fname,"/1551/HELLO.PRG"); info.fsize = length;
        CHECK(disk->OpenPRG(&info,program.data(),length));
        CHECK(cbm_image_mount_d64_sector_reader(info.fname,Sector,disk));
        u32 fileSize = 0; CHECK(cbm_image_open_file(2,"HELLO.PRG",fileSize)); CHECK(fileSize == 0xFFFFFFFF);
        for (unsigned i = 0; i < length; ++i) { u8 byte; CHECK(cbm_image_read_channel_byte(2,byte)); CHECK(byte == program[i]); }
        CHECK(cbm_image_channel_at_eof(2));
        cbm_image_unmount(); disk->Close();
    }
    delete disk;
    std::string d64 = std::string(argv[1]) + ".new.d64", g64 = std::string(argv[1]) + ".new.g64";
    CHECK(DiskImage::CreateNewImageFile(d64.c_str(),"00",DiskImage::D64));
    CHECK(DiskImage::CreateNewImageFile(g64.c_str(),"00",DiskImage::G64));
    CHECK(!DiskImage::CreateNewImageFile(g64.c_str(),"00",DiskImage::G64)); // never overwrite
    FILINFO created = {}; CHECK(f_stat(d64.c_str(),&created) == FR_OK); CHECK(created.fsize == 174848);
    FILE* exported = fopen(g64.c_str(),"rb"); CHECK(exported != 0);
    char header[12]; CHECK(fread(header,1,12,exported) == 12); fclose(exported);
    CHECK(memcmp(header,"GCR-1541",8) == 0); CHECK((unsigned char)header[9] == 84);
    puts("PASS actual D64 encoder, dirty retention, write/sync failures and runtime protection");
    return 0;
}
