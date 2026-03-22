#include "termcore/quick_terminal.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>

namespace termcore {

namespace {

std::string toLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

/// Split a string by '+' delimiter.
std::vector<std::string> splitPlus(const std::string& s) {
    std::vector<std::string> parts;
    std::istringstream iss(s);
    std::string part;
    while (std::getline(iss, part, '+')) {
        // Trim whitespace
        while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front())))
            part.erase(part.begin());
        while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back())))
            part.pop_back();
        if (!part.empty())
            parts.push_back(part);
    }
    return parts;
}

// Virtual key code mapping (Windows VK_ codes, also usable cross-platform)
uint32_t keyNameToVK(const std::string& name) {
    std::string lower = toLower(name);

    // Function keys
    if (lower.size() >= 2 && lower[0] == 'f') {
        int n = 0;
        try { n = std::stoi(lower.substr(1)); } catch (...) {}
        if (n >= 1 && n <= 24) {
            return 0x70 + (n - 1); // VK_F1 = 0x70
        }
    }

    // Single character a-z, 0-9
    if (lower.size() == 1) {
        char c = lower[0];
        if (c >= 'a' && c <= 'z') return static_cast<uint32_t>(c - 'a' + 'A');
        if (c >= '0' && c <= '9') return static_cast<uint32_t>(c);
    }

    // Backtick / grave accent
    if (lower == "`" || lower == "backtick" || lower == "grave")
        return 0xC0; // VK_OEM_3

    // Named keys
    static const std::unordered_map<std::string, uint32_t> names = {
        {"space", 0x20}, {"tab", 0x09}, {"enter", 0x0D}, {"return", 0x0D},
        {"escape", 0x1B}, {"esc", 0x1B}, {"backspace", 0x08},
        {"delete", 0x2E}, {"insert", 0x2D},
        {"home", 0x24}, {"end", 0x23},
        {"pageup", 0x21}, {"pagedown", 0x22},
        {"up", 0x26}, {"down", 0x28}, {"left", 0x25}, {"right", 0x27},
        {"minus", 0xBD}, {"-", 0xBD},
        {"equal", 0xBB}, {"=", 0xBB}, {"plus", 0xBB},
        {"semicolon", 0xBA}, {";", 0xBA},
        {"comma", 0xBC}, {",", 0xBC},
        {"period", 0xBE}, {".", 0xBE},
        {"slash", 0xBF}, {"/", 0xBF},
        {"backslash", 0xDC}, {"\\", 0xDC},
        {"[", 0xDB}, {"]", 0xDD},
        {"'", 0xDE}, {"quote", 0xDE},
    };

    auto it = names.find(lower);
    if (it != names.end()) return it->second;

    return 0;
}

} // namespace

ParsedHotkey parseHotkey(const std::string& hotkey_str) {
    ParsedHotkey result;
    if (hotkey_str.empty()) return result;

    auto parts = splitPlus(hotkey_str);
    if (parts.empty()) return result;

    // Last part is the key, rest are modifiers
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        std::string mod = toLower(parts[i]);
        if (mod == "alt")            result.mods |= 1;
        else if (mod == "ctrl" || mod == "control") result.mods |= 2;
        else if (mod == "shift")     result.mods |= 4;
        else if (mod == "win" || mod == "super" || mod == "cmd") result.mods |= 8;
    }

    result.vk = keyNameToVK(parts.back());
    return result;
}

} // namespace termcore
