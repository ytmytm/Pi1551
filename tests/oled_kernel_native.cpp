#include "Pi1551Oled.h"
#include "options.h"
#include "ROMs.h"
#include "tape_player.h"
#include "ScreenLCD.h"
#include "SpinLock.h"
#include "InputMappings.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include "pi1551_sound.h"
using namespace oled;
Options options;
ROMs roms;
TapePlayer* g_tapePlayer = 0;
ScreenLCD* screenLCD = 0;
unsigned char* CBMFont = 0;
static std::vector<unsigned char> oledCommands;
extern "C" void RPI_I2CInit(int,int) {}
extern "C" int RPI_I2CWrite(int,unsigned char,void* buffer,unsigned length) {
    unsigned char* bytes = static_cast<unsigned char*>(buffer);
    if (length == 2 && bytes[0] == 0) oledCommands.push_back(bytes[1]);
    return length;
}
static int LastContrast() {
    for (size_t i = oledCommands.size(); i > 1; --i)
        if (oledCommands[i-2] == 0x81) return oledCommands[i-1];
    return -1;
}
InputMappings* inputMappings = 0;
static unsigned now = 0, resetCount = 0, rebootCount = 0, applies = 0;
static std::string medium;
static bool failUnmount = false, failMount = false, wp = false;
static bool sampleAvailable = true;
static unsigned previews = 0, previewEnd = 0;
static std::vector<unsigned> previewData(Pi1551SoundPreview::Frames);
bool OledKernelPreviewSound() {
    ++previews; previewEnd = now + 1000000;
    const unsigned char sample[] = {0,128,255};
    Pi1551SoundPreview::Fill(previewData.data(),options.Runtime(Setting::Sound),options.SoundOnGPIO(),
        options.SoundOnGPIODuration(),options.SoundOnGPIOFreq(),sample,3);
    return true;
}
void OledKernelServiceSoundPreview() { if (previewEnd && (int)(now-previewEnd) >= 0) previewEnd = 0; }
static bool pins[54];
unsigned Pi1551NativeClock() { return now; }
extern void NativeFatFSRoot(const std::string& root);
extern void NativeFatFSFail(const std::string& operation);
extern void NativeFatFSFailNth(const std::string& operation, unsigned n);
extern void NativeFatFSCwdPrefix(const std::string& prefix);
extern "C" void RPI_SetGpioInputPullUp(rpi_gpio_pin_t) {}
extern "C" void RPI_SetGpioOutput(rpi_gpio_pin_t) {}
extern "C" void RPI_SetGpioHi(rpi_gpio_pin_t p) { pins[p] = true; }
extern "C" void RPI_SetGpioLo(rpi_gpio_pin_t p) { pins[p] = false; }
extern "C" rpi_gpio_value_t RPI_GetGpioValue(rpi_gpio_pin_t p) { return pins[p] ? RPI_IO_HI : RPI_IO_LO; }
SpinLock::SpinLock() : m_bLocked(false) {}
SpinLock::~SpinLock() {}
void SpinLock::Acquire() {}
void SpinLock::Release() {}
bool OledKernelMount(const char* path) { if (failMount) return false; medium = path; return true; }
bool OledKernelUnmount() { if (failUnmount) return false; medium.clear(); return true; }
bool OledKernelPrepareReboot() { return OledKernelUnmount(); }
bool OledKernelSampleAvailable() { return sampleAvailable; }
void OledKernelReset() { ++resetCount; }
void OledKernelSelectROM(unsigned i) { roms.currentROMIndex = i; }
void OledKernelApplySettings() { ++applies; if (g_tapePlayer) g_tapePlayer->SetMotorAlwaysOn(options.TapeMotorAlwaysOn()); }
bool OledKernelWriteImage() { return !failUnmount && !medium.empty(); }
bool OledKernelNewImage(const char*) { return true; }
bool OledKernelWriteProtected() { return wp; }
void OledKernelWriteProtect(bool value) { wp = value; }
void Reboot_Pi() { ++rebootCount; }
Status OledKernelStatus() {
    Status s; s.emulating = IsEmulated(medium); s.rom = roms.GetSelectedROMName();
    if (g_tapePlayer) { TapeUIState t; g_tapePlayer->GetUIState(t); s.tape = t.isLoaded ? t.filename : ""; s.motor = t.motorActive; s.play = t.isPlaying; s.counter = t.tapeCounter; s.counterMax = t.maxTapeCounter; }
    return s;
}
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL bridge:" << __LINE__ << " " #x << "\n"; return 1; } } while(0)
static void Press(Key key,bool realTime = false) {
    now += 200000; Pi1551Oled::TestInput(key); Pi1551Oled::Service(realTime);
}
static void Choose(const std::string& text) {
    Menu& m = Pi1551Oled::TestMenu();
    m.Render(now/1000);
    if (m.CurrentView() == View::Notice) Press(Key::Back);
    unsigned initial = m.Cursor();
    while (initial--) Press(Key::Left);
    for (unsigned i = 0; i < 100; ++i) {
        Frame f = m.Render(now/1000);
        if (m.CurrentView() == View::Notice) { Press(Key::Back); continue; }
        for (unsigned row = 2; row < 8; ++row) if (f.inverse[row] && std::string(f.rows[row]+1).find(text) == 0) return;
        Press(Key::Right);
    }
    throw std::runtime_error("No row " + text);
}
static void Enter(const std::string& text) { Choose(text); Press(Key::Enter); }
static int MixedHostNavigation() {
    using namespace Pi1551Oled;
    Menu& m = TestMenu();
    CHECK(f_chdir("/1551") == FR_OK); CHECK(HostDirectory());
    Enter("Menu..."); Enter("Browser");
    for (const std::string& prefix : {std::string("SD:"),std::string("0:"),std::string("")}) {
        NativeFatFSCwdPrefix(prefix);
        CHECK(f_chdir("/1551/sub/deep") == FR_OK); CHECK(HostDirectory());
        CHECK(m.Path() == "/1551/sub/deep"); CHECK(m.CurrentView() == View::Browser);
        CHECK(HostParent()); CHECK(m.Path() == "/1551/sub");
        CHECK(HostParent()); CHECK(m.Path() == "/1551");
        HostSelection("a.d64"); CHECK(m.Mounted() == "/1551/a.d64");
        CHECK(m.CurrentView() == View::Fliplist); CHECK(m.Path() == "/1551");
        Service(true); // reproduce the stale realTime flag after Emulate1551 returns
        CHECK(HostParent()); CHECK(m.Path() == "/1551"); CHECK(m.Mounted().empty());
        CHECK(medium.empty()); CHECK(m.List().empty()); CHECK(!TransitionPending());
        CHECK(HostParent()); CHECK(m.Path() == "/");
        CHECK(HostParent()); CHECK(m.Path() == "/");
        CHECK(HostRoot()); CHECK(m.Path() == "/1551");
    }
    NativeFatFSCwdPrefix("SD:");
    for (const char* name : {"a.d81","a.prg","a.d64"}) {
        HostSelection(name); CHECK(!m.Mounted().empty());
        CHECK(HostParent()); CHECK(m.Mounted().empty()); CHECK(medium.empty());
        CHECK(m.Path() == "/1551"); CHECK(m.List().empty());
    }
    HostSelection("list.lst"); CHECK(m.ListMode()); CHECK(m.List().size() == 3);
    HostRefresh("SD:/1551/a.d81"); CHECK(m.Mounted() == "/1551/a.d81");
    CHECK(m.List().size() == 3); CHECK(m.CurrentView() == View::Fliplist);
    Enter("Menu..."); Enter("Settings"); Enter("Display");
    HostRefresh("0:/1551/a.d81"); CHECK(m.CurrentView() == View::Display); // no duplicate/remount
    HostRefresh("SD:/1551/A.D81"); CHECK(m.CurrentView() == View::Display);
    CHECK(f_chdir("/1551/sub/deep") == FR_OK); CHECK(HostDirectory());
    CHECK(m.CurrentView() == View::Browser); CHECK(medium.empty()); CHECK(m.List().size() == 3);
    Press(Key::Back); CHECK(m.Path() == "/1551/sub"); // no stale Display/Fliplist return
    HostSelection("SD:/1551/a.d81"); CHECK(m.Path() == "/1551");
    failUnmount = true; CHECK(!HostParent()); CHECK(m.Mounted() == medium); CHECK(!medium.empty());
    failUnmount = false; m.Render(now/1000); Press(Key::Back); CHECK(HostParent());
    CHECK(m.Path() == "/1551"); CHECK(m.Mounted().empty()); CHECK(m.List().size() == 3);
    CHECK(g_tapePlayer->IsLoaded()); // CD never ejects the independent tape
    Enter("Menu..."); Enter("Browser");
    CHECK(m.List().empty());
    Enter("Menu..."); Enter("Settings"); Enter("Drive"); HostRefresh();
    CHECK(m.CurrentView() == View::Drive); // a plain SD refresh does not steal menu focus
    NativeFatFSCwdPrefix("");
    std::cout << "PASS mixed host/panel navigation: SD parents, media exits, aliases, histories and failures\n";
    return 0;
}
static std::string Read(const std::string& path) { std::ifstream f(path); std::ostringstream s; s << f.rdbuf(); return s.str(); }
int main(int argc, char** argv) {
    CHECK(argc == 2); std::string root = argv[1]; NativeFatFSRoot(root);
    CHECK(options.Runtime(Setting::DimTime) == 16);
    CHECK(options.Runtime(Setting::DimLevel) == 16);
    CHECK(options.Runtime(Setting::Contrast) == 128);
    CHECK(options.Runtime(Setting::Freq) == 1200);
    CHECK(options.Runtime(Setting::Duration) == 1000);
    for (unsigned i = 0; i < ROMs::MAX_ROMS; ++i) roms.ROMValid[i] = false;
    roms.ROMValid[0] = true; roms.ROMValid[2] = true;
    strcpy(roms.ROMNames[0],"dos1551.bin"); strcpy(roms.ROMNames[2],"other.bin");
    options.SetRuntime(Setting::AutoMount,0); g_tapePlayer = new TapePlayer(); g_tapePlayer->InitializeGPIO();
    ScreenLCD panel; screenLCD = &panel;
    { char config[] = "i2cLcdOnContrast = 0\n"; options.Process(config); }
    CHECK(options.I2CLcdOnContrast() == 1); CHECK(options.Runtime(Setting::Contrast) == 1);
    options.SetRuntime(Setting::Contrast,127);
    options.SetRuntime(Setting::DimTime,30); options.SetRuntime(Setting::DimLevel,10);
    screenLCD->Open(128,64,1,1,0x3c,0,LCD_1306_128x64,false);
    Pi1551Oled::Initialize(); CHECK(Pi1551Oled::TestMenu().Path() == "/1551");
    now += 100000;
    Pi1551Oled::BeginLiveRender();
    oledCommands.clear();
    Pi1551Oled::Draw(); // core 0 renders and displays without core 1 service
    CHECK(!oledCommands.empty());
    oledCommands.clear(); now += 100000;
    Pi1551Oled::Draw(); CHECK(oledCommands.empty()); // unchanged live frame needs no I2C
    Pi1551Oled::TestInput(Key::Right);
    Pi1551Oled::Draw(); CHECK(Pi1551Oled::InputPending());
    Pi1551Oled::FinishLiveRender();
    Pi1551Oled::Service(false); CHECK(!Pi1551Oled::InputPending());
    oledCommands.clear(); now = 30100000; Pi1551Oled::Service(false); Pi1551Oled::Draw(); CHECK(LastContrast() == 127);
    oledCommands.clear(); now = 30200000; Pi1551Oled::Service(false); Pi1551Oled::Draw(); CHECK(LastContrast() == 10);
    for (unsigned char command : oledCommands) CHECK(command != 0xDB);
    Press(Key::Right); oledCommands.clear(); Pi1551Oled::Draw(); CHECK(LastContrast() == 127); // input wakes panel
    options.SetRuntime(Setting::DimTime,0); now += 40000000;
    oledCommands.clear(); Pi1551Oled::Service(false); Pi1551Oled::Draw(); CHECK(LastContrast() == 127);
    options.SetRuntime(Setting::DimTime,30); now += 200000;
    oledCommands.clear(); Pi1551Oled::Service(false); Pi1551Oled::Draw(); CHECK(LastContrast() == 10);
    options.SetRuntime(Setting::Dimming,0); now += 200000;
    oledCommands.clear(); Pi1551Oled::Service(false); Pi1551Oled::Draw(); CHECK(LastContrast() == 127);
    options.SetRuntime(Setting::Dimming,1);
    options.SetRuntime(Setting::DimLevel,0); now += 200000;
    oledCommands.clear(); Pi1551Oled::Service(false); Pi1551Oled::Draw();
    CHECK(std::find(oledCommands.begin(),oledCommands.end(),0xAE) != oledCommands.end());
    Press(Key::Left); oledCommands.clear(); Pi1551Oled::Draw();
    CHECK(std::find(oledCommands.begin(),oledCommands.end(),0xAF) != oledCommands.end());
    CHECK(LastContrast() == 127);
    options.SetRuntime(Setting::Contrast,0); CHECK(options.Runtime(Setting::Contrast) == 1);
    options.SetRuntime(Setting::Contrast,127); options.SetRuntime(Setting::DimLevel,10);
    options.SetRuntime(Setting::IgnoreReset,1);
    Menu& resetMenu = Pi1551Oled::TestMenu(); resetMenu.SetHostDirectory("/");
    unsigned resetsBefore = resetCount;
    Pi1551Oled::ObserveHostReset(true); now += 200000; Pi1551Oled::Service(false);
    CHECK(resetMenu.Path() == "/1551"); CHECK(resetCount == resetsBefore);
    resetMenu.SetHostDirectory("/");
    Pi1551Oled::ObserveHostReset(true); Pi1551Oled::Service(false);
    CHECK(resetMenu.Path() == "/"); // held RESET produces only one event
    Pi1551Oled::ObserveHostReset(false); options.SetRuntime(Setting::ResetPath,0);
    Pi1551Oled::ObserveHostReset(true); Pi1551Oled::Service(false); CHECK(resetMenu.Path() == "/");
    Pi1551Oled::ObserveHostReset(false); options.SetRuntime(Setting::ResetPath,1);
    Pi1551Oled::ObserveHostReset(true); Pi1551Oled::Service(true);
    CHECK(Pi1551Oled::TransitionPending()); CHECK(resetMenu.Path() == "/");
    Pi1551Oled::CompleteTransition(); CHECK(resetMenu.Path() == "/1551"); CHECK(resetCount == resetsBefore);
    Pi1551Oled::ObserveHostReset(false); options.SetRuntime(Setting::IgnoreReset,0);
    Enter("Menu..."); Enter("Settings"); Enter("Sound");
    int savedSound = options.Runtime(Setting::Sound), savedMode = options.Runtime(Setting::SoundMode);
    int savedFreq = options.Runtime(Setting::Freq), savedDuration = options.Runtime(Setting::Duration);
    options.SetRuntime(Setting::Sound,1); options.SetRuntime(Setting::SoundMode,1);
    Enter("Sound"); CHECK(previews == 1); CHECK(!options.Runtime(Setting::Sound));
    for (unsigned value : previewData) CHECK(value == 128);
    Enter("Sound"); CHECK(previews == 2);
    Choose("Mode"); Press(Key::Enter,true); CHECK(Pi1551Oled::TransitionPending()); CHECK(previews == 2);
    Pi1551Oled::CompleteTransition(); CHECK(previews == 3); CHECK(!options.SoundOnGPIO());
    Enter("Mode"); CHECK(previews == 4); CHECK(options.SoundOnGPIO());
    Enter("Freq"); CHECK(previews == 5);
    Enter("Duration"); CHECK(previews == 6);
    now += 1000000; Pi1551Oled::Service(false); CHECK(previewEnd == 0);
    options.SetRuntime(Setting::Sound,savedSound); options.SetRuntime(Setting::SoundMode,savedMode);
    options.SetRuntime(Setting::Freq,savedFreq); options.SetRuntime(Setting::Duration,savedDuration);
    Press(Key::Back); Press(Key::Back); Press(Key::Back);
    { char config[] = "AutoMountImage = bad.d64\n"; options.Process(config); }
    options.SetRuntime(Setting::AutoMount,1); Pi1551Oled::HostReset(); CHECK(medium.empty());
    Enter("Menu..."); Enter("Settings"); Enter("Browser");
    Frame unavailable = Pi1551Oled::TestMenu().Render(now/1000);
    for (unsigned row = 2; row < 8; ++row) CHECK(std::string(unavailable.rows[row]).find("Auto mount") == std::string::npos);
    Press(Key::Back, false); Press(Key::Back, false); Press(Key::Back, false);
    { char config[] = "AutoMountImage = a.d64\n"; options.Process(config); }
    Pi1551Oled::HostReset(); CHECK(IsEmulated(medium));
    options.SetRuntime(Setting::AutoMount,0);
    Pi1551Oled::HostSelection("/1551/pair.lst"); CHECK(IsEmulated(medium));
    unsigned resetsBeforeFlip = resetCount;
    Press(Key::Right,false); CHECK(!Pi1551Oled::TransitionPending()); // actual post-EXIT_KEYBOARD dispatcher path
    CHECK(medium == "/1551/a.d64"); // first row is already mounted
    Press(Key::Right,false);
    CHECK(!Pi1551Oled::TransitionPending()); CHECK(medium == "/1551/b.d64");
    CHECK(resetCount == resetsBeforeFlip); // fliplist navigation never invokes ResetDrive
    Press(Key::Left,true); CHECK(Pi1551Oled::TransitionPending()); Pi1551Oled::CompleteTransition();
    CHECK(medium == "/1551/a.d64"); // deferred real-time path also swaps media in place
    failUnmount = true; Press(Key::Enter,true); CHECK(Pi1551Oled::TransitionPending());
    Pi1551Oled::CompleteTransition(); CHECK(IsEmulated(medium)); CHECK(Pi1551Oled::TestMenu().CurrentView() == View::Fliplist);
    CHECK(Pi1551Oled::TestMenu().Mounted() == medium); // failed deferred unmount rolls back UI too
    Pi1551Oled::TestMenu().Render(now/1000); Press(Key::Back); failUnmount = false;
    Press(Key::Enter,true); Pi1551Oled::CompleteTransition(); CHECK(medium.empty()); CHECK(Pi1551Oled::TestMenu().List().size() == 2);
    Pi1551Oled::HostSelection("/1551/list.lst"); CHECK(Pi1551Oled::TestMenu().List().size() == 3);
    CHECK(Pi1551Oled::TestMenu().ListMode());
    Press(Key::Right); Press(Key::Right); CHECK(medium == "/1551/a.d81");
    Press(Key::Right); CHECK(medium == "/1551/a.prg");
    Enter("Menu..."); Enter("Browser"); CHECK(medium.empty());
    options.SetRuntime(Setting::Sound,1);
    Pi1551Oled::HostSelection("/1551/a.tap"); CHECK(g_tapePlayer->IsLoaded());
    CHECK(options.Runtime(Setting::Sound) == 1); // fixed buzzer pin is independent of tape READ
    Enter("Menu..."); Enter("Settings"); Enter("ROM"); Press(Key::Right); Press(Key::Enter); CHECK(roms.currentROMIndex == 2);
    Press(Key::Enter); Enter("Drive"); Enter("Device number"); CHECK(options.GetDeviceID() == 9); CHECK(applies >= 2);
    { std::ofstream f(root+"/options.txt",std::ios::binary); f << "// user order\r\nROM2 = do-not-touch.bin\r\n\tDeViCeID = 8  // drive\r\nunknown = keep\r\nidleSleep = 0 // externally edited"; }
    const std::string originalOptions = Read(root+"/options.txt");
    { std::ofstream f(root+"/options-backup.txt"); f << "stale backup"; }
    Press(Key::Back); Enter("Save settings");
    std::string saved = Read(root+"/options.txt");
    CHECK(saved == "// user order\r\nROM2 = do-not-touch.bin\r\n\tDeViCeID = 9  // drive\r\nunknown = keep\r\nidleSleep = 0 // externally edited");
    CHECK(Read(root+"/options-backup.txt") == originalOptions);
    Enter("Save settings"); CHECK(Read(root+"/options.txt") == saved);
    CHECK(saved.find("ROM2 = do-not-touch.bin") != std::string::npos); CHECK(saved.find("unknown = keep") != std::string::npos);
    CHECK(Read(root+"/options-backup.txt") == saved);
    Enter("Drive"); Enter("Device number"); CHECK(options.GetDeviceID() == 8); Press(Key::Back);
    NativeFatFSFail("write"); Enter("Save settings"); CHECK(Read(root+"/options.txt") == saved); CHECK(options.GetDeviceID() == 8);
    NativeFatFSFail("sync"); Enter("Save settings"); CHECK(Read(root+"/options.txt") == saved);
    NativeFatFSFail("rename"); Enter("Save settings"); CHECK(Read(root+"/options.txt") == saved);
    NativeFatFSFailNth("rename",2); Enter("Save settings"); CHECK(Read(root+"/options.txt") == saved);
    Enter("Load settings"); CHECK(options.GetDeviceID() == 9);
    CHECK(options.IdleSleep() == 0); // on-demand reload updates runtime values from the current file
    options.SetRuntime(Setting::Freq,1600); Enter("Save settings");
    const std::string savedWithFreq = saved + "\r\n\r\n// Sound frequency in Hz.\r\nsoundOnGPIOFreq = 1600\r\n";
    CHECK(Read(root+"/options.txt") == savedWithFreq);
    CHECK(Read(root+"/options-backup.txt") == saved);
    options.SetRuntime(Setting::Freq,1200); Enter("Save settings");
    const std::string savedWithFreq1200 = saved + "\r\n\r\n// Sound frequency in Hz.\r\nsoundOnGPIOFreq = 1200\r\n";
    CHECK(Read(root+"/options.txt") == savedWithFreq1200); // existing key stays in place
    CHECK(Read(root+"/options-backup.txt") == savedWithFreq);
    options.SetRuntime(Setting::Scroll,200); options.SetRuntime(Setting::Font,1);
    options.SetRuntime(Setting::Temp,1); options.SetRuntime(Setting::DimTime,60); options.SetRuntime(Setting::DimLevel,8);
    options.SetRuntime(Setting::Duration,250); options.SetRuntime(Setting::SoundMode,1); options.SetRuntime(Setting::NewType,1);
    Enter("Save settings"); std::string controls = Read(root+"/options.txt");
    CHECK(controls.find("scrollHighlightRate = 0.20") != std::string::npos);
    CHECK(controls.find("\r\n\r\n// Browser scroll interval in seconds.\r\nscrollHighlightRate = 0.20") != std::string::npos);
    CHECK(controls.find("i2cLcdUseCBMChar = 1") != std::string::npos); CHECK(controls.find("NewDiskType = g64") != std::string::npos);
    CHECK(controls.find("oledFont") == std::string::npos && controls.find("oledScroll") == std::string::npos);
    options.SetRuntime(Setting::TapeAutoplay,0); Enter("Save settings");
    controls = Read(root+"/options.txt");
    CHECK(controls.find("\r\n\r\n// Start tape playback automatically when a TAP is loaded.\r\ntapeAutoplay = 0") != std::string::npos);
    options.SetRuntime(Setting::Scroll,100); options.SetRuntime(Setting::Font,0); options.SetRuntime(Setting::NewType,0);
    options.SetRuntime(Setting::TapeAutoplay,1);
    Enter("Load settings"); CHECK(options.Runtime(Setting::Scroll) == 200); CHECK(options.Runtime(Setting::Font) == 1);
    CHECK(options.Runtime(Setting::TapeAutoplay) == 0);
    CHECK(options.GetNewDiskType() == DiskImage::G64); CHECK(options.DisplayTemperature() == 1);
    CHECK(options.SoundOnGPIO() == 1); CHECK(options.SoundOnGPIODuration() == 250);
    { std::ofstream f(root+"/options.txt"); f << "deviceID = 8\nsoundOnGPIO = 0\nSoundEnabled = 1\n"; }
    sampleAvailable = false; Enter("Load settings"); CHECK(options.GetDeviceID() == 9); CHECK(options.SoundOnGPIO() == 1);
    sampleAvailable = true;
    { std::ofstream f(root+"/options.txt"); f << "deviceID = wrong\n"; }
    Enter("Load settings"); CHECK(options.GetDeviceID() == 9);
    Choose("ROM"); Press(Key::Back); Enter("Reboot Pi1551"); CHECK(rebootCount == 0);
    Press(Key::Enter); CHECK(rebootCount == 0); Enter("Reboot Pi1551"); Press(Key::Right); Press(Key::Enter); CHECK(rebootCount == 1);
    CHECK(MixedHostNavigation() == 0);
    Pi1551Oled::TestShutdown(); delete g_tapePlayer; g_tapePlayer = 0;
    screenLCD = 0;
    std::string templateText = Read("sdcard/options.txt"); CHECK(!templateText.empty());
    Options templateOptions; std::vector<char> bootText(templateText.begin(),templateText.end()); bootText.push_back(0);
    templateOptions.Process(bootText.data());
    CHECK(templateOptions.Runtime(Setting::Scroll) == 100); CHECK(templateOptions.Runtime(Setting::Font) == 1);
    CHECK(templateOptions.DisplayTemperature() == 1); CHECK(templateOptions.SoundOnGPIO() == 1);
    CHECK(templateOptions.SoundOnGPIODuration() == 500); CHECK(templateOptions.GetNewDiskType() == DiskImage::D64);
    CHECK(templateOptions.LoadRuntimeText(templateText)); CHECK(templateOptions.Runtime(Setting::Scroll) == 100);
    CHECK(templateOptions.Runtime(Setting::Font) == 1); CHECK(templateOptions.SoundOnGPIO() == 1);
    CHECK(templateOptions.Runtime(Setting::Sound) == 1);
    char oldSoundOption[] = "oledSound = 0\n";
    templateOptions.Process(oldSoundOption); CHECK(templateOptions.Runtime(Setting::Sound) == 0);
    char newSoundOption[] = "SoundEnabled = 1\n";
    templateOptions.Process(newSoundOption); CHECK(templateOptions.Runtime(Setting::Sound) == 1);
    std::cout << "PASS firmware bridge: deferred effects, ROM slots, options save/load failures, reboot\n";
    return 0;
}
