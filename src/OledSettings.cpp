#include "OledSettings.h"
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
namespace oled {
const char* OptionName(Setting id) {
    static const char* names[] = {"deviceID", "idleSleep", "RAMBOard", "skipMotorSpinUpDelay", "ignoreReset", "tapeMotorAlwaysOn", "tapeAutoplay", "autoMountEnabled", "onResetChangeToStartingFolder", "lowercaseBrowseModeFilenames", "disableSD2IECCommands", "i2cLcdOnContrast", "oledDimming", "i2cLcdFlip", "i2cLcdUseCBMChar", "scrollHighlightRate", "rotaryEncoderInvert", "SoundEnabled", "soundOnGPIOFreq", "displayTemperature", "i2cLcdDimTime", "i2cLcdDimContrast", "soundOnGPIODuration", "soundOnGPIO", "NewDiskType"};
    return names[(unsigned)id];
}
static const char* OptionComment(Setting id) {
    static const char* comments[] = {
        "Drive device number (8 or 9).", "Idle sleep setting.", "RAMBOard memory expansion.",
        "Skip the drive motor spin-up delay.", "Ignore the computer reset line.",
        "Tape motor mode: 1 = always on, 0 = follow the computer.",
        "Start tape playback automatically when a TAP is loaded.",
        "Mount the configured image automatically at startup/reset.",
        "Return to the starting folder after reset.", "Show browser filenames in lowercase.",
        "Enable SD2IEC commands.", "OLED active contrast (1..255).", "Enable OLED dimming.",
        "Rotate the OLED display by 180 degrees.", "Use the Commodore character set.",
        "Browser scroll interval in seconds.", "Invert rotary encoder direction.",
        "Enable head-step sound.", "Sound frequency in Hz.", "Show temperature in the OLED header.",
        "OLED dim timeout in seconds; 0 disables dimming.", "OLED dim contrast (0..255).",
        "Sound duration in milliseconds.", "Sound output mode: 1 = buzzer, 0 = sample.",
        "Default new disk image type: d64 or g64."
    };
    return comments[(unsigned)id];
}
static std::string Lower(std::string s) { for (size_t i = 0; i < s.size(); ++i) s[i] = tolower((unsigned char)s[i]); return s; }
static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
static std::string Name(const std::string& line) {
    size_t eq = line.find('='); if (eq == std::string::npos) return "";
    return Lower(Trim(line.substr(0, eq)));
}
// Retain character offsets so edits can target values without touching comments.
static bool MaskComments(const std::string& text, std::string& visible) {
    visible = text;
    bool block = false, line = false, quoted = false;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\n') { line = false; continue; }
        if (line) { if (c != '\r') visible[i] = ' '; continue; }
        if (block) {
            if (c == '*' && i + 1 < text.size() && text[i+1] == '/') { visible[i] = visible[i+1] = ' '; ++i; block = false; }
            else if (c != '\r') visible[i] = ' ';
            continue;
        }
        if (c == '"' && (!i || text[i-1] != '\\')) { quoted = !quoted; continue; }
        if (quoted) continue;
        if (c == '/' && i + 1 < text.size() && text[i+1] == '*') { visible[i] = visible[i+1] = ' '; ++i; block = true; }
        else if (c == '#' || c == ';' || (c == '/' && i + 1 < text.size() && text[i+1] == '/')) { visible[i] = ' '; line = true; }
    }
    return !block;
}
static bool Valid(Setting id, int v) {
    if (id == Setting::Device) return v == 8 || v == 9;
    if (id == Setting::Freq) return v >= 100 && v <= 10000;
    if (id == Setting::Contrast) return v >= 1 && v <= 255;
    if (id == Setting::DimLevel) return v >= 0 && v <= 255;
    if (id == Setting::Scroll) return v >= 10 && v <= 5000;
    if (id == Setting::DimTime) return v >= 0 && v <= 3600;
    if (id == Setting::Duration) return v >= 0 && v <= 5000;
    return v == 0 || v == 1;
}
static bool DecodeValue(Setting id, const std::string& token, int& value) {
    if (id == Setting::NewType) {
        std::string lower = Lower(token);
        if (lower != "d64" && lower != "g64") return false;
        value = lower == "g64"; return true;
    }
    char* tail = 0;
    if (id == Setting::Scroll) {
        if (token.empty() || token.find_first_not_of("0123456789.eE+-") != std::string::npos) return false;
        double seconds = strtod(token.c_str(), &tail);
        if (*tail || !(seconds >= 0.01 && seconds <= 5.0)) return false;
        value = (int)(seconds * 1000 + 0.5); return true;
    }
    long number = strtol(token.c_str(), &tail, 0);
    if (token.empty() || *tail || number < 0 || number > 10000 || !Valid(id, number)) return false;
    value = id == Setting::SD2IEC ? !number : number; return true;
}
static std::string EncodeValue(Setting id, int value) {
    if (id == Setting::NewType) return value ? "g64" : "d64";
    char b[32];
    if (id == Setting::Scroll) {
        snprintf(b, sizeof(b), "%.3f", value / 1000.0);
        std::string s = b; while (s.size() > s.find('.') + 3 && s.back() == '0') s.pop_back();
        return s;
    }
    snprintf(b, sizeof(b), "%d", id == Setting::SD2IEC ? !value : value); return b;
}
bool ParseRuntimeSettings(const std::string& text, std::map<Setting, int>& values) {
    std::map<Setting, int> parsed;
    std::string visible; if (!MaskComments(text, visible)) return false;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start); if (end == std::string::npos) end = text.size();
        std::string line = visible.substr(start, end - start), key = Name(line);
        for (unsigned i = 0; i < (unsigned)Setting::Count; ++i) {
            Setting id = (Setting)i;
            if (key != Lower(OptionName(id)) && !(id == Setting::Sound && key == "oledsound")) continue;
            std::string value = Trim(line.substr(line.find('=') + 1));
            size_t comment = value.find_first_of(";#"); if (comment != std::string::npos) value.erase(comment);
            comment = value.find("//"); if (comment != std::string::npos) value.erase(comment);
            value = Trim(value); int v;
            if (!DecodeValue(id, value, v)) return false;
            parsed[id] = v;
        }
        start = end + 1;
    }
    values.swap(parsed); return true;
}
std::string MergeRuntimeSettings(const std::string& text, const std::map<Setting, int>& changes,
    const std::map<Setting, int>& defaults) {
    (void)defaults; // Every changed missing option is persisted, including its default value.
    struct Line { std::string body, ending, visible; };
    std::vector<Line> lines;
    std::map<Setting, size_t> last;
    std::string newline = "\n";
    bool foundEnding = false;
    std::string visible; if (!MaskComments(text, visible)) return text;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start); if (end == std::string::npos) end = text.size();
        size_t bodyEnd = end;
        if (end < text.size() && bodyEnd > start && text[bodyEnd - 1] == '\r') --bodyEnd;
        Line line = {text.substr(start, bodyEnd - start), end < text.size() ? text.substr(bodyEnd, end + 1 - bodyEnd) : "", visible.substr(start, bodyEnd - start)};
        if (!foundEnding && !line.ending.empty()) { newline = line.ending; foundEnding = true; }
        std::string key = Name(line.visible);
        for (std::map<Setting,int>::const_iterator it = changes.begin(); it != changes.end(); ++it)
            if (key == Lower(OptionName(it->first)) || (it->first == Setting::Sound && key == "oledsound"))
                last[it->first] = lines.size();
        lines.push_back(line);
        start = end + 1;
    }
    for (std::map<Setting,int>::const_iterator it = changes.begin(); it != changes.end(); ++it) {
        if (!last.count(it->first)) continue;
        std::string& line = lines[last[it->first]].body;
        const std::string& masked = lines[last[it->first]].visible;
        size_t a = masked.find('=') + 1;
        while (a < masked.size() && isspace((unsigned char)masked[a])) ++a;
        size_t b = a;
        while (b < masked.size() && !isspace((unsigned char)masked[b])) ++b;
        std::string token = line.substr(a, b - a); int original;
        if (!DecodeValue(it->first, token, original) || original != it->second)
            line.replace(a, b - a, EncodeValue(it->first, it->second));
        if (it->first == Setting::Sound && Name(masked) == "oledsound") {
            size_t nameStart = masked.find_first_not_of(" \t");
            size_t nameEnd = masked.find_last_not_of(" \t", masked.find('=') - 1);
            line.replace(nameStart, nameEnd - nameStart + 1, OptionName(it->first));
        }
    }
    std::string result;
    for (size_t i = 0; i < lines.size(); ++i) result += lines[i].body + lines[i].ending;
    for (std::map<Setting,int>::const_iterator it = changes.begin(); it != changes.end(); ++it) {
        if (last.count(it->first)) continue;
        if (!result.empty()) {
            if (result.back() != '\n') result += newline;
            std::string blankLines = newline + newline;
            if (result.size() < blankLines.size() || result.compare(result.size() - blankLines.size(), blankLines.size(), blankLines) != 0)
                result += newline;
        }
        result += std::string("// ") + OptionComment(it->first) + newline;
        result += std::string(OptionName(it->first)) + " = " + EncodeValue(it->first, it->second) + newline;
    }
    return result;
}
}
