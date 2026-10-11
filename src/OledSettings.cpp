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
static bool Valid(Setting id, int v) {
    if (id == Setting::Device) return v == 8 || v == 9;
    if (id == Setting::Freq) return v >= 100 && v <= 10000;
    if (id == Setting::Contrast) return v >= 0 && v <= 255;
    if (id == Setting::Scroll) return v >= 0 && v <= 2;
    return v == 0 || v == 1;
}
bool ParseRuntimeSettings(const std::string& text, std::map<Setting, int>& values) {
    std::map<Setting, int> parsed;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start); if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start), key = Name(line);
        for (unsigned i = 0; i < (unsigned)Setting::Count; ++i) {
            Setting id = (Setting)i; if (key != Lower(OptionName(id))) continue;
            std::string value = Trim(line.substr(line.find('=') + 1));
            size_t comment = value.find_first_of(";#"); if (comment != std::string::npos) value.erase(comment);
            comment = value.find("//"); if (comment != std::string::npos) value.erase(comment);
            value = Trim(value); char* tail = 0; long v = strtol(value.c_str(), &tail, 10);
            if (value.empty() || *tail || v < 0 || v > 10000 || !Valid(id, v)) return false;
            if (id == Setting::SD2IEC) v = !v;
            parsed[id] = v;
        }
        start = end + 1;
    }
    values.swap(parsed); return true;
}
std::string MergeRuntimeSettings(const std::string& text, const std::map<Setting, int>& values) {
    std::string result; std::map<Setting, bool> seen; size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start); if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start), key = Name(line); bool replaced = false;
        for (std::map<Setting,int>::const_iterator it = values.begin(); it != values.end(); ++it) {
            if (key != Lower(OptionName(it->first))) continue;
            if (!seen[it->first]) {
                size_t comment = line.find("//", line.find('=') + 1);
                size_t other = line.find_first_of(";#", line.find('=') + 1);
                if (other < comment) comment = other;
                char b[32]; snprintf(b, sizeof(b), "%d", it->first == Setting::SD2IEC ? !it->second : it->second);
                result += std::string(OptionName(it->first)) + " = " + b;
                if (comment != std::string::npos) result += " " + line.substr(comment);
                result += "\n"; seen[it->first] = true;
            }
            replaced = true; break;
        }
        if (!replaced) result += line + "\n";
        start = end + 1;
    }
    for (std::map<Setting,int>::const_iterator it = values.begin(); it != values.end(); ++it) if (!seen[it->first]) {
        char b[32]; snprintf(b, sizeof(b), "%d", it->first == Setting::SD2IEC ? !it->second : it->second);
        result += std::string(OptionName(it->first)) + " = " + b + "\n";
    }
    return result;
}
}
