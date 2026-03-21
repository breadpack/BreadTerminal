#include "termcore/theme_loader.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace termcore {

namespace {

namespace fs = std::filesystem;

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::optional<uint32_t> parseColor(const std::string& s) {
    std::string hex = trim(s);
    if (hex.empty()) return std::nullopt;
    if (hex[0] == '#') hex = hex.substr(1);
    if (hex.size() != 6) return std::nullopt;
    try {
        return static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
    } catch (...) {
        return std::nullopt;
    }
}

std::string fileExtension(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

std::string stemName(const std::string& path) {
    return fs::path(path).stem().string();
}

ThemeFormat detectFormat(const std::string& content, const std::string& path) {
    std::string ext = fileExtension(path);
    if (ext == ".json") return ThemeFormat::WindowsTerminal;

    // Check content
    std::string trimmed = trim(content);
    if (!trimmed.empty() && trimmed[0] == '{') return ThemeFormat::WindowsTerminal;

    // Ghostty uses '=' for key-value
    if (content.find("palette =") != std::string::npos ||
        content.find("palette=") != std::string::npos ||
        content.find("background =") != std::string::npos ||
        content.find("background=") != std::string::npos) {
        return ThemeFormat::Ghostty;
    }

    // Kitty uses space-separated (color0 #..., background #...)
    if (content.find("color0 ") != std::string::npos ||
        content.find("background ") != std::string::npos) {
        return ThemeFormat::Kitty;
    }

    // Fallback: try Ghostty then Kitty
    return ThemeFormat::Ghostty;
}

std::optional<Theme> parseGhostty(const std::string& content,
                                   const std::string& name) {
    Theme theme{};
    theme.name = name;
    bool hasBackground = false;
    bool hasForeground = false;

    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        if (key == "background") {
            auto c = parseColor(value);
            if (c) { theme.background = *c; hasBackground = true; }
        } else if (key == "foreground") {
            auto c = parseColor(value);
            if (c) { theme.foreground = *c; hasForeground = true; }
        } else if (key == "cursor-color") {
            auto c = parseColor(value);
            if (c) theme.cursor_color = *c;
        } else if (key == "selection-background") {
            auto c = parseColor(value);
            if (c) theme.selection_background = *c;
        } else if (key == "selection-foreground") {
            auto c = parseColor(value);
            if (c) theme.selection_foreground = *c;
        } else if (key == "palette") {
            // Format: N=#RRGGBB or N=RRGGBB
            auto palEq = value.find('=');
            if (palEq != std::string::npos) {
                try {
                    int idx = std::stoi(value.substr(0, palEq));
                    if (idx >= 0 && idx < 16) {
                        auto c = parseColor(value.substr(palEq + 1));
                        if (c) theme.palette[idx] = *c;
                    }
                } catch (...) {}
            }
        }
    }

    if (!hasBackground && !hasForeground) return std::nullopt;
    return theme;
}

std::optional<Theme> parseKitty(const std::string& content,
                                 const std::string& name) {
    Theme theme{};
    theme.name = name;
    bool hasBackground = false;
    bool hasForeground = false;

    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        // Space-separated: key value
        auto space = line.find_first_of(" \t");
        if (space == std::string::npos) continue;

        std::string key = trim(line.substr(0, space));
        std::string value = trim(line.substr(space + 1));

        if (key == "background") {
            auto c = parseColor(value);
            if (c) { theme.background = *c; hasBackground = true; }
        } else if (key == "foreground") {
            auto c = parseColor(value);
            if (c) { theme.foreground = *c; hasForeground = true; }
        } else if (key == "cursor") {
            auto c = parseColor(value);
            if (c) theme.cursor_color = *c;
        } else if (key == "cursor_text_color") {
            // Not directly mapped to our Theme struct, skip
        } else if (key == "selection_background") {
            auto c = parseColor(value);
            if (c) theme.selection_background = *c;
        } else if (key == "selection_foreground") {
            auto c = parseColor(value);
            if (c) theme.selection_foreground = *c;
        } else if (key.substr(0, 5) == "color") {
            // color0 - color15
            try {
                int idx = std::stoi(key.substr(5));
                if (idx >= 0 && idx < 16) {
                    auto c = parseColor(value);
                    if (c) theme.palette[idx] = *c;
                }
            } catch (...) {}
        }
    }

    if (!hasBackground && !hasForeground) return std::nullopt;
    return theme;
}

