#include "OledMenu.h"
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

namespace oled {
static std::string Number(unsigned n) { char b[24]; snprintf(b, sizeof(b), "%u", n); return b; }
std::string Basename(const std::string& p) { return p.substr(p.find_last_of('/') + 1); }
std::string Parent(const std::string& p) { size_t i = p.find_last_of('/'); return i == 0 || i == std::string::npos ? "/" : p.substr(0, i); }
std::string Extension(const std::string& p) {
    size_t i = p.find_last_of('.');
    std::string s = i == std::string::npos ? "" : p.substr(i + 1);
    for (size_t j = 0; j < s.size(); ++j) s[j] = toupper((unsigned char)s[j]);
    return s;
}
bool IsEmulated(const std::string& p) { std::string e = Extension(p); return e == "D64" || e == "G64"; }
bool IsMedia(const std::string& p) {
    std::string e = Extension(p);
    return IsEmulated(p) || e == "D71" || e == "D80" || e == "D81" || e == "D82" || e == "PRG";
}
Menu::Menu(Backend& b, const std::string& s) : backend(b), start(s), path(s) {
    if (!backend.Directory(path)) { path = "/"; backend.Directory(path); }
    Refresh();
}
void Menu::Refresh() {
    std::vector<Entry> entries;
    if (!backend.List(path, entries)) { message = "SD read failed"; return; }
    files.clear();
    if (path != "/") files.push_back(Entry(Parent(path), "..", true));
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].name != "." && entries[i].name != ".." &&
            (!tapeFilter || entries[i].directory || Extension(entries[i].path) == "TAP"))
            files.push_back(entries[i]);
    Clamp();
}
bool Menu::ChangeDirectory(const std::string& next) {
    if (!backend.Directory(next)) { message = "SD path failed"; return false; }
    path = next; cursor = 1; offset = 0; scrollReset = true; Refresh(); return true;
}
void Menu::Open(View next) {
    history.push_back(Position{view, cursor, offset});
    view = next; cursor = 1; offset = 0; editing = false; scrollReset = true; Clamp();
}
bool Menu::Browse(bool fresh, bool tape) {
    if (!backend.Unmount()) { message = "Write failed"; return false; }
    lastMounted = mounted.empty() ? lastMounted : mounted;
    mounted.clear();
    if (!listMode) flip.clear();
    if (view == View::Fliplist || tape) {
        browserReturn = Position{view, cursor, offset}; hasBrowserReturn = true;
    }
    std::string next = tape ? "/" : fresh ? start : path;
    if (!fresh && !tape && !lastMounted.empty() && !IsEmulated(lastMounted)) next = Parent(lastMounted);
    tapeFilter = tape; view = View::Browser; cursor = 1; offset = 0; editing = false;
    if (fresh) { history.clear(); hasBrowserReturn = false; }
    ChangeDirectory(next);
    if (!fresh && !tape && !lastMounted.empty() && !IsEmulated(lastMounted))
        for (unsigned i = 0; i < files.size(); ++i) if (files[i].path == lastMounted) cursor = i + 1;
    Clamp(); scrollReset = true; return true;
}
bool Menu::Activate(const Entry& entry) {
    if (!backend.Mount(entry.path)) { message = "Mount failed"; return false; }
    mounted = entry.path; lastMounted = mounted; return true;
}
bool Menu::OpenMedia(const Entry& e) {
    if (Extension(e.path) == "TAP") {
        if (!backend.LoadTape(e.path)) { message = backend.Error(); return false; }
        if (tapeFilter) {
            tapeFilter = false;
            if (hasBrowserReturn) {
                view = browserReturn.view; cursor = browserReturn.cursor; offset = browserReturn.offset;
                hasBrowserReturn = false; Clamp(); scrollReset = true;
            }
        }
        return true;
    }
    if (Extension(e.path) == "LST") {
        std::vector<Entry> loaded;
        if (!backend.ReadList(e.path, loaded)) { message = "LST load failed"; return false; }
        if (loaded.empty()) { Clear(); message = "Empty list"; return false; }
        if (!Activate(loaded[0])) return false;
        flip = loaded; listMode = true;
    } else if (IsMedia(e.path)) {
        if (!Activate(e)) return false;
        if (!listMode) flip.clear();
        bool found = false;
        for (size_t i = 0; i < flip.size(); ++i) if (flip[i].path == e.path) found = true;
        if (!found) flip.push_back(e);
    } else return false;
    view = View::Fliplist; cursor = 0; offset = 0; tapeFilter = false;
    history.clear(); hasBrowserReturn = false; scrollReset = true; return true;
}
void Menu::Toggle(const Entry& e) {
    listMode = true;
    for (size_t i = 0; i < flip.size(); ++i) if (flip[i].path == e.path) {
        flip.erase(flip.begin() + i); if (flip.empty()) listMode = false; return;
    }
    flip.push_back(e);
}
void Menu::Clear() {
    if (!backend.Unmount()) { message = "Write failed"; return; }
    flip.clear(); mounted.clear(); lastMounted.clear(); listMode = false; hasBrowserReturn = false;
    history.clear();
    if (view != View::Browser) history.push_back(Position{View::Browser, 1, 0});
}
void Menu::Back() {
    if (view == View::Notice) message.clear();
    if (editing) { editing = false; return; }
    if (view == View::Browser) {
        if (path != "/") { ChangeDirectory(Parent(path)); return; }
        if (hasBrowserReturn) {
            Position p = browserReturn;
            if (p.view == View::Fliplist) {
                if (flip.empty()) { hasBrowserReturn = false; return; }
                unsigned selected = 0;
                for (unsigned i = 0; i < flip.size(); ++i) if (flip[i].path == lastMounted) selected = i;
                if (!Activate(flip[selected])) return;
            }
            view = p.view; cursor = p.cursor; offset = p.offset; hasBrowserReturn = false;
            tapeFilter = false; Clamp(); scrollReset = true;
        }
    } else if (view == View::Fliplist) Browse();
    else if (!history.empty()) {
        Position p = history.back(); history.pop_back();
        view = p.view; cursor = p.cursor; offset = p.offset; Clamp(); scrollReset = true;
    }
}
void Menu::MountFailed() { mounted.clear(); view = View::Browser; cursor = 1; offset = 0; message = "Mount failed"; Refresh(); }
void Menu::HostReset() {
    if (backend.Get(Setting::IgnoreReset)) return;
    backend.ResetDrive();
    if (backend.Get(Setting::ResetPath)) ChangeDirectory(start);
}
static const char* On(bool b) { return b ? "ON" : "OFF"; }
std::vector<Menu::Row> Menu::Rows() {
    std::vector<Row> r;
    if (view == View::Browser || view == View::Fliplist) {
        std::string label = listMode ? "Menu... LIST" : "Menu...";
        if (view == View::Fliplist && listMode) label += flip.size() > 99 ? "99+" : " " + Number(flip.size());
        r.push_back(Row(label));
        const std::vector<Entry>& entries = view == View::Browser ? files : flip;
        for (unsigned i = 0; i < entries.size(); ++i) {
            const Entry& e = entries[i]; std::string prefix;
            if (view == View::Browser)
                for (unsigned j = 0; j < flip.size(); ++j) if (flip[j].path == e.path) prefix = Number(j + 1) + ">";
            r.push_back(Row(prefix, e.directory ? "[" + e.name + "]" : e.name,
                view == View::Fliplist && e.path == mounted));
        }
        return r;
    }
    r.push_back(Row(".."));
    switch (view) {
    case View::Device:
        if (!flip.empty()) r.push_back(Row("Clear list"));
        r.push_back(Row("Browser")); r.push_back(Row("Tape")); r.push_back(Row("Disk"));
        r.push_back(Row("Settings")); r.push_back(Row("Status")); r.push_back(Row("Reboot Pi1551")); break;
    case View::Settings: {
        const char* names[] = {"ROM", "Drive", "Tape", "Browser", "Display", "Encoder", "Sound", "Load settings", "Save settings"};
        for (unsigned i = 0; i < 9; ++i) r.push_back(Row(names[i]));
        break;
    }
    case View::ROM: {
        std::vector<std::string> roms = backend.ROMs(); Status s = backend.ReadStatus();
        for (size_t i = 0; i < roms.size(); ++i) r.push_back(Row("", Basename(roms[i]), roms[i] == s.rom));
        break;
    }
    case View::Tape: {
        Status s = backend.ReadStatus();
        r.push_back(Row("1:", Basename(s.tape))); r.push_back(Row("Load TAP..."));
        r.push_back(Row("Eject TAP")); r.push_back(Row("Rewind")); r.push_back(Row("Go to counter")); break;
    }
    case View::Seek: { char b[20]; snprintf(b, sizeof(b), "TARGET %03u", target); r.push_back(Row(b)); break; }
    case View::Disk:
        r.push_back(Row(std::string("WProt ") + On(backend.WriteProtected())));
        r.push_back(Row("Write image")); r.push_back(Row("New image")); r.push_back(Row("Reset drive")); break;
    case View::Reboot: r.push_back(Row("Cancel")); r.push_back(Row("Reboot Pi1551?")); break;
    case View::Notice: r.push_back(Row("", message)); break;
    case View::Status: {
        Status s = backend.ReadStatus();
        r.push_back(Row("TEMP " + Number(s.temperature) + "C")); r.push_back(Row("SD " + s.sdSize));
        r.push_back(Row("FREE " + s.sdFree)); r.push_back(Row("FW " + s.firmware));
        r.push_back(Row("DOS ", Basename(s.rom))); r.push_back(Row(mounted.empty() ? "IMG " : Extension(mounted) + " ", mounted.empty() ? "---" : Basename(mounted)));
        r.push_back(Row("TAP ", s.tape.empty() ? "---" : Basename(s.tape)));
        r.push_back(Row(std::string("LED ") + On(s.led))); r.push_back(Row(std::string("MOTOR ") + On(s.motor)));
        r.push_back(Row(std::string("PLAY ") + On(s.play)));
        char b[24]; snprintf(b, sizeof(b), "TRACK %u.%u%s", s.halfTrack / 2 + 1, s.halfTrack % 2 ? 5 : 0, s.idle ? "S" : "");
        r.push_back(Row(s.emulating ? b : "TRACK ---")); break;
    }
    default: {
        struct Item { Setting id; const char* label; };
        static const Item drive[] = {{Setting::Device,"Device number"},{Setting::Idle,"Idle trap"},{Setting::RAM,"RAMBOard"},{Setting::Spinup,"Skip spinup"},{Setting::IgnoreReset,"Reset"}};
        static const Item tape[] = {{Setting::MotorAlways,"Motor ON"}};
        static const Item browser[] = {{Setting::AutoMount,"Auto mount"},{Setting::ResetPath,"Reset path"},{Setting::Lowercase,"Lowercase"},{Setting::SD2IEC,"SD2IEC"}};
        static const Item display[] = {{Setting::Contrast,"Contrast"},{Setting::Dimming,"Dimming"},{Setting::Orient,"Orient"},{Setting::Font,"Font"},{Setting::Scroll,"Scroll"}};
        static const Item encoder[] = {{Setting::Reverse,"Reverse"}};
        static const Item sound[] = {{Setting::Sound,"Sound"},{Setting::GPIO,"GPIO"},{Setting::Freq,"Freq"}};
        const Item* items = drive; unsigned n = 5;
        if (view == View::TapeSettings) { items = tape; n = 1; }
        if (view == View::BrowserSettings) { items = browser; n = 4; }
        if (view == View::Display) { items = display; n = 5; }
        if (view == View::Encoder) { items = encoder; n = 1; }
        if (view == View::Sound) { items = sound; n = 3; }
        for (unsigned i = 0; i < n; ++i) {
            Setting id = items[i].id;
            if (id == Setting::AutoMount && !backend.AutoMountAvailable()) continue;
            int v = backend.Get(id); std::string value = On(v);
            if (id == Setting::Device || id == Setting::Contrast || id == Setting::GPIO || id == Setting::Freq) value = Number(v);
            if (id == Setting::Freq) value += " Hz";
            if (id == Setting::IgnoreReset) value = v ? "IGNORE" : "REACT";
            if (id == Setting::MotorAlways) value = v ? "ALWAYS" : "COMP.";
            if (id == Setting::Orient) value = v ? "flip" : "normal";
            if (id == Setting::Font) value = v ? "C64" : "ASCII";
            if (id == Setting::Scroll) value = v == 0 ? "slow" : v == 1 ? "medium" : "fast";
            r.push_back(Row(std::string(items[i].label) + " " + value));
        }
        break;
    }
    }
    return r;
}
void Menu::Clamp() {
    unsigned n = Rows().size(); if (cursor >= n) cursor = n ? n - 1 : 0;
    if (view == View::Browser || view == View::Fliplist) {
        if (cursor > 0 && cursor <= offset) offset = cursor - 1;
        if (cursor > offset + 5) offset = cursor - 5;
    } else {
        if (cursor < offset) offset = cursor;
        if (cursor >= offset + 6) offset = cursor - 5;
    }
}
Setting Menu::SelectedSetting() {
    static const Setting drive[] = {Setting::Device, Setting::Idle, Setting::RAM, Setting::Spinup, Setting::IgnoreReset};
    static const Setting display[] = {Setting::Contrast, Setting::Dimming, Setting::Orient, Setting::Font, Setting::Scroll};
    static const Setting sound[] = {Setting::Sound, Setting::GPIO, Setting::Freq};
    unsigned i = cursor ? cursor - 1 : 0;
    if (view == View::Drive) return drive[i];
    if (view == View::Display) return display[i];
    if (view == View::Sound) return sound[i];
    if (view == View::Encoder) return Setting::Reverse;
    if (view == View::TapeSettings) return Setting::MotorAlways;
    static const Setting browser[] = {Setting::AutoMount, Setting::ResetPath, Setting::Lowercase, Setting::SD2IEC};
    if (!backend.AutoMountAvailable()) ++i;
    return browser[i];
}
void Menu::ChangeValue(int delta) {
    Setting id = SelectedSetting(); int v = backend.Get(id);
    if (id == Setting::Device) v = v == 8 ? 9 : 8;
    else if (id == Setting::GPIO) v = v == 18 ? 19 : 18;
    else if (id == Setting::Contrast) v = v == 255 ? 32 : 255;
    else if (id == Setting::Scroll) v = (v + delta + 3) % 3;
    else if (id == Setting::Freq) {
        v = v == 1600 ? 1200 : v == 1200 ? 2000 : 1600;
    } else v = !v;
    if (!backend.Set(id, v)) message = backend.Error();
}
void Menu::Select() {
    if (cursor == 0) { if (view == View::Browser || view == View::Fliplist) Open(View::Device); else Back(); return; }
    if (view == View::Browser) {
        const Entry e = files[cursor - 1];
        if (e.directory) ChangeDirectory(e.path); else OpenMedia(e);
    } else if (view == View::Fliplist) Browse();
    else if (view == View::Device) {
        std::string name = Rows()[cursor].prefix;
        if (name == "Clear list") { Clear(); cursor = 1; Clamp(); }
        else if (name == "Browser") { Clear(); Browse(true); }
        else if (name == "Tape") Open(View::Tape);
        else if (name == "Disk") Open(View::Disk);
        else if (name == "Settings") Open(View::Settings);
        else if (name == "Status") Open(View::Status);
        else Open(View::Reboot);
    } else if (view == View::Settings) {
        static const View targets[] = {View::ROM, View::Drive, View::TapeSettings, View::BrowserSettings, View::Display, View::Encoder, View::Sound};
        if (cursor <= 7) Open(targets[cursor - 1]);
        else if (cursor == 8) message = backend.LoadSettings() ? "Settings loaded" : "Load failed";
        else message = backend.SaveSettings() ? "Settings saved" : "Save failed";
    } else if (view == View::Notice) Back();
    else if (view == View::ROM) {
        if (Rows()[cursor].active) Back();
        else if (!backend.SelectROM(cursor - 1)) message = "ROM failed";
    } else if (view == View::Reboot) {
        if (cursor == 1) Back(); else if (!backend.Reboot()) message = "Reboot failed";
    } else if (view == View::Tape) {
        if (cursor == 2) Browse(false, true);
        else if (cursor == 3) backend.EjectTape();
        else if (cursor == 4 || cursor == 5) {
            Status s = backend.ReadStatus();
            if (s.tape.empty()) message = "No TAP loaded";
            else if (cursor == 4) { if (!backend.SeekTape(0)) message = "Seek failed"; }
            else { target = s.counter; Open(View::Seek); }
        }
    } else if (view == View::Seek) { if (backend.SeekTape(target)) Back(); else message = "Seek failed"; }
    else if (view == View::Disk) {
        if (cursor == 1) backend.WriteProtect(!backend.WriteProtected());
        if (cursor == 2) message = backend.WriteImage() ? "Image written" : "Write failed";
        if (cursor == 3) { message = backend.NewImage(path) ? "Image created" : "Create failed"; Refresh(); }
        if (cursor == 4) backend.ResetDrive();
    } else if (view != View::Status) {
        ChangeValue(1);
    }
}
void Menu::Input(Key key) {
    scrollReset = true;
    if (key == Key::Back) { Back(); return; }
    if (key == Key::Enter) { Select(); Clamp(); return; }
    if (key == Key::Insert) {
        if (view == View::Browser) {
            if (cursor == 0) Open(View::Device);
            else { Entry e = files[cursor - 1]; if (IsMedia(e.path)) Toggle(e); else if (e.directory) ChangeDirectory(e.path); else OpenMedia(e); }
        } else if (view == View::Fliplist) { listMode = true; Browse(); }
        Clamp(); return;
    }
    int delta = key == Key::Left ? -1 : 1;
    if (view == View::Seek && cursor == 1) { target = std::max(0, std::min(999, (int)target + delta)); return; }
    if (editing) { ChangeValue(delta); return; }
    unsigned n = Rows().size();
    if (view == View::Fliplist && flip.empty()) { Browse(); return; }
    if (view == View::Fliplist) {
        if (!cursor && delta < 0) return;
        if (cursor == 1 && delta < 0) cursor = 0;
        else if (cursor == n - 1 && delta > 0) cursor = 1;
        else cursor += delta;
    } else cursor = std::max(0, std::min((int)n - 1, (int)cursor + delta));
    if (view == View::Fliplist && cursor) Activate(flip[cursor - 1]);
    Clamp();
}
Frame Menu::Render(uint32_t ms) {
    if (!message.empty() && view != View::Notice) Open(View::Notice);
    Frame f = {}; for (unsigned i = 0; i < 8; ++i) memset(f.rows[i], ' ', 16);
    if (scrollReset) { selectionTime = ms; scrollReset = false; }
    Status s = backend.ReadStatus(); char b[64];
    if (s.emulating) snprintf(b, sizeof(b), "%u: %u.%u%s", backend.Get(Setting::Device), s.halfTrack / 2 + 1, s.halfTrack % 2 ? 5 : 0, s.idle ? "S" : "");
    else snprintf(b, sizeof(b), "%u: BROWSER", backend.Get(Setting::Device));
    memcpy(f.rows[0], b, std::min((size_t)12, strlen(b)));
    snprintf(b, sizeof(b), "%02uC", std::min(99u, s.temperature)); memcpy(f.rows[0] + 13, b, 3);
    if (s.tape.empty()) snprintf(b, sizeof(b), "TAPE --- --%% %c-", s.motor ? 'M' : '-');
    else snprintf(b, sizeof(b), "TAPE %03u %02u%% %cP", s.counter % 1000, std::min(100u, s.percent), s.motor ? 'M' : '-');
    memcpy(f.rows[1], b, std::min((size_t)16, strlen(b)));
    std::vector<Row> rows = Rows();
    for (unsigned y = 2; y < 8; ++y) {
        unsigned i = offset + y - 2;
        if (view == View::Browser || view == View::Fliplist) i = y == 2 ? 0 : offset + y - 2;
        if (i >= rows.size()) continue;
        const Row& row = rows[i]; f.inverse[y] = i == cursor; f.active[y] = row.active;
        unsigned w = 15 - std::min(15u, (unsigned)row.prefix.size());
        unsigned pos = 0;
        if (i == cursor && row.name.size() > w) {
            unsigned max = row.name.size() - w;
            unsigned ticks = (ms - selectionTime) / (backend.Get(Setting::Scroll) == 0 ? 350 : backend.Get(Setting::Scroll) == 1 ? 200 : 100);
            unsigned cycle = max * 2 + 14; ticks %= cycle;
            if (ticks > 7 && ticks <= 7 + max) pos = ticks - 7;
            else if (ticks > 7 + max && ticks <= 14 + max) pos = max;
            else if (ticks > 14 + max) pos = cycle - ticks;
        }
        std::string text = row.prefix + row.name.substr(pos, w);
        memcpy(f.rows[y] + 1, text.data(), std::min((size_t)15, text.size()));
    }
    return f;
}
}
