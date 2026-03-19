#ifndef TERMCORE_CONFIG_H
#define TERMCORE_CONFIG_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace termcore {

/// A single keybinding
struct KeyBinding {
    std::string trigger;  // e.g., "cmd+t", "ctrl+shift+c"
    std::string action;   // e.g., "new_tab", "copy"
};

/// Terminal configuration
struct Config {
    // Font
    std::string font_family = "Menlo";
    float font_size = 14.0f;
    std::vector<std::string> font_features;  // OpenType features

    // Colors
    uint32_t background = 0x1e1e2e;
    uint32_t foreground = 0xcdd6f4;
    uint32_t cursor_color = 0xf5e0dc;
    uint32_t selection_background = 0x585b70;
    uint32_t selection_foreground = 0xcdd6f4;
    uint32_t palette[16] = {
        0x45475a, 0xf38ba8, 0xa6e3a1, 0xf9e2af,
        0x89b4fa, 0xf5c2e7, 0x94e2d5, 0xbac2de,
        0x585b70, 0xf38ba8, 0xa6e3a1, 0xf9e2af,
        0x89b4fa, 0xf5c2e7, 0x94e2d5, 0xa6adc8,
    };

    // Window
    int window_width = 800;
    int window_height = 600;

    // Terminal
    int scrollback_limit = 10000;
    std::string cursor_style = "block";  // block, underline, bar
    bool cursor_blink = true;
    float cursor_blink_interval = 0.5f;  // seconds (0.1 - 2.0)
    std::string shell;  // Empty = use $SHELL

    // Clipboard paste protection
    std::string clipboard_paste_protection = "multiline";  // "never", "multiline", "always"
    bool clipboard_paste_bracketed_safe = true;

    // OSC 52 clipboard write from applications (default: false for security)
    bool allow_clipboard_write = false;

    // Background transparency
    float background_opacity = 1.0f;   // 0.0 (transparent) to 1.0 (opaque)
    int background_blur = 0;           // 0=none, 1=low, 2=medium, 3=high

    // Sidebar
    bool sidebar_visible = true;
    int sidebar_width = 220;

    // Theme
    std::string theme;  // Theme name or "dark:name,light:name"

    // Keybindings
    std::vector<KeyBinding> keybindings;

    // Raw key-value pairs (for custom/unknown keys)
    std::unordered_map<std::string, std::string> raw;
};

/// Parse a config file. Returns Config with parsed values.
Config parseConfigFile(const std::string& path);

/// Parse a config string (for testing).
Config parseConfigString(const std::string& content);

/// Get the default config file path.
/// macOS: ~/Library/Application Support/BreadTerminal/config
/// Linux: ~/.config/breadterminal/config
std::string defaultConfigPath();

/// Write a default config file if none exists.
bool writeDefaultConfig(const std::string& path);

/// Serialize a Config to config file text format.
std::string serializeConfig(const Config& config);

/// Write a config to a file atomically (write .tmp, rename).
/// Sets 0600 permissions on the file.
bool writeConfigFile(const std::string& path, const Config& config);

/// Named color themes
struct Theme {
    std::string name;
    uint32_t background;
    uint32_t foreground;
    uint32_t cursor_color;
    uint32_t selection_background;
    uint32_t selection_foreground;
    uint32_t palette[16];
};

/// Get a built-in theme by name. Returns nullptr if not found.
const Theme* getBuiltinTheme(const std::string& name);

/// List all built-in theme names.
std::vector<std::string> listBuiltinThemes();

/// Apply a theme to a config.
void applyTheme(Config& config, const Theme& theme);

} // namespace termcore
#endif
