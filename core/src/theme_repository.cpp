#include "termcore/theme_repository.h"
#include "termcore/theme_importer.h"
#include "termcore/theme_loader.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace termcore {

namespace {

namespace fs = std::filesystem;

bool computeIsDark(uint32_t color) {
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    double luminance = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    return luminance < 128.0;
}

std::string themeToJson(const Theme& theme) {
    nlohmann::json j;
    j["name"] = theme.name;

    auto colorHex = [](uint32_t c) -> std::string {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%06x", c);
        return std::string(buf);
    };

    j["background"] = colorHex(theme.background);
    j["foreground"] = colorHex(theme.foreground);
    j["cursorColor"] = colorHex(theme.cursor_color);
    j["selectionBackground"] = colorHex(theme.selection_background);
    j["selectionForeground"] = colorHex(theme.selection_foreground);

    nlohmann::json palette = nlohmann::json::array();
    for (int i = 0; i < 16; ++i) {
        palette.push_back(colorHex(theme.palette[i]));
    }
    j["palette"] = palette;

    return j.dump(2);
}

std::optional<Theme> themeFromJson(const std::string& json_str,
                                    const std::string& fallback_name) {
    try {
        auto j = nlohmann::json::parse(json_str);
        if (!j.is_object()) return std::nullopt;

        Theme theme{};
        theme.name = j.value("name", fallback_name);

        auto parseHex = [](const std::string& hex) -> uint32_t {
            std::string h = hex;
            if (!h.empty() && h[0] == '#') h = h.substr(1);
            try {
                return static_cast<uint32_t>(std::stoul(h, nullptr, 16));
            } catch (...) {
                return 0;
            }
        };

        if (j.contains("background"))
            theme.background = parseHex(j["background"].get<std::string>());
        if (j.contains("foreground"))
            theme.foreground = parseHex(j["foreground"].get<std::string>());
        if (j.contains("cursorColor"))
            theme.cursor_color = parseHex(j["cursorColor"].get<std::string>());
        if (j.contains("selectionBackground"))
            theme.selection_background =
                parseHex(j["selectionBackground"].get<std::string>());
        if (j.contains("selectionForeground"))
            theme.selection_foreground =
                parseHex(j["selectionForeground"].get<std::string>());

        if (j.contains("palette") && j["palette"].is_array()) {
            const auto& pal = j["palette"];
            for (size_t i = 0; i < 16 && i < pal.size(); ++i) {
                if (pal[i].is_string()) {
                    theme.palette[i] = parseHex(pal[i].get<std::string>());
                }
            }
        }

        return theme;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace

std::vector<Theme> ThemeRepository::builtinThemes() const {
    auto names = listBuiltinThemes();
    std::vector<Theme> themes;
    themes.reserve(names.size());
    for (const auto& name : names) {
        const Theme* t = getBuiltinTheme(name);
        if (t) themes.push_back(*t);
    }
    return themes;
}

std::vector<Theme> ThemeRepository::userThemes() const {
    return scanThemeDirectory(userThemeDirectory());
}

std::vector<Theme> ThemeRepository::allThemes() const {
    auto themes = builtinThemes();
    auto user = userThemes();

    for (auto& ut : user) {
        bool duplicate = false;
        for (const auto& bt : themes) {
            if (bt.name == ut.name) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            themes.push_back(std::move(ut));
        }
    }

    std::sort(themes.begin(), themes.end(),
              [](const Theme& a, const Theme& b) { return a.name < b.name; });
    return themes;
}

std::vector<ThemeInfo> ThemeRepository::allThemeInfos() const {
    std::vector<ThemeInfo> infos;

    auto builtins = builtinThemes();
    for (auto& t : builtins) {
        ThemeInfo info;
        info.theme = std::move(t);
        info.source = ThemeSource::Builtin;
        info.is_dark = computeIsDark(info.theme.background);
        infos.push_back(std::move(info));
    }

    auto user = userThemes();
    for (auto& t : user) {
        bool duplicate = false;
        for (const auto& existing : infos) {
            if (existing.theme.name == t.name) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            ThemeInfo info;
            info.theme = std::move(t);
            info.source = ThemeSource::User;
            info.is_dark = computeIsDark(info.theme.background);
            infos.push_back(std::move(info));
        }
    }

    std::sort(infos.begin(), infos.end(),
              [](const ThemeInfo& a, const ThemeInfo& b) {
                  return a.theme.name < b.theme.name;
              });
    return infos;
}

bool ThemeRepository::importTheme(const std::string& path) {
    auto theme = importFromFile(path);
    if (!theme) return false;
    return saveUserTheme(*theme);
}

bool ThemeRepository::exportTheme(const std::string& name,
                                   const std::string& path) const {
    // Try built-in first
    const Theme* builtin = getBuiltinTheme(name);
    if (builtin) {
        std::ofstream file(path);
        if (!file.is_open()) return false;
        file << themeToJson(*builtin);
        return file.good();
    }

    // Try user themes
    auto user = userThemes();
    for (const auto& t : user) {
        if (t.name == name) {
            std::ofstream file(path);
            if (!file.is_open()) return false;
            file << themeToJson(t);
            return file.good();
        }
    }

    return false;
}

bool ThemeRepository::deleteUserTheme(const std::string& name) {
    std::string dir = userThemeDirectory();
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;

    // Look for theme files matching the name
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;

        std::string stem = entry.path().stem().string();
        if (stem == name) {
            return fs::remove(entry.path(), ec);
        }

        // Also check JSON content for name field
        std::string ext = entry.path().extension().string();
        if (ext == ".json") {
            std::ifstream file(entry.path());
            if (file.is_open()) {
                std::string content((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
                auto theme = themeFromJson(content, "");
                if (theme && theme->name == name) {
                    file.close();
                    return fs::remove(entry.path(), ec);
                }
            }
        }
    }

    return false;
}

std::string ThemeRepository::userThemeDirectory() const {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (appdata) {
        return std::string(appdata) + "\\BreadTerminal\\themes";
    }
    return "";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (!home) return "";
    return std::string(home) +
           "/Library/Application Support/BreadTerminal/themes";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') {
        return std::string(xdg) + "/breadterminal/themes";
    }
    const char* home = std::getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/.config/breadterminal/themes";
#endif
}

bool ThemeRepository::saveUserTheme(const Theme& theme) const {
    std::string dir = userThemeDirectory();
    if (dir.empty()) return false;

    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return false;

    std::string filepath = dir + "/" + theme.name + ".json";
    // Use platform path separator on Windows
#ifdef _WIN32
    std::replace(filepath.begin(), filepath.end(), '/', '\\');
#endif

    std::ofstream file(filepath);
    if (!file.is_open()) return false;
    file << themeToJson(theme);
    return file.good();
}

} // namespace termcore