std::optional<Theme> parseWindowsTerminal(const std::string& content,
                                           const std::string& name) {
    try {
        auto json = nlohmann::json::parse(content);

        Theme theme{};
        theme.name = json.value("name", name);

        auto getColor = [&](const std::string& key) -> std::optional<uint32_t> {
            if (json.contains(key) && json[key].is_string()) {
                return parseColor(json[key].get<std::string>());
            }
            return std::nullopt;
        };

        auto bg = getColor("background");
        auto fg = getColor("foreground");
        if (!bg && !fg) return std::nullopt;

        if (bg) theme.background = *bg;
        if (fg) theme.foreground = *fg;

        auto cursor = getColor("cursorColor");
        if (cursor) theme.cursor_color = *cursor;

        auto selBg = getColor("selectionBackground");
        if (selBg) theme.selection_background = *selBg;

        // Map color names to palette indices
        // Normal colors: 0-7
        static const std::pair<const char*, int> colorMap[] = {
            {"black", 0},   {"red", 1},     {"green", 2},  {"yellow", 3},
            {"blue", 4},    {"purple", 5},   {"cyan", 6},   {"white", 7},
            {"brightBlack", 8}, {"brightRed", 9},   {"brightGreen", 10},
            {"brightYellow", 11}, {"brightBlue", 12}, {"brightPurple", 13},
            {"brightCyan", 14},   {"brightWhite", 15},
        };

        for (const auto& [colorName, idx] : colorMap) {
            auto c = getColor(colorName);
            if (c) theme.palette[idx] = *c;
        }

        return theme;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace

std::optional<Theme> parseThemeString(const std::string& content,
                                       const std::string& name,
                                       ThemeFormat format) {
    switch (format) {
    case ThemeFormat::Ghostty:
        return parseGhostty(content, name);
    case ThemeFormat::Kitty:
        return parseKitty(content, name);
    case ThemeFormat::WindowsTerminal:
        return parseWindowsTerminal(content, name);
    case ThemeFormat::Auto:
        break;
    }

    // Auto: detect from content (no path info available here)
    std::string trimmed = trim(content);
    if (!trimmed.empty() && trimmed[0] == '{') {
        return parseWindowsTerminal(content, name);
    }
    if (content.find("palette =") != std::string::npos ||
        content.find("palette=") != std::string::npos ||
        content.find("background =") != std::string::npos ||
        content.find("background=") != std::string::npos) {
        return parseGhostty(content, name);
    }
    if (content.find("color0 ") != std::string::npos ||
        content.find("background ") != std::string::npos) {
        return parseKitty(content, name);
    }

    // Fallback: try Ghostty then Kitty
    auto result = parseGhostty(content, name);
    if (result) return result;
    return parseKitty(content, name);
}

Result<Theme> loadThemeFile(const std::string& path,
                            ThemeFormat format) {
    std::ifstream file(path);
    if (!file.is_open()) return Error("theme not found: " + path);

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    std::string name = stemName(path);

    if (format == ThemeFormat::Auto) {
        format = detectFormat(content, path);
    }

    auto theme = parseThemeString(content, name, format);
    if (!theme) return Error("invalid format: " + path);
    if (theme->name.empty()) {
        theme->name = name;
    }
    return std::move(*theme);
}

std::vector<Theme> scanThemeDirectory(const std::string& dir) {
    std::vector<Theme> themes;

    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return themes;

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;

        auto theme = loadThemeFile(entry.path().string());
        if (theme.ok()) {
            themes.push_back(std::move(theme.value()));
        }
    }

    std::sort(themes.begin(), themes.end(),
              [](const Theme& a, const Theme& b) { return a.name < b.name; });

    return themes;
}

std::string defaultThemeDir() {
#ifdef __APPLE__
    const char* home = std::getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/Library/Application Support/BreadTerminal/themes";
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

std::vector<Theme> allAvailableThemes() {
    // Start with built-in themes
    auto builtinNames = listBuiltinThemes();
    std::vector<Theme> themes;
    themes.reserve(builtinNames.size());

    for (const auto& name : builtinNames) {
        const Theme* t = getBuiltinTheme(name);
        if (t) themes.push_back(*t);
    }

    // Add user themes, skipping duplicates
    auto userThemes = scanThemeDirectory(defaultThemeDir());
    for (auto& ut : userThemes) {
        bool duplicate = false;
        for (const auto& bt : themes) {
            if (bt.name == ut.name) { duplicate = true; break; }
        }
        if (!duplicate) {
            themes.push_back(std::move(ut));
        }
    }

    return themes;
}

std::optional<Theme> findTheme(const std::string& name) {
    // Check built-in first
    const Theme* builtin = getBuiltinTheme(name);
    if (builtin) return *builtin;

    // Scan user directory
    auto userThemes = scanThemeDirectory(defaultThemeDir());
    for (auto& t : userThemes) {
        if (t.name == name) return std::move(t);
    }

    return std::nullopt;
}

} // namespace termcore
