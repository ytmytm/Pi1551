// Tests execute the production firmware state machine with observable kernel effects.
#include "OledMenu.h"
#include "OledSettings.h"
#include <map>
#include <stdexcept>
#include <iostream>
#include <cstring>
#include <cstdlib>
using namespace oled;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + " " #x); } while (0)
struct KernelStub : Backend {
    std::map<std::string,std::vector<Entry> > sd;
    std::map<Setting,int> settings;
    std::vector<std::string> effects;
    std::string cwd, medium;
    Status status;
    bool failMount = false, failUnmount = false, failTape = false, available = true, wp = false;
    int resets = 0, reboots = 0;
    std::vector<Entry> lst;
    KernelStub() {
        sd["/"] = {Entry("/1551","1551",true)};
        sd["/1551"] = {Entry("/1551/disks","disks",true), Entry("/1551/tapes","tapes",true), Entry("/1551/roms","roms",true), Entry("/1551/favorites.lst","favorites.lst")};
        sd["/1551/disks"] = {Entry("/1551/disks/a.d64","a.d64"), Entry("/1551/disks/b.g64","b.g64"), Entry("/1551/disks/c.d71","c.d71"), Entry("/1551/disks/d.d80","d.d80"), Entry("/1551/disks/e.d81","e.d81"), Entry("/1551/disks/f.d82","f.d82"), Entry("/1551/disks/g.prg","g.prg")};
        sd["/1551/tapes"] = {Entry("/1551/tapes/boulder dash.tap","boulder dash.tap"),Entry("/1551/tapes/info.txt","info.txt")};
        sd["/1551/roms"] = {Entry("/1551/roms/ignored.bin","ignored.bin")};
        settings[Setting::Device] = 8; settings[Setting::GPIO] = 18; settings[Setting::Freq] = 1200;
        settings[Setting::ResetPath] = 1; settings[Setting::Contrast] = 255; settings[Setting::Scroll] = 1;
        status.rom = "dos1551.bin"; status.firmware = "1.26"; status.temperature = 38;
        status.sdSize = "8192MB"; status.sdFree = "6400MB";
    }
    bool List(const std::string& p,std::vector<Entry>& e) override { if (!sd.count(p)) return false; e = sd[p]; return true; }
    bool Directory(const std::string& p) override { if (!sd.count(p)) return false; cwd = p; effects.push_back("source:"+p); return true; }
    bool Mount(const std::string& p) override {
        if (failMount) return false;
        medium = p; status.emulating = IsEmulated(p); effects.push_back(std::string(status.emulating ? "emulate:" : "container:")+p); return true;
    }
    bool Unmount() override { if (failUnmount) return false; effects.push_back("unmount:"+medium); medium.clear(); status.emulating = false; return true; }
    bool ReadList(const std::string&,std::vector<Entry>& e) override { e = lst; return true; }
    bool LoadTape(const std::string& p) override { if (failTape) return false; status.tape = p; status.play = true; status.counter = 0; effects.push_back("play:1"); return true; }
    void EjectTape() override { status.tape.clear(); status.play = false; effects.push_back("play:0"); }
    bool SeekTape(unsigned n) override { if (status.tape.empty()) return false; status.counter = n; effects.push_back("seek:"+std::to_string(n)); return true; }
    Status ReadStatus() override { return status; }
    std::vector<std::string> ROMs() override { return {"dos1551.bin","a very long alternative dos1551.bin"}; }
    bool SelectROM(unsigned i) override { status.rom = ROMs().at(i); effects.push_back("rom:"+status.rom); return true; }
    int Get(Setting s) override { return settings[s]; }
    bool Set(Setting s,int v) override { settings[s] = v; if (s == Setting::MotorAlways) status.motor = v; return true; }
    bool AutoMountAvailable() override { return available; }
    bool LoadSettings() override { effects.push_back("load-settings"); return true; }
    bool SaveSettings() override { effects.push_back("save-settings"); return true; }
    bool WriteImage() override { effects.push_back("write:"+medium); return !medium.empty(); }
    bool NewImage(const std::string& p) override { sd[p].push_back(Entry(p+"/NEWIMAGE1.d64","NEWIMAGE1.d64")); return true; }
    void ResetDrive() override { ++resets; effects.push_back("reset-drive"); }
    bool WriteProtected() override { return wp; }
    void WriteProtect(bool b) override { wp = b; }
    bool Reboot() override { ++reboots; effects.push_back("sync/reboot"); return true; }
};
static void Press(Menu& m, Key k, unsigned n = 1) { while(n--) m.Input(k); }
static void Choose(Menu& m, const std::string& text) {
    m.Render(0);
    if (m.CurrentView() == View::Notice) m.Input(Key::Back);
    unsigned initial = m.Cursor();
    while (initial--) m.Input(Key::Left);
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        Frame f = m.Render(0);
        if (m.CurrentView() == View::Notice) { m.Input(Key::Back); continue; }
        for (unsigned row = 2; row < 8; ++row)
            if (f.inverse[row] && std::string(f.rows[row]+1).find(text) == 0) return;
        m.Input(Key::Right);
    }
    throw std::runtime_error("No selectable row: " + text);
}
static void Enter(Menu& m,const std::string& text) { Choose(m,text); m.Input(Key::Enter); }
static void Device(Menu& m) { Enter(m,"Menu..."); CHECK(m.CurrentView() == View::Device); }
static Entry Image(const char* ext) { std::string n = std::string("a.")+ext; return Entry("/1551/disks/"+n,n); }
static void startup_and_source() {
    KernelStub k; Menu m(k);
    Frame f = m.Render(0); CHECK(m.Cursor() == 1); CHECK(std::string(f.rows[2]).find("Menu...") != std::string::npos);
    CHECK(std::string(f.rows[3]).find("[..]") != std::string::npos);
    CHECK(std::string(f.rows[0]) == "8: BROWSER   38C");
    Enter(m,"[disks]"); CHECK(k.cwd == "/1551/disks");
    Press(m,Key::Back); CHECK(k.cwd == "/1551");
    Press(m,Key::Back); CHECK(k.cwd == "/"); Press(m,Key::Back); CHECK(m.CurrentView() == View::Browser);
}
static void solo_all_types() {
    const char* types[] = {"d64","g64","d71","d80","d81","d82","prg"};
    for (const char* ext : types) {
        KernelStub k; Menu m(k); CHECK(m.OpenMedia(Image(ext)));
        CHECK(m.CurrentView() == View::Fliplist); CHECK(!m.ListMode()); CHECK(m.List().size() == 1); CHECK(m.Cursor() == 0);
        CHECK(k.status.emulating == IsEmulated(Image(ext).path));
        m.Input(Key::Right); CHECK(m.Cursor() == 1); CHECK(k.medium == Image(ext).path);
        m.Input(Key::Left); CHECK(m.Cursor() == 0);
        m.Input(Key::Right); m.Input(Key::Enter);
        CHECK(m.CurrentView() == View::Browser); CHECK(m.Mounted().empty()); CHECK(k.medium.empty()); CHECK(m.List().empty());
        CHECK(!m.ListMode());
        m.Input(Key::Back); m.Input(Key::Back); m.Input(Key::Back); CHECK(m.CurrentView() == View::Browser);
    }
}
static void mixed_list_toggle_and_restore() {
    KernelStub k; Menu m(k); Enter(m,"[disks]");
    Choose(m,"c.d71"); m.Input(Key::Insert);
    Choose(m,"e.d81"); m.Input(Key::Insert);
    CHECK(m.ListMode()); CHECK(m.List().size() == 2); CHECK(k.medium.empty());
    Choose(m,"2>e.d81"); m.Input(Key::Enter);
    CHECK(m.CurrentView() == View::Fliplist); CHECK(k.medium == "/1551/disks/e.d81"); CHECK(m.Cursor() == 0);
    m.Input(Key::Right); CHECK(k.medium == "/1551/disks/c.d71");
    m.Input(Key::Right); CHECK(k.medium == "/1551/disks/e.d81");
    m.Input(Key::Enter); CHECK(m.Path() == "/1551/disks"); CHECK(m.List().size() == 2); CHECK(k.medium.empty());
    Choose(m,"a.d64"); m.Input(Key::Enter); CHECK(m.List().size() == 3); CHECK(k.status.emulating);
    Press(m,Key::Right); CHECK(!k.status.emulating); CHECK(k.medium == "/1551/disks/c.d71");
    Press(m,Key::Enter); Choose(m,"1>c.d71"); Press(m,Key::Insert);
    CHECK(m.List().size() == 2); CHECK(m.List()[0].name == "e.d81");
    Press(m,Key::Back,3); CHECK(m.CurrentView() == View::Fliplist); CHECK(!k.medium.empty());
    Device(m); Enter(m,"Clear list"); CHECK(m.List().empty()); CHECK(k.medium.empty());
    Press(m,Key::Back); CHECK(m.CurrentView() == View::Browser);
}
static void insert_last_enter_and_empty_list() {
    KernelStub k; Menu m(k); Enter(m,"[disks]");
    Choose(m,"a.d64"); Press(m,Key::Insert); Choose(m,"e.d81"); Press(m,Key::Enter);
    CHECK(m.List().size() == 2); CHECK(m.ListMode()); CHECK(!k.status.emulating);
    Press(m,Key::Insert); CHECK(m.CurrentView() == View::Browser); CHECK(k.medium.empty());
    Choose(m,"1>a.d64"); Press(m,Key::Insert); Choose(m,"1>e.d81"); Press(m,Key::Insert);
    CHECK(m.List().empty()); CHECK(!m.ListMode());
    Press(m,Key::Back,2); CHECK(m.CurrentView() == View::Browser);
    CHECK(!m.OpenMedia(Entry("/1551/empty.lst","empty.lst"))); CHECK(!m.ListMode());
    k.lst = {Image("d81")}; CHECK(m.OpenMedia(Entry("/1551/list.lst","list.lst"))); CHECK(m.ListMode());
    Device(m); Enter(m,"Browser"); CHECK(m.Path() == "/1551"); CHECK(m.List().empty()); CHECK(!m.ListMode());
}
static void tape_independent_and_seek() {
    KernelStub k; Menu m(k); k.status.motor = true;
    CHECK(std::string(m.Render(0).rows[1]) == "TAPE --- --% M- ");
    CHECK(m.OpenMedia(Entry("/1551/tapes/boulder dash.tap","boulder dash.tap")));
    CHECK(k.status.play); CHECK(k.status.motor);
    CHECK(m.OpenMedia(Image("d64"))); CHECK(k.status.counter == 0);
    Device(m); Enter(m,"Tape"); CHECK(m.Cursor() == 1);
    Choose(m,"1:"); bool tail = false;
    for (unsigned ms = 0; ms < 16000; ms += 100) if (std::string(m.Render(ms).rows[3]).find("dash.tap") != std::string::npos) tail = true;
    CHECK(tail);
    Enter(m,"Go to counter"); CHECK(m.CurrentView() == View::Seek);
    Press(m,Key::Right,7); Press(m,Key::Enter); CHECK(k.status.counter == 7); CHECK(m.CurrentView() == View::Tape);
    Enter(m,"Go to counter"); Press(m,Key::Right,3); Press(m,Key::Back); CHECK(k.status.counter == 7);
    Enter(m,"Rewind"); CHECK(k.status.counter == 0);
    Enter(m,"Eject TAP"); CHECK(k.status.tape.empty()); CHECK(!k.status.play); CHECK(k.status.motor);
    Enter(m,"Load TAP..."); CHECK(k.medium.empty()); CHECK(m.CurrentView() == View::Browser);
    Enter(m,"[1551]"); Enter(m,"[tapes]"); Enter(m,"boulder dash"); CHECK(m.CurrentView() == View::Tape);
}
static void rom_markers_and_scrolling() {
    KernelStub k; Menu m(k); Device(m); Enter(m,"Settings"); Enter(m,"ROM");
    CHECK(m.Cursor() == 1); Frame f = m.Render(0); CHECK(f.active[3]); CHECK(f.inverse[3]);
    Press(m,Key::Right); f = m.Render(0); CHECK(f.active[3]); CHECK(f.inverse[4]); CHECK(!f.active[4]);
    Press(m,Key::Enter); CHECK(m.CurrentView() == View::ROM); f = m.Render(0); CHECK(f.active[4]); CHECK(!f.active[3]);
    bool tail = false; for (unsigned ms = 0; ms < 30000; ms += 100) if (std::string(m.Render(ms).rows[4]).find("dos1551.bin") != std::string::npos) tail = true;
    CHECK(tail); Press(m,Key::Enter); CHECK(m.CurrentView() == View::Settings);
    Press(m,Key::Back,2); Enter(m,"[roms]"); Enter(m,"ignored.bin"); CHECK(k.status.rom == k.ROMs()[1]); CHECK(m.CurrentView() == View::Browser);
}
static void failures_and_reboot() {
    KernelStub k; Menu m(k); k.failMount = true;
    CHECK(!m.OpenMedia(Image("d64"))); CHECK(m.List().empty()); CHECK(m.CurrentView() == View::Browser);
    k.failMount = false; CHECK(m.OpenMedia(Image("d64"))); k.failUnmount = true;
    Press(m,Key::Right); Press(m,Key::Enter); CHECK(m.CurrentView() == View::Fliplist); CHECK(!m.Mounted().empty());
    k.failUnmount = false; Device(m); Enter(m,"Reboot Pi1551"); CHECK(m.CurrentView() == View::Reboot); CHECK(m.Cursor() == 1);
    Press(m,Key::Enter); CHECK(k.reboots == 0); CHECK(m.CurrentView() == View::Device);
    Enter(m,"Reboot Pi1551"); Press(m,Key::Right); Press(m,Key::Enter); CHECK(k.reboots == 1);
}
static void reset_and_settings() {
    KernelStub k; Menu m(k); Enter(m,"[disks]"); CHECK(m.OpenMedia(Image("d64")));
    k.settings[Setting::IgnoreReset] = 1; m.HostReset(); CHECK(k.resets == 0);
    k.settings[Setting::IgnoreReset] = 0; m.HostReset(); CHECK(k.resets == 1); CHECK(m.Path() == "/1551"); CHECK(!m.Mounted().empty());
    Device(m); Enter(m,"Settings"); Enter(m,"Drive"); Enter(m,"Device number"); CHECK(k.settings[Setting::Device] == 9);
    Enter(m,"Skip spinup"); CHECK(k.settings[Setting::Spinup] == 1);
    Press(m,Key::Back); Enter(m,"Browser"); k.available = false;
    Frame f = m.Render(0); for (unsigned row = 2; row < 8; ++row) CHECK(std::string(f.rows[row]).find("Auto mount") == std::string::npos);
    Choose(m,"Reset path"); Press(m,Key::Enter); CHECK(k.settings[Setting::ResetPath] == 0);
    Press(m,Key::Back); Enter(m,"Sound"); Enter(m,"GPIO"); CHECK(k.settings[Setting::GPIO] == 19);
    Press(m,Key::Back); Enter(m,"Save settings"); CHECK(k.effects.back() == "save-settings");
}
static void persistence_preserves_unknown() {
    std::string input = "// keep comment\nROM2 = custom.bin\ndeviceID = 8 // drive\nrotaryEncoderEnable = 1\nunknown = untouched\nidleSleep = 1\n";
    std::map<Setting,int> values; CHECK(ParseRuntimeSettings(input,values)); CHECK(values[Setting::Device] == 8);
    values[Setting::Device] = 9; values[Setting::SD2IEC] = 1;
    std::string out = MergeRuntimeSettings(input,values);
    CHECK(out.find("ROM2 = custom.bin") != std::string::npos); CHECK(out.find("unknown = untouched") != std::string::npos);
    CHECK(out.find("deviceID = 9 // drive") != std::string::npos); CHECK(out.find("disableSD2IECCommands = 0") != std::string::npos);
    CHECK(ParseRuntimeSettings(out,values)); CHECK(values[Setting::Device] == 9); CHECK(values[Setting::SD2IEC] == 1);
    CHECK(!ParseRuntimeSettings("deviceID = invalid\n",values)); CHECK(values[Setting::Device] == 9);
    CHECK(!ParseRuntimeSettings("deviceID = 10\n",values));
    CHECK(!ParseRuntimeSettings("soundOnGPIOFreq = 0\n",values));
    CHECK(!ParseRuntimeSettings("i2cLcdOnContrast = 999\n",values));
}
static void random_navigation() {
    KernelStub k; Menu m(k); srand(1551);
    for (unsigned i = 0; i < 20000; ++i) {
        m.Input((Key)(rand()%5)); Frame f = m.Render(i * 200);
        for (unsigned row = 0; row < 8; ++row) CHECK(strlen(f.rows[row]) == 16);
        if (m.CurrentView() == View::Browser) CHECK(k.medium.empty());
        if (m.CurrentView() == View::Fliplist) CHECK(!m.List().empty());
        CHECK(m.Mounted() == k.medium);
    }
}
int main() {
    struct Test { const char* name; void (*run)(); };
    Test tests[] = {{"startup/source",startup_and_source},{"solo all media",solo_all_types},{"mixed list",mixed_list_toggle_and_restore},{"list construction",insert_last_enter_and_empty_list},{"independent tape",tape_independent_and_seek},{"ROM markers/scroll",rom_markers_and_scrolling},{"failures/reboot",failures_and_reboot},{"reset/settings",reset_and_settings},{"settings document",persistence_preserves_unknown},{"random graph navigation",random_navigation}};
    unsigned failures = 0;
    for (const Test& t : tests) try { t.run(); std::cout << "PASS " << t.name << "\n"; } catch (const std::exception& e) { ++failures; std::cerr << "FAIL " << t.name << ": " << e.what() << "\n"; }
    return failures ? 1 : 0;
}
