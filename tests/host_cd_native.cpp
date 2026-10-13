// Execute the real shared CD parser against a native filesystem and image layer.
#include "commands_base.h"
#include "cbm_diskimage.h"
#include <string>
#include <cstdio>
#include <cstring>
unsigned versionMajor = 1, versionMinor = 26;
int numberOfUSBMassStorageDevices = 0;
static std::string switchedDrive;
void Reboot_Pi() {}
void SwitchDrive(const char* drive) { switchedDrive = drive; }
void DisplayMessage(int,int,bool,const char*,u32,u32) {}
extern "C" void SetACTLed(int) {}
void FileBrowser::RefreshDevicesEntries(std::vector<BrowsableList::Entry>&,bool) {}
extern "C" FRESULT f_getfree(const TCHAR*,DWORD*,FATFS**) { return FR_DISK_ERR; }
extern void NativeFatFSRoot(const std::string& root);
struct CDProbe : Commands_Base {
    void SimulateIECBegin() override {}
    UpdateAction SimulateIECUpdate() override { return updateAction; }
    bool WriteSerialPortByte(u8,bool) override { return true; }
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
};
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
    puts("PASS production SD2IEC commands: CD/MD/RD/CP/XS/P");
}
