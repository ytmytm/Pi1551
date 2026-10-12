#include "Pi1551Oled.h"
#include "OledSettings.h"
#include "options.h"
#include "ScreenLCD.h"
#include "tape_player.h"
#include "ROMs.h"
#include "tcbm_bus.h"
#include "InputMappings.h"
#include "SpinLock.h"
#include "Pi1551Platform.h"
#include <algorithm>
#include <string.h>
#include <strings.h>
#include <stdio.h>
extern Options options;
extern ScreenLCD* screenLCD;
extern TapePlayer* g_tapePlayer;
extern ROMs roms;
extern InputMappings* inputMappings;
extern void Reboot_Pi();
extern bool OledKernelWriteImage();
extern bool OledKernelNewImage(const char* path);
extern bool OledKernelWriteProtected();
extern void OledKernelWriteProtect(bool value);
extern bool OledKernelPrepareReboot();
extern bool OledKernelSampleAvailable();

namespace Pi1551Oled {
static oled::Menu* menu = 0;
static SpinLock frameLock;
static oled::Frame frame;
static volatile bool frameReady = false;
static volatile unsigned readIndex = 0, writeIndex = 0;
static oled::Key events[64]; // single producer core0, single consumer core1
static bool pressed[3] = {};
static bool realTime = false;
static std::string pendingMount;
static oled::Menu* checkpoint = 0;
static bool pendingUnmount = false, pendingReboot = false;
static bool pendingBrowserReset = false, hostResetDown = false;
static volatile uint32_t lastAction = 0;
static std::string error;
static void Barrier() { Pi1551Barrier(); }
static void Enqueue(oled::Key key) {
    unsigned next = (writeIndex + 1) % 64;
    if (next == readIndex) return;
    events[writeIndex] = key; Barrier(); writeIndex = next;
}
static bool ReadText(const char* path, std::string& text) {
    FIL f; if (f_open(&f, path, FA_READ) != FR_OK) return false;
    char b[512]; UINT n = 0; text.clear(); bool ok = true;
    do {
        if (f_read(&f, b, sizeof(b), &n) != FR_OK) { ok = false; break; }
        text.append(b, n); if (text.size() > 65536) { ok = false; break; }
    } while (n == sizeof(b));
    if (f_close(&f) != FR_OK) ok = false;
    return ok;
}
static std::string Absolute(const std::string& name, const std::string& base) {
    if (!name.empty() && (name[0] == '/' || name.find(':') != std::string::npos)) return name;
    return base + (base == "/" ? "" : "/") + name;
}
static bool ValidMedia(const std::string& path) {
    FILINFO f; if (!oled::IsMedia(path) || f_stat(path.c_str(), &f) != FR_OK || (f.fattrib & AM_DIR)) return false;
    std::string ext = oled::Extension(path);
    if (ext == "D64") return f.fsize == 174848 || f.fsize == 175531 || f.fsize == 196608 || f.fsize == 197376 || f.fsize == 205312 || f.fsize == 206114;
    if (ext == "D71") return f.fsize == 349696 || f.fsize == 351062;
    if (ext == "D80") return f.fsize == 533248;
    if (ext == "D81") return f.fsize == 819200 || f.fsize == 822400;
    if (ext == "D82") return f.fsize == 1066496;
    if (ext == "PRG") return f.fsize >= 2;
    FIL file; if (f_open(&file, path.c_str(), FA_READ) != FR_OK) return false;
    unsigned char header[12]; UINT n = 0;
    bool ok = f_read(&file, header, sizeof(header), &n) == FR_OK && n == sizeof(header) &&
        !memcmp(header, "GCR-1541", 8) && header[8] == 0 && header[9] > 0 && header[9] <= 84 && f.fsize >= 12u + 8u * header[9];
    f_close(&file); return ok;
}
class KernelBackend : public oled::Backend {
    std::map<oled::Setting, int> baseline;
public:
    void CaptureBaseline() {
        for (unsigned i = 0; i < (unsigned)oled::Setting::Count; ++i)
            baseline[(oled::Setting)i] = Get((oled::Setting)i);
    }
    bool List(const std::string& path, std::vector<oled::Entry>& result) override {
        DIR d; if (f_opendir(&d, path.c_str()) != FR_OK) return false;
        FILINFO f; bool ok = true;
        for (;;) {
            if (f_readdir(&d, &f) != FR_OK) { ok = false; break; }
            if (!f.fname[0]) break;
            if (f.fattrib & (AM_HID | AM_SYS)) continue;
            result.push_back(oled::Entry(Absolute(f.fname, path), f.fname, (f.fattrib & AM_DIR) != 0));
        }
        f_closedir(&d);
        std::sort(result.begin(), result.end(), [](const oled::Entry& a, const oled::Entry& b) {
            return a.directory != b.directory ? a.directory : strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
        });
        return ok;
    }
    bool Directory(const std::string& path) override { return f_chdir(path.c_str()) == FR_OK; }
    bool Mount(const std::string& path) override {
        if (!ValidMedia(path)) return false;
        if (realTime) { pendingMount = path; pendingUnmount = false; return true; }
        return OledKernelMount(path.c_str());
    }
    bool Unmount() override {
        if (realTime) { pendingMount.clear(); pendingUnmount = true; return true; }
        return OledKernelUnmount();
    }
    bool ReadList(const std::string& path, std::vector<oled::Entry>& result) override {
        std::string text; if (!ReadText(path.c_str(), text)) return false;
        size_t at = 0;
        while (at < text.size()) {
            size_t end = text.find('\n', at); if (end == std::string::npos) end = text.size();
            std::string line = text.substr(at, end - at);
            size_t first = line.find_first_not_of(" \t\r");
            if (first != std::string::npos && line[first] != '#' && line[first] != ';' && line.compare(first, 2, "//") != 0) {
                line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
                if (line.size() > 1 && line[0] == '"' && line.back() == '"') line = line.substr(1, line.size() - 2);
                std::string p = Absolute(line, oled::Parent(path));
                if (!ValidMedia(p)) return false;
                bool duplicate = false;
                for (size_t i = 0; i < result.size(); ++i) if (result[i].path == p) duplicate = true;
                if (!duplicate) result.push_back(oled::Entry(p, oled::Basename(p)));
            }
            at = end + 1;
        }
        return true;
    }
    bool LoadTape(const std::string& path) override {
        error = "TAP load failed";
        FILINFO f; if (!g_tapePlayer || f_stat(path.c_str(), &f) != FR_OK || path.size() >= sizeof(f.fname)) return false;
        strcpy(f.fname, path.c_str()); return g_tapePlayer->LoadTap(&f);
    }
    void EjectTape() override { if (g_tapePlayer) g_tapePlayer->Eject(); }
    bool SeekTape(unsigned count) override { return g_tapePlayer && g_tapePlayer->SeekCounter(count); }
    oled::Status ReadStatus() override { return OledKernelStatus(); }
    std::vector<std::string> ROMs() override {
        std::vector<std::string> names;
        for (unsigned i = 0; i < ROMs::MAX_ROMS; ++i) if (roms.ROMValid[i]) names.push_back(roms.ROMNames[i]);
        return names;
    }
    bool SelectROM(unsigned index) override {
        for (unsigned i = 0; i < ROMs::MAX_ROMS; ++i) if (roms.ROMValid[i]) {
            if (!index--) { OledKernelSelectROM(i); return true; }
        }
        return false;
    }
    int Get(oled::Setting id) override { return options.Runtime(id); }
    bool Set(oled::Setting id, int v) override {
        error.clear();
        if (((id == oled::Setting::SoundMode && !v) ||
             (id == oled::Setting::Sound && v && !Get(oled::Setting::SoundMode))) && !OledKernelSampleAvailable()) {
            error = "Sample unavailable"; return false;
        }
        options.SetRuntime(id, v); OledKernelApplySettings(); return true;
    }
    std::string Error() override { return error.empty() ? "Operation failed" : error; }
    bool AutoMountAvailable() override {
        const char* name = options.GetAutoMountImageName(); if (!name[0]) return false;
        std::string path = Absolute(name, DEFAULT_BROWSE_DIR); FILINFO f;
        if (f_stat(path.c_str(), &f) != FR_OK || !f.fsize) return false;
        if (oled::Extension(path) == "LST") { std::vector<oled::Entry> entries; return ReadList(path, entries) && !entries.empty(); }
        return ValidMedia(path);
    }
    bool LoadSettings() override {
        std::string text;
        Options candidate = options;
        if (!ReadText("/options.txt", text) || !candidate.LoadRuntimeText(text)) return false;
        if (candidate.Runtime(oled::Setting::Sound) && !candidate.SoundOnGPIO() && !OledKernelSampleAvailable()) return false;
        for (unsigned i = 0; i < (unsigned)oled::Setting::Count; ++i)
            options.SetRuntime((oled::Setting)i, candidate.Runtime((oled::Setting)i));
        OledKernelApplySettings(); CaptureBaseline(); return true;
    }
    bool SaveSettings() override {
        std::string text; FILINFO f;
        FRESULT exists = f_stat("/options.txt", &f);
        if (exists != FR_OK && exists != FR_NO_FILE) return false;
        if (exists == FR_OK && !ReadText("/options.txt", text)) return false;
        std::map<oled::Setting,int> parsed, changes, defaults;
        if (!oled::ParseRuntimeSettings(text, parsed)) return false;
        Options bootDefaults;
        for (unsigned i = 0; i < (unsigned)oled::Setting::Count; ++i) {
            oled::Setting id = (oled::Setting)i;
            defaults[id] = bootDefaults.Runtime(id);
            if (Get(id) != baseline[id]) changes[id] = Get(id);
        }
        std::string updated = oled::MergeRuntimeSettings(text, changes, defaults);
        if (updated == text) { CaptureBaseline(); return true; }
        text = updated;
        // Never truncate the original: sync a new file, then retain it as a backup.
        if (f_stat("/options.txt.tmp", &f) == FR_OK || f_stat("/options.txt.bak", &f) == FR_OK) return false;
        FIL out; if (f_open(&out, "/options.txt.tmp", FA_WRITE | FA_CREATE_NEW) != FR_OK) return false;
        UINT n = 0; bool ok = f_write(&out, text.data(), text.size(), &n) == FR_OK && n == text.size();
        if (ok) ok = f_sync(&out) == FR_OK;
        if (f_close(&out) != FR_OK) ok = false;
        if (!ok) { f_unlink("/options.txt.tmp"); return false; }
        if (exists == FR_OK && f_rename("/options.txt", "/options.txt.bak") != FR_OK) { f_unlink("/options.txt.tmp"); return false; }
        if (f_rename("/options.txt.tmp", "/options.txt") != FR_OK) {
            if (exists == FR_OK && f_rename("/options.txt.bak", "/options.txt") == FR_OK)
                f_unlink("/options.txt.tmp");
            return false;
        }
        if (exists == FR_OK) f_unlink("/options.txt.bak");
        CaptureBaseline();
        return true;
    }
    bool WriteImage() override { return OledKernelWriteImage(); }
    bool NewImage(const std::string& path) override { return OledKernelNewImage(path.c_str()); }
    void ResetDrive() override { OledKernelReset(); }
    bool WriteProtected() override { return OledKernelWriteProtected(); }
    void WriteProtect(bool value) override { OledKernelWriteProtect(value); }
    bool Reboot() override {
        if (realTime) { pendingReboot = true; return true; }
        if (!OledKernelPrepareReboot()) return false;
        Reboot_Pi(); return true;
    }
};
static KernelBackend backend;
bool Enabled() { return menu != 0; }
void Initialize() {
    hostResetDown = false;
    pendingBrowserReset = false;
    lastAction = Pi1551Clock();
    backend.CaptureBaseline();
    menu = new oled::Menu(backend, DEFAULT_BROWSE_DIR);
    OledKernelApplySettings();
    if (backend.Get(oled::Setting::AutoMount) && backend.AutoMountAvailable())
        HostSelection(Absolute(options.GetAutoMountImageName(), DEFAULT_BROWSE_DIR).c_str());
    Service(false);
}
void PollInput() {
    if (!menu) return;
    ObserveHostReset(TCBM_Bus::PollGPIOUserInput1551());
    bool next = false, prev = false; TCBM_Bus::ConsumeRotaryDiskSteps(next, prev);
    TCBM_Bus::ClearRotarySynthesizedButtons();
    if (next) Enqueue(oled::Key::Right);
    if (prev) Enqueue(oled::Key::Left);
    const unsigned buttons[] = {InputMappings::INPUT_BUTTON_ENTER, InputMappings::INPUT_BUTTON_INSERT, InputMappings::INPUT_BUTTON_BACK};
    const oled::Key keys[] = {oled::Key::Enter, oled::Key::Insert, oled::Key::Back};
    for (unsigned i = 0; i < 3; ++i) {
        bool down = !TCBM_Bus::GetInputButtonReleased(buttons[i]);
        if (pressed[i] && !down) Enqueue(keys[i]);
        pressed[i] = down;
    }
}
void ObserveHostReset(bool asserted) {
    // REACT is handled by the existing protocol/emulation reset path. In IGNORE,
    // forward only the independently configured browser action, once per pulse.
    if (asserted && !hostResetDown && options.IgnoreReset() && options.Runtime(oled::Setting::ResetPath))
        Enqueue(oled::Key::ResetPath);
    hostResetDown = asserted;
}
bool TransitionPending() { return pendingUnmount || !pendingMount.empty() || pendingReboot || pendingBrowserReset; }
bool InputPending() { return readIndex != writeIndex; }
void CompleteTransition() {
    realTime = false;
    bool ok = true;
    if (pendingReboot) { pendingReboot = false; ok = backend.Reboot(); }
    else if (pendingUnmount) { pendingUnmount = false; ok = OledKernelUnmount(); }
    else if (!pendingMount.empty()) {
        std::string p = pendingMount; pendingMount.clear(); ok = OledKernelMount(p.c_str());
    }
    else if (pendingBrowserReset) { pendingBrowserReset = false; menu->ResetBrowserPath(); }
    if (!ok) {
        if (checkpoint) { delete menu; menu = checkpoint; checkpoint = 0; }
        menu->Notify("Media/write error");
    }
    delete checkpoint; checkpoint = 0;
}
void Service(bool emulating) {
    if (!menu) return;
    realTime = emulating;
    // While a medium transition is queued, leave the real-time loop before taking another event.
    while (readIndex != writeIndex && !TransitionPending()) {
        Barrier(); oled::Key key = events[readIndex]; readIndex = (readIndex + 1) % 64;
        if (realTime) { delete checkpoint; checkpoint = new oled::Menu(*menu); }
        lastAction = Pi1551Clock();
        if (key == oled::Key::ResetPath && realTime) pendingBrowserReset = true;
        else menu->Input(key);
        if (!TransitionPending()) { delete checkpoint; checkpoint = 0; }
    }
    static uint32_t next = 0; uint32_t now = Pi1551Clock();
    if ((int32_t)(now - next) < 0) return;
    next = now + 100000;
    oled::Frame fresh = menu->Render(now / 1000);
    frameLock.Acquire(); frame = fresh; frameReady = true; frameLock.Release();
}
void Draw() {
    if (!menu || !screenLCD || !frameReady) return;
    oled::Frame fresh; frameLock.Acquire(); fresh = frame; frameReady = false; frameLock.Release();
    bool dim = options.Runtime(oled::Setting::Dimming) &&
        options.I2CLcdDimTime() != 0 &&
        (uint32_t)(Pi1551Clock() - lastAction) / 1000000 >= options.I2CLcdDimTime();
    screenLCD->SetContrast(dim ? options.I2CLcdDimContrast() : std::max(1u, options.I2CLcdOnContrast()));
    screenLCD->SetPower(!dim || options.I2CLcdDimContrast() != 0);
    screenLCD->DrawMenu(fresh, options.Runtime(oled::Setting::Font), options.I2CLcdFlip());
}
void HostReset() {
    if (!menu) return;
    menu->HostReset();
    if (!backend.Get(oled::Setting::IgnoreReset) && backend.Get(oled::Setting::AutoMount) && backend.AutoMountAvailable())
        HostSelection(Absolute(options.GetAutoMountImageName(), DEFAULT_BROWSE_DIR).c_str());
}
void HostSelection(const char* name) {
    if (!menu || !name || !name[0]) return;
    char cwd[256]; if (f_getcwd(cwd, sizeof(cwd)) != FR_OK) return;
    std::string p = Absolute(name, cwd); menu->OpenMedia(oled::Entry(p, oled::Basename(p)));
}
void HostDirectory() {
    if (!menu) return;
    char cwd[256]; if (f_getcwd(cwd, sizeof(cwd)) != FR_OK) return;
    // The kernel already changed cwd for a host CD. Reflect it without re-entering containers.
    if (!menu->Mounted().empty()) menu->ShowHostBrowser(cwd);
    else if (menu->CurrentView() == oled::View::Browser) {
        // Updating the source through the same state-machine operation.
        menu->SetHostDirectory(cwd);
    }
}
std::string MountedPath() { return menu ? menu->Mounted() : ""; }
#ifdef PI1551_NATIVE_TEST
void TestInput(oled::Key key) { Enqueue(key); }
oled::Menu& TestMenu() { return *menu; }
void TestShutdown() { delete menu; menu = 0; delete checkpoint; checkpoint = 0; }
#endif
}
