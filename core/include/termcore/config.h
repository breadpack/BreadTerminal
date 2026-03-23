#ifndef TERMCORE_CONFIG_H
#define TERMCORE_CONFIG_H

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "termcore/profile.h"

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
    std::string font_subpixel = "auto";  // "none", "rgb", "bgr", "vrgb", "vbgr", "auto"
    std::string font_hinting = "auto";   // "none", "slight", "medium", "full", "auto"

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
    int window_padding = 0;           // pixels, all sides

    // Minimum contrast ratio (WCAG 2.0): 1.0 = disabled, up to 21.0
    float minimum_contrast = 1.0f;

    // Quick terminal / visor mode
    std::string quick_terminal_hotkey;        // global hotkey (e.g., "ctrl+`", "F12")
    float quick_terminal_height = 0.4f;       // dropdown height as fraction of screen (0.1-1.0)
    int quick_terminal_animation_ms = 150;    // slide animation duration in milliseconds
    std::string quick_terminal_position = "top";  // top, bottom, left, right
    bool quick_terminal_auto_hide = true;     // hide on focus loss

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

    // Clickable URLs
    bool clickable_urls = true;            // Underline detected URLs, Ctrl+Click to open
    uint32_t url_color = 0x89b4fa;         // Link color (Catppuccin blue)

    // Command completion notifications
    bool notify_on_command_finish = true;
    float notify_after_seconds = 5.0f;  // only notify if command took > N seconds

    // Post-processing shader: "none", "crt", "bloom", etc., or path to custom shader file
    std::string custom_shader = "none";
    float shader_intensity = 1.0f;     // 0.0 to 1.0 shader effect strength

    // Background transparency
    float background_opacity = 1.0f;   // 0.0 (transparent) to 1.0 (opaque)
    float background_blur = 0.5f;       // legacy (macOS used as int 0-3), kept for config compat
    std::string background_blur_mode = "none";      // Windows: "none", "acrylic" (DWM)
    std::string background_blur_material = "none";  // macOS: "none", "hud_window", "sheet", "under_window"

    // Sidebar
    bool sidebar_visible = true;
    int sidebar_width = 220;

    // Theme
    std::string theme;  // Theme name or "dark:name,light:name"

    // Accessibility
    bool auto_detect_high_contrast = true;  // auto-switch to HC theme when OS HC is on
    bool respect_reduced_motion = true;      // honor OS reduced motion setting

    // Keybindings
    std::string keybinding_preset;  // Preset name: "Ghostty", "Kitty", "tmux", etc.
    std::vector<KeyBinding> keybindings;

    // Profiles (user-defined only — auto-detected are runtime)
    std::vector<Profile> profiles;
    std::string default_profile_id;
    std::vector<std::string> hidden_profile_ids;

    // Update checking
    bool check_for_updates = true;
    int update_check_interval = 24;  // hours

    // Font ligatures
    bool font_ligatures = true;

    // Inline image preview (opt-in)
    bool image_preview = false;
    int image_preview_max_height = 10;  // max cell rows for inline preview

    // Tab badge format (e.g., "{hostname}", "{user}@{hostname}")
    std::string tab_badge_format;

    // Tab process icon map: process_name -> Nerd Font hex codepoint string
    // Users can override or extend in config.lua via tab_process_icons table.
    std::unordered_map<std::string, std::string> tab_process_icons = {
        {"bash",            "F489"},  // nf-oct-terminal
        {"sh",              "F489"},
        {"zsh",             "F489"},
        {"fish",            "F489"},
        {"cmd",             "E70F"},  // nf-dev-windows
        {"cmd.exe",         "E70F"},
        {"powershell",      "EBC7"},  // nf-md-powershell
        {"pwsh",            "EBC7"},
        {"powershell.exe",  "EBC7"},
        {"pwsh.exe",        "EBC7"},
        {"python",          "E73C"},  // nf-dev-python
        {"python3",         "E73C"},
        {"python.exe",      "E73C"},
        {"python3.exe",     "E73C"},
        {"node",            "E718"},  // nf-dev-nodejs_small
        {"node.exe",        "E718"},
        {"vim",             "E62B"},  // nf-dev-vim
        {"nvim",            "E62B"},
        {"git",             "E702"},  // nf-dev-git
        {"git.exe",         "E702"},
        {"ssh",             "F489"},  // nf-oct-terminal
        {"ssh.exe",         "F489"},
        {"docker",          "F308"},  // nf-linux-docker
        {"docker.exe",      "F308"},
        {"cargo",           "E7A8"},  // nf-dev-rust
        {"rustc",           "E7A8"},
        {"go",              "E626"},  // nf-dev-go
        {"go.exe",          "E626"},
        {"ruby",            "E739"},  // nf-dev-ruby
        {"irb",             "E739"},
        {"lua",             "E620"},  // nf-seti-lua
        {"luajit",          "E620"},
    };

    // Session auto-save for crash recovery
    bool session_autosave = true;
    int session_autosave_interval = 30;  // seconds

    // Raw key-value pairs (for custom/unknown keys)
    std::unordered_map<std::string, std::string> raw;
};

/// Load config with automatic format detection.
/// Tries config.lua first (if Lua is available), then falls back to legacy format.
/// This is the recommended entry point for all platforms.
Config loadConfig();

/// Get the default config directory base path (platform-specific).
/// Used internally to locate config.lua.
std::string defaultConfigPath();

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

/// Parsed adaptive theme: dark and light theme names from "dark:name,light:name" format.
struct AdaptiveTheme {
    std::string dark_theme;
    std::string light_theme;
};

/// Check if a theme string uses adaptive "dark:name,light:name" format.
bool isAdaptiveTheme(const std::string& theme_str);

/// Parse an adaptive theme string. Returns nullopt if not in adaptive format.
std::optional<AdaptiveTheme> parseAdaptiveTheme(const std::string& theme_str);

/// Resolve theme name for current appearance mode (true = dark, false = light).
/// If the theme is adaptive, returns the appropriate variant; otherwise returns the theme as-is.
std::string resolveThemeForAppearance(const std::string& theme_str, bool is_dark);

} // namespace termcore
#endif
