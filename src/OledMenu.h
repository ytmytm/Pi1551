// Pi1551 OLED state machine. No GPIO, FatFS or display dependencies.
#ifndef PI1551_OLED_MENU_H
#define PI1551_OLED_MENU_H
#include <string>
#include <vector>
#include <stdint.h>

namespace oled {
struct ClockValue { unsigned year, month, day, hour, minute, second; };
unsigned DaysInMonth(unsigned year, unsigned month);
enum class Key { Left, Right, Enter, Insert, Back, ResetPath };
enum class View { Browser, Fliplist, Device, Tape, TapeIndex, Record, Seek, Disk, Settings,
    ROM, Drive, TapeSettings, BrowserSettings, Display, Encoder, Sound, DateTime, Status, Reboot, Notice, NewImage };
enum class Setting { Device, Idle, RAM, Spinup, IgnoreReset, MotorAlways, TapeAutoplay,
    AutoMount, ResetPath, Lowercase, SD2IEC, Contrast, Dimming, Orient, Font,
    Scroll, Reverse, Sound, Freq, Temp, DimTime, DimLevel, Duration, SoundMode, NewType, Count };
struct Entry {
    std::string path, name;
    bool directory;
    Entry(const std::string& p = "", const std::string& n = "", bool d = false)
        : path(p), name(n), directory(d) {}
};
struct TapeIndexEntry {
    uint32_t offset;
    std::string name;
    TapeIndexEntry(uint32_t o = 0, const std::string& n = "") : offset(o), name(n) {}
};
struct Status {
    bool emulating = false, idle = false, led = false, motor = false, play = false, record = false;
    unsigned halfTrack = 34, temperature = 0, counter = 0, counterMax = 999, percent = 0;
    uint32_t tapeIndexGeneration = 0;
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
    virtual bool ReadTapeIndex(const std::string& tap, std::vector<TapeIndexEntry>& entries) { (void)tap; (void)entries; return false; }
    virtual bool SeekTapeOffset(uint32_t offset) { (void)offset; return false; }
    virtual bool SetTapePlaying(bool playing) { (void)playing; return false; }
    virtual bool RecordTapeNew(const std::string& directory) { (void)directory; return false; }
    virtual bool RecordTapeExisting() { return false; }
    virtual bool EjectTape() = 0;
    virtual bool SeekTape(unsigned counter) = 0;
    // Seeking releases PLAY/RECORD; production backends flush changed TAP data first.
    virtual bool RewindTape() { return SetTapePlaying(false) && SeekTape(0); }
    virtual Status ReadStatus() = 0;
    virtual std::vector<std::string> ROMs() = 0;
    virtual bool SelectROM(unsigned slot) = 0;
    virtual int Get(Setting setting) = 0;
    virtual bool Set(Setting setting, int value) = 0;
    virtual bool PreviewSound() { return true; }
    virtual bool AutoMountAvailable() = 0;
    virtual bool LoadSettings() = 0;
    virtual bool SaveSettings() = 0;
    virtual bool ReadDateTime(ClockValue& value) { (void)value; return false; }
    virtual bool SetDateTime(const ClockValue& value) { (void)value; return false; }
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
bool ParseUltimateTapeIndex(const std::string& text, std::vector<TapeIndexEntry>& entries);

class Menu {
public:
    explicit Menu(Backend& backend, const std::string& start = "/1551");
    void Input(Key key);
    Frame Render(uint32_t milliseconds);
    void Refresh(); // host changes to SD directory
    bool OpenMedia(const Entry& entry); // host selection and automount use same rules
    bool OpenHostMedia(const Entry& entry);
    void HostReset();
    void ResetBrowserPath();
    void SetHostDirectory(const std::string& next) { ChangeDirectory(next); }
    bool ShowHostBrowser(const std::string& next);
    View CurrentView() const { return view; }
    unsigned Cursor() const { return cursor; }
    const std::vector<Entry>& List() const { return flip; }
    const std::string& Path() const { return path; }
    const std::string& Mounted() const { return mounted; }
    bool ListMode() const { return listMode; }
    const std::string& Message() const { return message; }
    void MountFailed();
    void DisableList();
    void Notify(const std::string& text) { message = text; }
private:
    struct Position { View view; unsigned cursor, offset; };
    struct Row {
        std::string prefix, name;
        bool active, fullWidth;
        Row(const std::string& p, const std::string& n = "", bool a = false, bool full = false)
            : prefix(p), name(n), active(a), fullWidth(full) {}
    };
    Backend& backend;
    std::string start, path, mounted, lastMounted, message;
    std::vector<Entry> files, flip;
    std::vector<TapeIndexEntry> tapeIndex;
    std::string indexedTape;
    uint32_t indexedGeneration = 0;
    std::vector<Position> history;
    Position browserReturn;
    bool hasBrowserReturn = false, listMode = false, tapeFilter = false, editing = false;
    View view = View::Browser;
    unsigned cursor = 1, offset = 0, target = 0, targetMax = 999;
    uint32_t selectionTime = 0;
    uint32_t headerTime = 0;
    std::string headerKey;
    bool scrollReset = true;
    ClockValue dateDraft = {2026, 1, 1, 0, 0, 0};
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
