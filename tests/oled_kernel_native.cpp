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
using namespace oled;
Options options;
ROMs roms;
TapePlayer* g_tapePlayer = 0;
ScreenLCD* screenLCD = 0;
InputMappings* inputMappings = 0;
static unsigned now = 0, resetCount = 0, rebootCount = 0, applies = 0;
static std::string medium;
static bool failUnmount = false, failMount = false, wp = false;
static bool sampleAvailable = true;
static bool pins[54];
unsigned Pi1551NativeClock() { return now; }
extern void NativeFatFSRoot(const std::string& root);
extern void NativeFatFSFail(const std::string& operation);
extern void NativeFatFSFailNth(const std::string& operation, unsigned n);
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
    if (g_tapePlayer) { TapeUIState t; g_tapePlayer->GetUIState(t); s.tape = t.isLoaded ? t.filename : ""; s.motor = t.motorActive; s.play = t.isLoaded; }
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
static std::string Read(const std::string& path) { std::ifstream f(path); std::ostringstream s; s << f.rdbuf(); return s.str(); }
int main(int argc, char** argv) {
    CHECK(argc == 2); std::string root = argv[1]; NativeFatFSRoot(root);
    for (unsigned i = 0; i < ROMs::MAX_ROMS; ++i) roms.ROMValid[i] = false;
    roms.ROMValid[0] = true; roms.ROMValid[2] = true;
    strcpy(roms.ROMNames[0],"dos1551.bin"); strcpy(roms.ROMNames[2],"other.bin");
    options.SetRuntime(Setting::AutoMount,0); g_tapePlayer = new TapePlayer(); g_tapePlayer->InitializeGPIO();
    Pi1551Oled::Initialize(); CHECK(Pi1551Oled::TestMenu().Path() == "/1551");
    { char config[] = "AutoMountImage = bad.d64\n"; options.Process(config); }
    options.SetRuntime(Setting::AutoMount,1); Pi1551Oled::HostReset(); CHECK(medium.empty());
    Enter("Menu..."); Enter("Settings"); Enter("Browser");
    Frame unavailable = Pi1551Oled::TestMenu().Render(now/1000);
    for (unsigned row = 2; row < 8; ++row) CHECK(std::string(unavailable.rows[row]).find("Auto mount") == std::string::npos);
    Press(Key::Back, false); Press(Key::Back, false); Press(Key::Back, false);
    { char config[] = "AutoMountImage = a.d64\n"; options.Process(config); }
    Pi1551Oled::HostReset(); CHECK(IsEmulated(medium));
    options.SetRuntime(Setting::AutoMount,0);
    Pi1551Oled::HostSelection("/1551/a.d64"); CHECK(IsEmulated(medium));
    Press(Key::Right,true); CHECK(Pi1551Oled::TransitionPending()); Pi1551Oled::CompleteTransition();
    failUnmount = true; Press(Key::Enter,true); CHECK(Pi1551Oled::TransitionPending());
    Pi1551Oled::CompleteTransition(); CHECK(IsEmulated(medium)); CHECK(Pi1551Oled::TestMenu().CurrentView() == View::Fliplist);
    CHECK(Pi1551Oled::TestMenu().Mounted() == medium); // failed deferred unmount rolls back UI too
    Pi1551Oled::TestMenu().Render(now/1000); Press(Key::Back); failUnmount = false;
    Press(Key::Enter,true); Pi1551Oled::CompleteTransition(); CHECK(medium.empty()); CHECK(Pi1551Oled::TestMenu().List().empty());
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
    Press(Key::Back); Enter("Save settings");
    std::string saved = Read(root+"/options.txt");
    CHECK(saved == "// user order\r\nROM2 = do-not-touch.bin\r\n\tDeViCeID = 9  // drive\r\nunknown = keep\r\nidleSleep = 0 // externally edited");
    Enter("Save settings"); CHECK(Read(root+"/options.txt") == saved);
    CHECK(saved.find("ROM2 = do-not-touch.bin") != std::string::npos); CHECK(saved.find("unknown = keep") != std::string::npos);
    Enter("Drive"); Enter("Device number"); CHECK(options.GetDeviceID() == 8); Press(Key::Back);
    NativeFatFSFail("write"); Enter("Save settings"); CHECK(Read(root+"/options.txt") == saved); CHECK(options.GetDeviceID() == 8);
    NativeFatFSFail("sync"); Enter("Save settings"); CHECK(Read(root+"/options.txt") == saved);
    NativeFatFSFail("rename"); Enter("Save settings"); CHECK(Read(root+"/options.txt") == saved);
    NativeFatFSFailNth("rename",2); Enter("Save settings"); CHECK(Read(root+"/options.txt") == saved);
    Enter("Load settings"); CHECK(options.GetDeviceID() == 9);
    CHECK(options.IdleSleep() == 0); // on-demand reload updates runtime values from the current file
    options.SetRuntime(Setting::Freq,1600); Enter("Save settings");
    CHECK(Read(root+"/options.txt") == saved + "\r\nsoundOnGPIOFreq = 1600\r\n");
    options.SetRuntime(Setting::Freq,1200); Enter("Save settings");
    CHECK(Read(root+"/options.txt") == saved + "\r\nsoundOnGPIOFreq = 1200\r\n"); // existing key stays in place
    options.SetRuntime(Setting::Scroll,200); options.SetRuntime(Setting::Font,1);
    options.SetRuntime(Setting::Temp,1); options.SetRuntime(Setting::DimTime,60); options.SetRuntime(Setting::DimLevel,8);
    options.SetRuntime(Setting::Duration,250); options.SetRuntime(Setting::SoundMode,1); options.SetRuntime(Setting::NewType,1);
    Enter("Save settings"); std::string controls = Read(root+"/options.txt");
    CHECK(controls.find("scrollHighlightRate = 0.20") != std::string::npos);
    CHECK(controls.find("i2cLcdUseCBMChar = 1") != std::string::npos); CHECK(controls.find("NewDiskType = g64") != std::string::npos);
    CHECK(controls.find("oledFont") == std::string::npos && controls.find("oledScroll") == std::string::npos);
    options.SetRuntime(Setting::Scroll,100); options.SetRuntime(Setting::Font,0); options.SetRuntime(Setting::NewType,0);
    Enter("Load settings"); CHECK(options.Runtime(Setting::Scroll) == 200); CHECK(options.Runtime(Setting::Font) == 1);
    CHECK(options.GetNewDiskType() == DiskImage::G64); CHECK(options.DisplayTemperature() == 1);
    CHECK(options.SoundOnGPIO() == 1); CHECK(options.SoundOnGPIODuration() == 250);
    { std::ofstream f(root+"/options.txt"); f << "deviceID = 8\nsoundOnGPIO = 0\noledSound = 1\n"; }
    sampleAvailable = false; Enter("Load settings"); CHECK(options.GetDeviceID() == 9); CHECK(options.SoundOnGPIO() == 1);
    sampleAvailable = true;
    { std::ofstream f(root+"/options.txt"); f << "deviceID = wrong\n"; }
    Enter("Load settings"); CHECK(options.GetDeviceID() == 9);
    Choose("ROM"); Press(Key::Back); Enter("Reboot Pi1551"); CHECK(rebootCount == 0);
    Press(Key::Enter); CHECK(rebootCount == 0); Enter("Reboot Pi1551"); Press(Key::Right); Press(Key::Enter); CHECK(rebootCount == 1);
    Pi1551Oled::TestShutdown(); delete g_tapePlayer; g_tapePlayer = 0;
    std::string templateText = Read("sdcard/options.txt"); CHECK(!templateText.empty());
    Options templateOptions; std::vector<char> bootText(templateText.begin(),templateText.end()); bootText.push_back(0);
    templateOptions.Process(bootText.data());
    CHECK(templateOptions.Runtime(Setting::Scroll) == 100); CHECK(templateOptions.Runtime(Setting::Font) == 1);
    CHECK(templateOptions.DisplayTemperature() == 1); CHECK(templateOptions.SoundOnGPIO() == 1);
    CHECK(templateOptions.SoundOnGPIODuration() == 500); CHECK(templateOptions.GetNewDiskType() == DiskImage::D64);
    CHECK(templateOptions.LoadRuntimeText(templateText)); CHECK(templateOptions.Runtime(Setting::Scroll) == 100);
    CHECK(templateOptions.Runtime(Setting::Font) == 1); CHECK(templateOptions.SoundOnGPIO() == 1);
    std::cout << "PASS firmware bridge: deferred effects, ROM slots, options save/load failures, reboot\n";
    return 0;
}
