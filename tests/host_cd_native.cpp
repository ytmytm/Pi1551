// Execute the real shared CD parser against a native filesystem and image layer.
#include "commands_base.h"
#include "cbm_diskimage.h"
#include <string>
#include <cstdio>
#include <cstring>
#include <vector>
unsigned versionMajor = 1, versionMinor = 26;
int numberOfUSBMassStorageDevices = 0;
static std::string switchedDrive;
void Reboot_Pi() {}
void SwitchDrive(const char* drive) { switchedDrive = drive; }
void DisplayMessage(int,int,bool,const char*,u32,u32) {}
extern "C" void SetACTLed(int) {}
extern "C" unsigned TimerGetTicks(void) { return 0; }
void FileBrowser::RefreshDevicesEntries(std::vector<BrowsableList::Entry>&,bool) {}
extern "C" FRESULT f_getfree(const TCHAR*,DWORD*,FATFS**) { return FR_DISK_ERR; }
extern void NativeFatFSRoot(const std::string& root);
struct CDProbe : Commands_Base {
    std::vector<u8> output;
    u32 ticks = 0;
    void SimulateIECBegin() override {}
    UpdateAction SimulateIECUpdate() override { return updateAction; }
    bool WriteSerialPortByte(u8 byte,bool) override { output.push_back(byte); return false; }
    void ReadBrowseMode() override {}
    bool IsReset() override { return false; }
    void Listen() override {}
    void Talk() override {}
    void LoadFile() override {}
    void SaveFile() override {}
    void LoadDirectory() override {}
    UpdateAction Change(const char* path) {
        char text[256]; strcpy(text,path); updateAction = NONE; CD(0,text); return updateAction;
    }
    void SetCommand(const char* text) {
        Channel& channel = channels[15];
        memset(channel.buffer, 0, sizeof(channel.buffer));
        channel.cursor = strlen(text);
        memcpy(channel.buffer, text, channel.cursor);
    }
    UpdateAction Folder(const char* text) {
        SetCommand(text); updateAction = NONE; FolderCommand();
        return updateAction;
    }
    UpdateAction ChangePartition(const char* text) {
        SetCommand(text); updateAction = NONE; ChangeDevice();
        return updateAction;
    }
    UpdateAction Execute(const char* text) {
        SetCommand(text); updateAction = NONE; ProcessCommand();
        return updateAction;
    }
    void Execute(const u8* data, size_t length) {
        Channel& channel = channels[15];
        memset(channel.buffer, 0, sizeof(channel.buffer));
        memcpy(channel.buffer, data, length); channel.cursor = length;
        updateAction = NONE; ProcessCommand();
    }
    bool OpenTestChannel(u8 number, const char* path) {
        Channel& channel = channels[number];
        channel.Close();
        channel.open = f_open(&channel.file, path, FA_READ) == FR_OK;
        channel.imageFile = false; channel.writing = false;
        return channel.open;
    }
    u32 TestChannelPosition(u8 number) const { return channels[number].file.fptr; }
    std::vector<u8> Listing(const char* query = "$") {
        output.clear(); SetCommand(query); Commands_Base::LoadDirectory(); return output;
    }
    std::vector<u8> Status() { output.clear(); SendError(); return output; }
    void AdvanceSeconds(u32 seconds) { ticks += seconds * 100; }
    u32 ClockTicks() const override { return ticks; }
};
static bool Contains(const std::vector<u8>& data, const char* text) {
    return std::search(data.begin(), data.end(), text, text + strlen(text)) != data.end();
}
static unsigned CountNear(const std::vector<u8>& data, const char* name, const char* type) {
    unsigned count = 0;
    for (size_t i = 0; i + strlen(name) < data.size(); ++i) {
        if (memcmp(&data[i], name, strlen(name))) continue;
        size_t end = std::min(data.size(), i + 32);
        for (size_t j = i; j + 3 <= end; ++j)
            if (!memcmp(&data[j], type, 3)) { ++count; break; }
    }
    return count;
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL host CD:%d %s\n",__LINE__,#x); return 1; } } while(0)
int main(int argc,char** argv) {
    CHECK(argc == 2); NativeFatFSRoot(argv[1]); CHECK(f_chdir("/1551") == FR_OK);
    CDProbe command{};
    CHECK(command.Change("..") == Commands_Base::POP_DIR);
    CHECK(command.Change("_") == Commands_Base::POP_DIR);
    CHECK(command.Change("/") == Commands_Base::DIR_PUSHED);
    char cwd[256]; CHECK(f_getcwd(cwd,sizeof(cwd)) == FR_OK); CHECK(std::string(cwd) == "/");
    CHECK(command.Change("//") == Commands_Base::DIR_PUSHED);
    CHECK(f_getcwd(cwd,sizeof(cwd)) == FR_OK); CHECK(std::string(cwd) == "/1551");
    CHECK(command.Change("A.D81") == Commands_Base::REFRESH); CHECK(cbm_image_is_mounted());
    CHECK(command.Change("..") == Commands_Base::POP_DIR);
    CHECK(cbm_image_is_mounted()); // retain the medium until kernel/UI flushes and commits exit
    CHECK(command.Change("_") == Commands_Base::POP_DIR); CHECK(cbm_image_is_mounted());
    CHECK(command.Change("SUB/") == Commands_Base::DIR_PUSHED); CHECK(cbm_image_is_mounted());
    cbm_image_unmount();
    CHECK(f_chdir("/1551") == FR_OK);
    CHECK(command.Change("A.D64") == Commands_Base::NONE);
    CHECK(std::string(command.GetNameOfImageSelected()) == "a.d64");
    CHECK(std::string(command.GetMountedDiskImagePath()) == "/1551/a.d64");
    CHECK(command.Folder("MD:newdir") == Commands_Base::REFRESH);
    FILINFO created{}; CHECK(f_stat("newdir", &created) == FR_OK); CHECK((created.fattrib & AM_DIR) != 0);
    CHECK(command.Folder("RD:newdir") == Commands_Base::REFRESH); CHECK(f_stat("newdir", &created) == FR_NO_FILE);
    CHECK(command.ChangePartition("CP0") == Commands_Base::DEVICE_SWITCHED); CHECK(switchedDrive == "SD:");
    CHECK(command.Execute("XS:list.lst") == Commands_Base::IMAGE_SELECTED);
    CHECK(std::string(command.GetSwapListPath()) == "list.lst");
    CHECK(std::string(command.GetNameOfImageSelected()) == "list.lst");
    command.ClearPendingImageSelection();
    CHECK(command.Execute("XS") == Commands_Base::SWAP_LIST_CLEARED);
    CHECK(std::string(command.GetSwapListPath()).empty());
    CHECK(command.OpenTestChannel(2, "a.d64"));
    const u8 position[] = {'P', 2, 0x34, 0x12}; command.Execute(position, sizeof(position));
    CHECK(command.TestChannelPosition(2) == 0x1234);
    CHECK(command.GetImageListingMode() == 1);
    std::vector<u8> listing = command.Listing(); CHECK(CountNear(listing, "A.D64", "DIR") == 1);
    command.Execute("XI0"); CHECK(command.GetImageListingMode() == 0);
    listing = command.Listing(); CHECK(CountNear(listing, "A.D64", "PRG") == 1);
    command.Execute("XI2"); listing = command.Listing();
    CHECK(CountNear(listing, "A.D64", "PRG") == 1); CHECK(CountNear(listing, "A.D64", "DIR") == 1);
    command.Execute("XI1");
    CHECK(Contains(command.Listing("$:SUB*=D"), "SUB"));
    CHECK(!Contains(command.Listing("$:SUB*=P"), "SUB"));
    FIL hidden{}; CHECK(f_open(&hidden, ".secret", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK); CHECK(f_close(&hidden) == FR_OK);
    CHECK(!Contains(command.Listing(), ".SECRET")); CHECK(Contains(command.Listing("$*=H"), ".SECRET"));
    CHECK(Contains(command.Listing("$=T"), "/"));
    numberOfUSBMassStorageDevices = 2;
    listing = command.Listing("$=P"); CHECK(Contains(listing, "SD")); CHECK(Contains(listing, "USB01")); CHECK(Contains(listing, "FAT"));
    listing = command.Listing("$=P:USB02"); CHECK(!Contains(listing, "USB01")); CHECK(Contains(listing, "USB02"));
    command.Execute("G-P"); std::vector<u8> status = command.Status();
    CHECK(status.size() == 31); CHECK(status[0] == 1); CHECK(status[2] == 0); CHECK(!memcmp(&status[3], "SD", 2));
    FIL typed{};
    CHECK(f_open(&typed, "alpha.prg", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK); CHECK(f_close(&typed) == FR_OK);
    CHECK(f_open(&typed, "alphabet.seq", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK); CHECK(f_close(&typed) == FR_OK);
    listing = command.Listing("$:A*.PRG"); CHECK(Contains(listing, "ALPHA.PRG")); CHECK(!Contains(listing, "ALPHABET.SEQ"));
    command.Execute("X*-"); CHECK(!command.GetWildcardMatchTail());
    listing = command.Listing("$:A*.PRG"); CHECK(Contains(listing, "ALPHA.PRG")); CHECK(Contains(listing, "ALPHABET.SEQ"));
    command.Execute("XE+"); CHECK(command.GetExtensionHiding());
    listing = command.Listing("$*=S"); CHECK(CountNear(listing, "ALPHABET", "SEQ") == 1); CHECK(!Contains(listing, ".SEQ"));
    command.Execute("X"); status = command.Status();
    CHECK(Contains(status, "I1:E+:*-,08,00"));
    command.Execute("XE-"); command.Execute("X*+");
    command.Execute("T-RI"); CHECK(Contains(command.Status(), "31,SYNTAX ERROR"));
    command.Execute("T-WI2024-02-29T23:59:58"); command.AdvanceSeconds(3); command.Execute("T-RI");
    status = command.Status(); CHECK(Contains(status, "2024-03-01T00:00:01 FRI\r"));
    command.Execute("T-RA"); CHECK(Contains(command.Status(), "FRI. 03/01/24 12:00:01 AM\r"));
    command.Execute("T-RD"); status = command.Status(); CHECK(status.size() == 9); CHECK(status[1] == 124); CHECK(status[2] == 3);
    command.Execute("T-RB"); status = command.Status(); CHECK(status.size() == 9); CHECK(status[1] == 0x24); CHECK(status[2] == 0x03);
    const u8 decimalTime[] = {'T','-','W','D', 0,125,12,31,11,59,58,1}; command.Execute(decimalTime, sizeof(decimalTime));
    command.Execute("T-RI"); CHECK(Contains(command.Status(), "2025-12-31T23:59:58 WED\r"));
    command.Execute("T-WAMON. 01/05/26 02:03:04 PM"); command.Execute("T-RI");
    CHECK(Contains(command.Status(), "2026-01-05T14:03:04 MON\r"));
    const u8 bcdTime[] = {'T','-','W','B', 0,0x27,0x07,0x04,0x12,0x34,0x56,0}; command.Execute(bcdTime, sizeof(bcdTime));
    command.Execute("T-RI"); CHECK(Contains(command.Status(), "2027-07-04T00:34:56 SUN\r"));
    puts("PASS production SD2IEC commands: CD/MD/RD/CP/XS/P/XI/filters/$=P/G-P/X*/XE/X/T-R/T-W");
}
