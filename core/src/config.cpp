#include "termcore/config.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

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
    } else if (key == "scrollback-limit") {
        config.scrollback_limit = std::stoi(value);
    } else if (key == "cursor-style") {
        config.cursor_style = value;
    } else if (key == "cursor-blink") {
        config.cursor_blink = (value == "true" || value == "1" || value == "yes");
    } else if (key == "shell") {
        config.shell = value;
    } else if (key == "theme") {
        config.theme = value;
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

} // namespace termcore
