// Production FatFS + diskio; only the physical SD/USB sector devices are stubbed.
#include "ff.h"
#include "diskio.h"
#include "Pi1551Oled.h"
#include "options.h"
#include "ROMs.h"
#include "tape_player.h"
#include "ScreenLCD.h"
#include "SpinLock.h"
#include "InputMappings.h"
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>
static std::vector<unsigned char> sectors;
static bool initOK = true, readFails = false, writeFails = false;
static unsigned writes = 0, usbCount = 0;
Options options;
ROMs roms;
TapePlayer* g_tapePlayer = 0;
ScreenLCD* screenLCD = 0;
InputMappings* inputMappings = 0;
static unsigned now = 0, reboots = 0;
static bool gpioPins[54] = {};
unsigned Pi1551NativeClock() { return now; }
SpinLock::SpinLock() : m_bLocked(false) {}
SpinLock::~SpinLock() {}
void SpinLock::Acquire() {}
void SpinLock::Release() {}
bool OledKernelMount(const char*) { return false; }
bool OledKernelUnmount() { return true; }
bool OledKernelPrepareReboot() {
    if (disk_ioctl(0,CTRL_SYNC,0)==RES_OK) return true;
    Pi1551Oled::KernelError("SD sync failed"); return false;
}
bool OledKernelSampleAvailable() { return true; }
bool OledKernelPreviewSound() { return true; }
void OledKernelServiceSoundPreview() {}
void OledKernelReset() {}
void OledKernelSelectROM(unsigned) {}
void OledKernelApplySettings() {}
bool OledKernelWriteImage() { return false; }
bool OledKernelNewImage(const char*) { return false; }
bool OledKernelWriteProtected() { return false; }
void OledKernelWriteProtect(bool) {}
oled::Status OledKernelStatus() { return oled::Status(); }
void Reboot_Pi() { ++reboots; }
extern "C" void RPI_SetGpioInputPullUp(rpi_gpio_pin_t) {}
extern "C" void RPI_SetGpioOutput(rpi_gpio_pin_t) {}
extern "C" void RPI_SetGpioHi(rpi_gpio_pin_t p) { gpioPins[p] = true; }
extern "C" void RPI_SetGpioLo(rpi_gpio_pin_t p) { gpioPins[p] = false; }
extern "C" rpi_gpio_value_t RPI_GetGpioValue(rpi_gpio_pin_t p) { return gpioPins[p] ? RPI_IO_HI : RPI_IO_LO; }
CEMMCDevice::CEMMCDevice() {}
CEMMCDevice::~CEMMCDevice() {}
bool CEMMCDevice::Initialize() { return initOK; }
int CEMMCDevice::DoRead(u8* out,size_t count,u32 block) {
    size_t pos = size_t(block)*512;
    if (readFails || pos+count > sectors.size()) return -1;
    memcpy(out,sectors.data()+pos,count); return count;
}
int CEMMCDevice::DoWrite(u8* in,size_t count,u32 block) {
    size_t pos = size_t(block)*512;
    if (writeFails || pos+count > sectors.size()) return -1;
    memcpy(sectors.data()+pos,in,count); ++writes; return count;
}
extern "C" int USPiMassStorageDeviceAvailable() { return usbCount; }
extern "C" int USPiMassStorageDeviceRead(unsigned long long,void*,unsigned,unsigned) { return -1; }
extern "C" int USPiMassStorageDeviceWrite(unsigned long long,const void*,unsigned,unsigned) { return -1; }
extern "C" DWORD get_fattime() { return 0; }
static void W16(unsigned pos,unsigned value) { sectors[pos]=value; sectors[pos+1]=value>>8; }
static void W32(unsigned pos,unsigned value) { for (unsigned i=0;i<4;++i) sectors[pos+i]=value>>(i*8); }
static void Format(bool fat32) {
    unsigned total = fat32 ? 131072 : 8192, reserved = fat32 ? 32 : 1, fatSize = fat32 ? 1024 : 32;
    sectors.assign(size_t(total)*512,0);
    sectors[0]=0xeb; sectors[1]=0x58; sectors[2]=0x90; memcpy(sectors.data()+3,"MSDOS5.0",8);
    W16(11,512); sectors[13]=1; W16(14,reserved); sectors[16]=2;
    W16(17,fat32?0:128); W16(19,fat32?0:total); sectors[21]=0xf8;
    W16(22,fat32?0:fatSize); W16(510,0xaa55);
    memcpy(sectors.data()+(fat32?82:54),fat32?"FAT32   ":"FAT16   ",8);
    if (fat32) {
        W32(32,total); W32(36,fatSize); W32(44,2); W16(48,1); W16(50,6);
        W32(512,0x41615252); W32(512+484,0x61417272);
        W32(512+488,0xffffffff); W32(512+492,3); W32(512+508,0xaa550000);
    }
    for (unsigned n=0;n<2;++n) {
        unsigned base = (reserved+n*fatSize)*512;
        if (fat32) { W32(base,0x0ffffff8); W32(base+4,0x0fffffff); W32(base+8,0x0fffffff); }
        else { W16(base,0xfff8); W16(base+2,0xffff); }
    }
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL storage:%d %s\n",__LINE__,#x); return 1; } } while(0)
static bool Click(const char* label) {
    oled::Menu& m = Pi1551Oled::TestMenu();
    m.Render(now);
    if (m.CurrentView()==oled::View::Notice) m.Input(oled::Key::Back);
    for (unsigned i=0;i<30;++i) m.Input(oled::Key::Left);
    for (unsigned i=0;i<30;++i) {
        oled::Frame f = m.Render(now);
        for (unsigned y=2;y<8;++y) if (f.inverse[y] && std::string(f.rows[y]+1).find(label)==0) {
            m.Input(oled::Key::Enter); return true;
        }
        m.Input(oled::Key::Right);
    }
    return false;
}
static std::string ReadFile(const char* path) {
    FIL file; if (f_open(&file,path,FA_READ)!=FR_OK) return std::string();
    std::string text; char buffer[256]; UINT count=0;
    while (f_read(&file,buffer,sizeof(buffer),&count)==FR_OK && count) text.append(buffer,count);
    f_close(&file); return text;
}
static void TapeHalfwave(TapePlayer& tape, unsigned us) {
    now += us; gpioPins[TAPE_WRITE_GPIO] = !gpioPins[TAPE_WRITE_GPIO]; tape.RecordSample(now);
}
static void TapePulse(TapePlayer& tape, unsigned us) { TapeHalfwave(tape, us); TapeHalfwave(tape, us); }
static void TapeBit(TapePlayer& tape, bool one) {
    TapePulse(tape, one ? 480 : 240); TapePulse(tape, one ? 240 : 480);
}
static void TapeByte(TapePlayer& tape, unsigned value) {
    TapePulse(tape, 960); TapePulse(tape, 480);
    unsigned parity = 1;
    for (unsigned bit=0; bit<8; ++bit) { unsigned one = (value >> bit) & 1; TapeBit(tape, one); parity ^= one; }
    TapeBit(tape, parity);
}
static void TapeHeader(TapePlayer& tape, const char* name, bool repeat) {
    for (unsigned i=0; i<64; ++i) TapePulse(tape, 240);
    for (unsigned i=0; i<9; ++i) TapeByte(tape, (repeat ? 0x09 : 0x89) - i);
    TapeByte(tape, 1);
    unsigned checksum = 1; unsigned char data[191]; memset(data, ' ', sizeof(data));
    for (unsigned i=0; i<16 && name[i]; ++i) data[i+4] = name[i];
    for (unsigned char c : data) { TapeByte(tape, c); checksum ^= c; }
    TapeByte(tape, checksum);
}
int main() {
    static_assert(sizeof(DWORD)==4,"FatFS DWORD must match ARM");
    CEMMCDevice card; unsigned char block[512] = {};
    disk_setEMM(&card); CHECK(disk_initialize(0)==0);
    // This assertion fails against the old all-RES_PARERR disk_ioctl.
    CHECK(disk_ioctl(0,CTRL_SYNC,0)==RES_OK);
    for (bool fat32 : {false,true}) {
        Format(fat32); FATFS fs = {};
        CHECK(f_mount(&fs,"SD:",1)==FR_OK); CHECK(fs.fs_type==(fat32?FS_FAT32:FS_FAT16));
        FIL file; UINT count = 0;
        const char original[] = "// keep order\r\ndeviceID = 8 // user comment\r\nROM2 = custom.bin\r\n";
        CHECK(f_open(&file,"/options.txt",FA_WRITE|FA_CREATE_NEW)==FR_OK);
        CHECK(f_write(&file,original,sizeof(original)-1,&count)==FR_OK);
        CHECK(f_sync(&file)==FR_OK); CHECK(f_close(&file)==FR_OK);
        CHECK(f_mkdir("/1551")==FR_OK);
        options.SetRuntime(oled::Setting::MotorAlways,0);
        gpioPins[TAPE_MOTOR_GPIO] = false; gpioPins[TAPE_WRITE_GPIO] = false; now = 1000;
        {
            TapePlayer tape;
            tape.InitializeGPIO();
            CHECK(tape.CreateTap("/1551/C16TEST.tap")); CHECK(tape.Record());
            tape.RecordSample(now);
            TapeHeader(tape,"FAT INDEX",false);
            gpioPins[TAPE_MOTOR_GPIO] = true; tape.RecordSample(++now);
            now += 100000; tape.Tick10ms(); CHECK(tape.IsLoaded());
            gpioPins[TAPE_MOTOR_GPIO] = false; tape.RecordSample(++now);
            TapeHeader(tape,"FAT INDEX",true);
            gpioPins[TAPE_MOTOR_GPIO] = true; tape.RecordSample(++now);
            now += 500001; tape.Tick10ms();
            CHECK(tape.Eject());
        }
        CHECK(ReadFile("/1551/C16TEST.idx").find("fat index") != std::string::npos);
        CHECK(f_mount(0,"SD:",0)==FR_OK); CHECK(f_mount(&fs,"SD:",1)==FR_OK);
        CHECK(ReadFile("/1551/C16TEST.idx").find("fat index") != std::string::npos);
        options.SetRuntime(oled::Setting::Device,8);
        Pi1551Oled::Initialize();
        CHECK(Click("Menu...")); CHECK(Click("Settings")); CHECK(Click("Drive")); CHECK(Click("Device number"));
        CHECK(options.GetDeviceID()==9); Pi1551Oled::TestMenu().Input(oled::Key::Back);
        CHECK(Click("Save settings")); CHECK(Pi1551Oled::TestMenu().Message()=="Settings saved");
        const char changed[] = "// keep order\r\ndeviceID = 9 // user comment\r\nROM2 = custom.bin\r\n";
        CHECK(ReadFile("/options-backup.txt") == original);
        CHECK(ReadFile("/options.txt") == changed);
        Pi1551Oled::TestMenu().Render(now); Pi1551Oled::TestMenu().Input(oled::Key::Back);
        options.SetRuntime(oled::Setting::Device,8);
        CHECK(Click("Load settings")); CHECK(options.GetDeviceID()==9);
        Pi1551Oled::TestMenu().Render(now); Pi1551Oled::TestMenu().Input(oled::Key::Back);
        CHECK(f_open(&file,"/options.txt.tmp",FA_WRITE|FA_CREATE_NEW)==FR_OK);
        CHECK(f_close(&file)==FR_OK);
        options.SetRuntime(oled::Setting::Device,8);
        CHECK(Click("Save settings")); CHECK(Pi1551Oled::TestMenu().Message()=="options.txt.tmp exists");
        CHECK(f_unlink("/options.txt.tmp")==FR_OK); options.SetRuntime(oled::Setting::Device,9);
        Pi1551Oled::TestMenu().Render(now); Pi1551Oled::TestMenu().Input(oled::Key::Back);
        Pi1551Oled::TestMenu().Input(oled::Key::Back);
        CHECK(Click("Reboot Pi1551")); CHECK(Click("Reboot Pi1551?")); CHECK(reboots==(fat32?2u:1u));
        disk_setEMM(0); CHECK(Click("Reboot Pi1551?")); CHECK(reboots==(fat32?2u:1u));
        CHECK(Pi1551Oled::TestMenu().Message()=="SD sync failed");
        disk_setEMM(&card); CHECK(disk_initialize(0)==0);
        Pi1551Oled::TestShutdown();
        CHECK(disk_ioctl(0,CTRL_SYNC,0)==RES_OK); // reboot's final storage barrier
        CHECK(f_mount(0,"SD:",0)==FR_OK); CHECK(f_mount(&fs,"SD:",1)==FR_OK);
        char result[128] = {};
        CHECK(f_open(&file,"/options.txt",FA_READ)==FR_OK);
        CHECK(f_read(&file,result,sizeof(result),&count)==FR_OK);
        CHECK(count==sizeof(changed)-1 && std::string(result,count)==changed);
        CHECK(f_close(&file)==FR_OK); CHECK(writes>0);
        readFails=true; CHECK(disk_read(0,block,0,1)==RES_ERROR); readFails=false;
        writeFails=true; CHECK(disk_write(0,block,0,1)==RES_ERROR); writeFails=false;
        CHECK(disk_ioctl(0,255,0)==RES_PARERR);
        CHECK(f_mount(0,"SD:",0)==FR_OK);
    }
    initOK=false; disk_setEMM(&card); CHECK(disk_initialize(0)&STA_NOINIT);
    CHECK(disk_ioctl(0,CTRL_SYNC,0)==RES_NOTRDY);
    disk_setEMM(0); CHECK(disk_ioctl(0,CTRL_SYNC,0)==RES_NOTRDY);
    CHECK(disk_ioctl(1,CTRL_SYNC,0)==RES_NOTRDY);
    usbCount=1; CHECK(disk_ioctl(1,CTRL_SYNC,0)==RES_OK);
    puts("PASS production FAT16/FAT32 + diskio: synced save/rename/remount, reboot barrier and real I/O errors");
}
