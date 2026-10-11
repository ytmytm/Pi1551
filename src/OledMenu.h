// Pi1551 OLED state machine. No GPIO, FatFS or display dependencies.
#ifndef PI1551_OLED_MENU_H
#define PI1551_OLED_MENU_H
#include <string>
#include <vector>
#include <stdint.h>

namespace oled {
enum class Key { Left, Right, Enter, Insert, Back };
enum class View { Browser, Fliplist, Device, Tape, Seek, Disk, Settings,
    ROM, Drive, TapeSettings, BrowserSettings, Display, Encoder, Sound, Status, Reboot, Notice, NewImage };
enum class Setting { Device, Idle, RAM, Spinup, IgnoreReset, MotorAlways,
    AutoMount, ResetPath, Lowercase, SD2IEC, Contrast, Dimming, Orient, Font,
    Scroll, Reverse, Sound, Freq, Temp, DimTime, DimLevel, Duration, SoundMode, NewType, Count };
struct Entry {
    std::string path, name;
    bool directory;
    Entry(const std::string& p = "", const std::string& n = "", bool d = false)
        : path(p), name(n), directory(d) {}
};
struct Status {
    bool emulating = false, idle = false, led = false, motor = false, play = false;
    unsigned halfTrack = 34, temperature = 0, counter = 0, percent = 0;
    std::string tape, rom, sdSize, sdFree, firmware;
};
struct Frame {
    char rows[8][17];
    bool inverse[8];
    bool active[8]; // independent triangle marker in column zero
};
class Backend {
public:
    virtual ~Backend() {}
    virtual bool List(const std::string& path, std::vector<Entry>& entries) = 0;
    virtual bool Directory(const std::string& path) = 0;
    virtual bool Mount(const std::string& path) = 0;
    virtual bool Unmount() = 0;
    virtual bool ReadList(const std::string& path, std::vector<Entry>& entries) = 0;
    virtual bool LoadTape(const std::string& path) = 0;
    virtual void EjectTape() = 0;
    virtual bool SeekTape(unsigned counter) = 0;
    virtual Status ReadStatus() = 0;
    virtual std::vector<std::string> ROMs() = 0;
    virtual bool SelectROM(unsigned slot) = 0;
    virtual int Get(Setting setting) = 0;
    virtual bool Set(Setting setting, int value) = 0;
    virtual bool AutoMountAvailable() = 0;
    virtual bool LoadSettings() = 0;
    virtual bool SaveSettings() = 0;
    virtual bool WriteImage() = 0;
    virtual bool NewImage(const std::string& directory) = 0;
    virtual void ResetDrive() = 0;
    virtual bool WriteProtected() = 0;
    virtual void WriteProtect(bool value) = 0;
    virtual bool Reboot() = 0;
    virtual std::string Error() { return "Operation failed"; }
};
bool IsMedia(const std::string& path);
bool IsEmulated(const std::string& path);
std::string Extension(const std::string& path);
std::string Basename(const std::string& path);
std::string Parent(const std::string& path);

class Menu {
public:
    explicit Menu(Backend& backend, const std::string& start = "/1551");
    void Input(Key key);
    Frame Render(uint32_t milliseconds);
    void Refresh(); // host changes to SD directory
    bool OpenMedia(const Entry& entry); // host selection and automount use same rules
    void HostReset();
    void SetHostDirectory(const std::string& next) { ChangeDirectory(next); }
    void ShowHostBrowser(const std::string& next) { if (Browse()) ChangeDirectory(next); }
    View CurrentView() const { return view; }
    unsigned Cursor() const { return cursor; }
    const std::vector<Entry>& List() const { return flip; }
    const std::string& Path() const { return path; }
    const std::string& Mounted() const { return mounted; }
    bool ListMode() const { return listMode; }
    const std::string& Message() const { return message; }
    void MountFailed();
    void Notify(const std::string& text) { message = text; }
private:
    struct Position { View view; unsigned cursor, offset; };
    struct Row {
        std::string prefix, name;
        bool active;
        Row(const std::string& p, const std::string& n = "", bool a = false)
            : prefix(p), name(n), active(a) {}
    };
    Backend& backend;
    std::string start, path, mounted, lastMounted, message;
    std::vector<Entry> files, flip;
    std::vector<Position> history;
    Position browserReturn;
    bool hasBrowserReturn = false, listMode = false, tapeFilter = false, editing = false;
    View view = View::Browser;
    unsigned cursor = 1, offset = 0, target = 0;
    uint32_t selectionTime = 0;
    bool scrollReset = true;
    std::vector<Row> Rows();
    void Open(View next);
    void Back();
    bool Browse(bool fresh = false, bool tape = false);
    bool ChangeDirectory(const std::string& next);
    bool Activate(const Entry& entry);
    void Select();
    void Toggle(const Entry& entry);
    void Clear();
    void ChangeValue(int delta);
    Setting SelectedSetting();
    void Clamp();
};
}
#endif
