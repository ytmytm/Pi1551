#ifndef PI1551_OLED_SETTINGS_H
#define PI1551_OLED_SETTINGS_H
#include "OledMenu.h"
#include <map>
namespace oled {
const char* OptionName(Setting id);
bool ParseRuntimeSettings(const std::string& text, std::map<Setting, int>& values);
std::string MergeRuntimeSettings(const std::string& text, const std::map<Setting, int>& changes,
    const std::map<Setting, int>& defaults = std::map<Setting, int>());
}
#endif
