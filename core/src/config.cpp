#include "termcore/config.h"
#include "termcore/lua_config.h"
#include "termcore/theme_loader.h"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

namespace termcore {

std::string defaultConfigPath() {
#if defined(_WIN32)
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (!home) return "";
    return std::string(home) + "/.bt/config";
}

std::string pluginsDirectory() {
    std::string base = defaultConfigPath();
    if (base.empty()) return "";
    namespace fs = std::filesystem;
    return (fs::path(base).parent_path() / "plugins").string();
}

std::string dataDirectory() {
    std::string base = defaultConfigPath();
    if (base.empty()) return "";
    namespace fs = std::filesystem;
    return (fs::path(base).parent_path() / "data").string();
}

Config loadConfig() {
    // Try config.lua from the standard Lua config path
    std::string luaPath = defaultLuaConfigPath();
    if (!luaPath.empty() && loadConfigLua(luaPath).ok()) {
        return luaConfig();
    }

    // Also check config.lua alongside the base config directory
    std::string basePath = defaultConfigPath();
    if (!basePath.empty()) {
        namespace fs = std::filesystem;
        fs::path luaAlt = fs::path(basePath).parent_path() / "config.lua";
        if (fs::exists(luaAlt) && loadConfigLua(luaAlt.string()).ok()) {
            return luaConfig();
        }
    }

    // No config found — return defaults
    return Config{};
}

void applyTheme(Config& config, const Theme& theme) {
    config.background = theme.background;
    config.foreground = theme.foreground;
    config.cursor_color = theme.cursor_color;
    config.selection_background = theme.selection_background;
    config.selection_foreground = theme.selection_foreground;
    for (int i = 0; i < 16; ++i) {
        config.palette[i] = theme.palette[i];
    }
}

bool isAdaptiveTheme(const std::string& theme_str) {
    return theme_str.find("dark:") != std::string::npos &&
           theme_str.find("light:") != std::string::npos;
}

std::optional<AdaptiveTheme> parseAdaptiveTheme(const std::string& theme_str) {
    if (!isAdaptiveTheme(theme_str)) return std::nullopt;

    AdaptiveTheme result;

    auto comma = theme_str.find(',');
    std::string part1, part2;
    if (comma != std::string::npos) {
        part1 = theme_str.substr(0, comma);
        part2 = theme_str.substr(comma + 1);
    } else {
        return std::nullopt;
    }

    auto trimPart = [](const std::string& s) -> std::string {
        auto start = s.find_first_not_of(" \t");
        if (start == std::string::npos) return "";
        auto end = s.find_last_not_of(" \t");
        return s.substr(start, end - start + 1);
    };

    part1 = trimPart(part1);
    part2 = trimPart(part2);

    auto parsePart = [&](const std::string& part) {
        if (part.substr(0, 5) == "dark:") {
            result.dark_theme = trimPart(part.substr(5));
        } else if (part.substr(0, 6) == "light:") {
            result.light_theme = trimPart(part.substr(6));
        }
    };

    parsePart(part1);
    parsePart(part2);

    if (result.dark_theme.empty() || result.light_theme.empty()) {
        return std::nullopt;
    }

    return result;
}

std::string resolveThemeForAppearance(const std::string& theme_str, bool is_dark) {
    auto adaptive = parseAdaptiveTheme(theme_str);
    if (!adaptive) return theme_str;
    return is_dark ? adaptive->dark_theme : adaptive->light_theme;
}

} // namespace termcore
