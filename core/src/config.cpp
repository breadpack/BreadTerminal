#include "termcore/config.h"
#include "termcore/theme_loader.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>

namespace termcore {

namespace {

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

uint32_t parseHexColor(const std::string& s) {
    std::string hex = s;
    if (!hex.empty() && hex[0] == '#') {
        hex = hex.substr(1);
    }
    if (hex.size() != 6) return 0;
    return static_cast<uint32_t>(std::stoul(hex, nullptr, 16));
}

void parseLine(Config& config, const std::string& key, const std::string& value) {
    if (key == "font-family") {
        config.font_family = value;
    } else if (key == "font-size") {
        config.font_size = std::stof(value);
    } else if (key == "font-feature") {
        config.font_features.push_back(value);
    } else if (key == "background") {
        config.background = parseHexColor(value);
    } else if (key == "foreground") {
        config.foreground = parseHexColor(value);
    } else if (key == "cursor-color") {
        config.cursor_color = parseHexColor(value);
    } else if (key == "selection-background") {
        config.selection_background = parseHexColor(value);
    } else if (key == "selection-foreground") {
        config.selection_foreground = parseHexColor(value);
    } else if (key == "palette") {
        // Format: "N=#RRGGBB"
        auto eq = value.find('=');
        if (eq != std::string::npos) {
            int idx = std::stoi(value.substr(0, eq));
            if (idx >= 0 && idx < 16) {
                config.palette[idx] = parseHexColor(trim(value.substr(eq + 1)));
            }
        }
    } else if (key == "window-width") {
        config.window_width = std::stoi(value);
    } else if (key == "window-height") {
        config.window_height = std::stoi(value);
    } else if (key == "window-padding") {
        config.window_padding = std::clamp(std::stoi(value), 0, 200);
    } else if (key == "minimum-contrast") {
        config.minimum_contrast = std::clamp(std::stof(value), 1.0f, 21.0f);
    } else if (key == "quick-terminal-hotkey") {
        config.quick_terminal_hotkey = value;
    } else if (key == "scrollback-limit") {
        config.scrollback_limit = std::stoi(value);
    } else if (key == "cursor-style") {
        config.cursor_style = value;
    } else if (key == "cursor-blink") {
        config.cursor_blink = (value == "true" || value == "1" || value == "yes");
    } else if (key == "cursor-blink-interval") {
        config.cursor_blink_interval = std::clamp(std::stof(value), 0.1f, 2.0f);
    } else if (key == "shell") {
        config.shell = value;
    } else if (key == "theme") {
        config.theme = value;
    } else if (key == "clipboard-paste-protection") {
        config.clipboard_paste_protection = value;
    } else if (key == "clipboard-paste-bracketed-safe") {
        config.clipboard_paste_bracketed_safe = (value == "true" || value == "1" || value == "yes");
    } else if (key == "allow-clipboard-write") {
        config.allow_clipboard_write = (value == "true" || value == "1" || value == "yes");
    } else if (key == "notify-on-command-finish") {
        config.notify_on_command_finish = (value == "true" || value == "1" || value == "yes");
    } else if (key == "notify-after-seconds") {
        config.notify_after_seconds = std::clamp(std::stof(value), 0.0f, 3600.0f);
    } else if (key == "background-opacity") {
        config.background_opacity = std::clamp(std::stof(value), 0.0f, 1.0f);
    } else if (key == "background-blur") {
        config.background_blur = std::stoi(value);
    } else if (key == "sidebar-visible") {
        config.sidebar_visible = (value == "true" || value == "1" || value == "yes");
    } else if (key == "sidebar-width") {
        config.sidebar_width = std::stoi(value);
    } else if (key == "keybind") {
        // Format: "trigger=action"
        auto eq = value.find('=');
        if (eq != std::string::npos) {
            KeyBinding kb;
            kb.trigger = trim(value.substr(0, eq));
            kb.action = trim(value.substr(eq + 1));
            config.keybindings.push_back(std::move(kb));
        }
    } else {
        config.raw[key] = value;
    }
}

} // namespace

Config parseConfigString(const std::string& content) {
    Config config;

    // First pass: find the theme directive and apply it as a baseline.
    // This allows explicit color values to override the theme.
    {
        std::istringstream stream(content);
        std::string line;
        while (std::getline(stream, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = trim(line.substr(0, eq));
            if (key == "theme") {
                std::string value = trim(line.substr(eq + 1));
                config.theme = value;
                // For adaptive themes, default to dark variant during parsing.
                // The platform layer will re-resolve based on actual appearance.
                std::string resolved = resolveThemeForAppearance(value, /*is_dark=*/true);
                auto theme = findTheme(resolved);
                if (theme) {
                    applyTheme(config, *theme);
                }
                break;  // Only first theme directive matters
            }
        }
    }

    // Second pass: parse all directives (explicit colors override theme).
    {
        std::istringstream stream(content);
        std::string line;
        while (std::getline(stream, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = trim(line.substr(0, eq));
            std::string value = trim(line.substr(eq + 1));
            parseLine(config, key, value);
        }
    }

    return config;
}

Config parseConfigFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return Config{};
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return parseConfigString(content);
}

std::string defaultConfigPath() {
#ifdef __APPLE__
    const char* home = std::getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/Library/Application Support/BreadTerminal/config";
#elif defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    if (!appdata) return "";
    return std::string(appdata) + "\\BreadTerminal\\config";
#else
    // Linux: respect XDG_CONFIG_HOME
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') {
        return std::string(xdg) + "/breadterminal/config";
    }
    const char* home = std::getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/.config/breadterminal/config";
#endif
}

bool writeDefaultConfig(const std::string& path) {
    // Check if file already exists
    {
        std::ifstream check(path);
        if (check.is_open()) return false;
    }

    std::ofstream file(path);
    if (!file.is_open()) return false;

    file << "# BreadTerminal Configuration\n"
         << "# Lines starting with # are comments\n"
         << "# Empty lines are ignored\n"
         << "\n"
         << "font-family = Menlo\n"
         << "font-size = 14\n"
         << "\n"
         << "# theme = Dracula\n"
         << "\n"
         << "background = #1e1e2e\n"
         << "foreground = #cdd6f4\n"
         << "cursor-color = #f5e0dc\n"
         << "\n"
         << "window-width = 800\n"
         << "window-height = 600\n"
         << "\n"
         << "scrollback-limit = 10000\n"
         << "cursor-style = block\n"
         << "cursor-blink = true\n"
         << "\n"
         << "# shell = /bin/zsh\n"
         << "\n"
         << "# Keybindings\n"
         << "keybind = cmd+t=new_tab\n"
         << "keybind = cmd+w=close_tab\n"
         << "keybind = cmd+d=split_right\n"
         << "keybind = cmd+shift+d=split_down\n";

    return file.good();
}

namespace {

std::string colorToHex(uint32_t color) {
    std::ostringstream oss;
    oss << "#" << std::hex << std::setfill('0') << std::setw(6) << (color & 0xFFFFFF);
    return oss.str();
}

} // namespace

std::string serializeConfig(const Config& config) {
    std::ostringstream out;

    out << "# BreadTerminal Configuration\n\n";

    // Font
    out << "# Font\n";
    out << "font-family = " << config.font_family << "\n";
    out << "font-size = " << config.font_size << "\n";
    for (const auto& feat : config.font_features) {
        out << "font-feature = " << feat << "\n";
    }
    out << "\n";

    // Colors
    out << "# Colors\n";
    out << "background = " << colorToHex(config.background) << "\n";
    out << "foreground = " << colorToHex(config.foreground) << "\n";
    out << "cursor-color = " << colorToHex(config.cursor_color) << "\n";
    out << "selection-background = " << colorToHex(config.selection_background) << "\n";
    out << "selection-foreground = " << colorToHex(config.selection_foreground) << "\n";
    out << "\n";

    // Palette
    out << "# Palette (16 ANSI colors)\n";
    for (int i = 0; i < 16; ++i) {
        out << "palette = " << i << "=" << colorToHex(config.palette[i]) << "\n";
    }
    out << "\n";

    // Window
    out << "# Window\n";
    out << "window-width = " << config.window_width << "\n";
    out << "window-height = " << config.window_height << "\n";
    if (config.window_padding > 0) {
        out << "window-padding = " << config.window_padding << "\n";
    }
    out << "\n";

    // Minimum contrast
    if (config.minimum_contrast > 1.0f) {
        out << "minimum-contrast = " << config.minimum_contrast << "\n\n";
    }

    // Quick terminal hotkey
    if (!config.quick_terminal_hotkey.empty()) {
        out << "quick-terminal-hotkey = " << config.quick_terminal_hotkey << "\n\n";
    }

    // Terminal
    out << "# Terminal\n";
    out << "scrollback-limit = " << config.scrollback_limit << "\n";
    out << "cursor-style = " << config.cursor_style << "\n";
    out << "cursor-blink = " << (config.cursor_blink ? "true" : "false") << "\n";
    out << "cursor-blink-interval = " << config.cursor_blink_interval << "\n";
    if (!config.shell.empty()) {
        out << "shell = " << config.shell << "\n";
    }
    out << "\n";

    // Clipboard
    out << "# Clipboard\n";
    out << "clipboard-paste-protection = " << config.clipboard_paste_protection << "\n";
    out << "clipboard-paste-bracketed-safe = " << (config.clipboard_paste_bracketed_safe ? "true" : "false") << "\n";
    out << "allow-clipboard-write = " << (config.allow_clipboard_write ? "true" : "false") << "\n";
    out << "\n";

    // Command completion notifications
    out << "# Command completion notifications\n";
    out << "notify-on-command-finish = " << (config.notify_on_command_finish ? "true" : "false") << "\n";
    out << "notify-after-seconds = " << config.notify_after_seconds << "\n";
    out << "\n";

    // Background transparency
    out << "# Background transparency\n";
    out << "background-opacity = " << config.background_opacity << "\n";
    out << "background-blur = " << config.background_blur << "\n";
    out << "\n";

    // Sidebar
    out << "# Sidebar\n";
    out << "sidebar-visible = " << (config.sidebar_visible ? "true" : "false") << "\n";
    out << "sidebar-width = " << config.sidebar_width << "\n";
    out << "\n";

    // Theme
    if (!config.theme.empty()) {
        out << "# Theme\n";
        out << "theme = " << config.theme << "\n";
        out << "\n";
    }

    // Keybindings
    if (!config.keybindings.empty()) {
        out << "# Keybindings\n";
        for (const auto& kb : config.keybindings) {
            out << "keybind = " << kb.trigger << "=" << kb.action << "\n";
        }
        out << "\n";
    }

    return out.str();
}

bool writeConfigFile(const std::string& path, const Config& config) {
    std::string content = serializeConfig(config);
    std::string tmpPath = path + ".tmp";

    {
        std::ofstream file(tmpPath, std::ios::binary);
        if (!file.is_open()) return false;
        file << content;
        if (!file.good()) {
            std::remove(tmpPath.c_str());
            return false;
        }
    }

    // Set 0600 permissions (Unix only)
#if !defined(_WIN32)
    chmod(tmpPath.c_str(), 0600);
#endif

    // Atomic rename
    if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
        std::remove(tmpPath.c_str());
        return false;
    }

    return true;
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

    // Split on comma
    auto comma = theme_str.find(',');
    std::string part1, part2;
    if (comma != std::string::npos) {
        part1 = theme_str.substr(0, comma);
        part2 = theme_str.substr(comma + 1);
    } else {
        return std::nullopt;
    }

    // Trim parts
    auto trimPart = [](const std::string& s) -> std::string {
        auto start = s.find_first_not_of(" \t");
        if (start == std::string::npos) return "";
        auto end = s.find_last_not_of(" \t");
        return s.substr(start, end - start + 1);
    };

    part1 = trimPart(part1);
    part2 = trimPart(part2);

    // Parse each part: "dark:name" or "light:name"
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
