#include "OledSettings.h"
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
namespace oled {
const char* OptionName(Setting id) {
    static const char* names[] = {"deviceID", "idleSleep", "RAMBOard", "skipMotorSpinUpDelay", "ignoreReset", "tapeMotorAlwaysOn", "autoMountEnabled", "onResetChangeToStartingFolder", "lowercaseBrowseModeFilenames", "disableSD2IECCommands", "i2cLcdOnContrast", "oledDimming", "i2cLcdFlip", "oledFont", "oledScroll", "rotaryEncoderInvert", "oledSound", "soundOnGPIOFreq"};
    return names[(unsigned)id];
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
    if (id == Setting::Contrast) return v >= 0 && v <= 255;
    if (id == Setting::Scroll) return v >= 0 && v <= 2;
    return v == 0 || v == 1;
}
bool ParseRuntimeSettings(const std::string& text, std::map<Setting, int>& values) {
    std::map<Setting, int> parsed;
    std::string visible; if (!MaskComments(text, visible)) return false;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start); if (end == std::string::npos) end = text.size();
        std::string line = visible.substr(start, end - start), key = Name(line);
        for (unsigned i = 0; i < (unsigned)Setting::Count; ++i) {
            Setting id = (Setting)i; if (key != Lower(OptionName(id))) continue;
            std::string value = Trim(line.substr(line.find('=') + 1));
            size_t comment = value.find_first_of(";#"); if (comment != std::string::npos) value.erase(comment);
            comment = value.find("//"); if (comment != std::string::npos) value.erase(comment);
            value = Trim(value); char* tail = 0; long v = strtol(value.c_str(), &tail, 0);
            if (value.empty() || *tail || v < 0 || v > 10000 || !Valid(id, v)) return false;
            if (id == Setting::SD2IEC) v = !v;
            parsed[id] = v;
        }
        start = end + 1;
    }
    values.swap(parsed); return true;
}
std::string MergeRuntimeSettings(const std::string& text, const std::map<Setting, int>& changes,
    const std::map<Setting, int>& defaults) {
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
            if (key == Lower(OptionName(it->first))) last[it->first] = lines.size();
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
        int value = it->first == Setting::SD2IEC ? !it->second : it->second;
        std::string token = line.substr(a, b - a); char* tail = 0;
        long original = strtol(token.c_str(), &tail, 0);
        if (!token.empty() && !*tail && original == value) continue;
        char number[32]; snprintf(number, sizeof(number), "%d", value);
        line.replace(a, b - a, number); // only the value; preserve case, spacing, comments and line number
    }
    std::string result;
    for (size_t i = 0; i < lines.size(); ++i) result += lines[i].body + lines[i].ending;
    for (std::map<Setting,int>::const_iterator it = changes.begin(); it != changes.end(); ++it) {
        if (last.count(it->first)) continue;
        std::map<Setting,int>::const_iterator def = defaults.find(it->first);
        if (def != defaults.end() && def->second == it->second) continue;
        if (!result.empty() && result.back() != '\n') result += newline;
        char number[32]; snprintf(number, sizeof(number), "%d", it->first == Setting::SD2IEC ? !it->second : it->second);
        result += std::string(OptionName(it->first)) + " = " + number + newline;
    }
    return result;
}
}
