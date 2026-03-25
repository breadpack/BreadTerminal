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
/// All member initializers are "unset" sentinel values.
/// Actual defaults are provided by embedded defaults/*.lua files.
struct Config {
    // Font
    std::string font_family;              // unset — populated by Lua defaults
    float font_size = 0;                  // unset (0 = not configured)
    std::vector<std::string> font_features;
    std::vector<std::string> font_fallback;
    std::string font_subpixel;            // unset
    std::string font_hinting;             // unset

    // Colors
    uint32_t background = 0;              // unset
    uint32_t foreground = 0;              // unset
    uint32_t cursor_color = 0;            // unset
    uint32_t selection_background = 0;    // unset
    uint32_t selection_foreground = 0;    // unset
    uint32_t palette[16] = {};            // unset — populated by Lua defaults

    // Window
    int window_width = 0;                 // unset
    int window_height = 0;                // unset
    int window_padding = 0;

    // Minimum contrast ratio
    float minimum_contrast = 0;           // unset

    // Quick terminal / visor mode
    std::string quick_terminal_hotkey;
    float quick_terminal_height = 0;      // unset
    int quick_terminal_animation_ms = 0;  // unset
    std::string quick_terminal_position;  // unset
    bool quick_terminal_auto_hide = false; // unset

    // Terminal
    int scrollback_limit = 0;             // unset
    std::string cursor_style;             // unset
    bool cursor_blink = false;            // unset
    float cursor_blink_interval = 0;      // unset
    std::string shell;

    // Clipboard paste protection
    std::string clipboard_paste_protection;  // unset
    bool clipboard_paste_bracketed_safe = false; // unset

    // OSC 52 clipboard write from applications
    bool allow_clipboard_write = false;

    // Clickable URLs
    bool clickable_urls = false;           // unset
    uint32_t url_color = 0;               // unset

    // Command completion notifications
    bool notify_on_command_finish = false;  // unset
    float notify_after_seconds = 0;        // unset

    // Shader
    std::string custom_shader;             // unset
    float shader_intensity = 0;            // unset

    // Background
    float background_opacity = 0;          // unset
    float background_blur = 0;             // unset
    std::string background_blur_mode;      // unset
    std::string background_blur_material;  // unset

    // Sidebar
    bool sidebar_visible = false;          // unset
    int sidebar_width = 0;                 // unset

    // TMUX compatibility (for Agent Team support)
    bool tmux_compat_enabled = true;

    // Theme
    std::string theme;

    // Accessibility
    bool auto_detect_high_contrast = false; // unset
    bool respect_reduced_motion = false;    // unset

    // Keybindings
    std::string keybinding_preset;
    std::vector<KeyBinding> keybindings;

    // Profiles (user-defined only — auto-detected are runtime)
    std::vector<Profile> profiles;
    std::string default_profile_id;
    std::vector<std::string> hidden_profile_ids;

    // Update checking
    bool check_for_updates = false;        // unset
    int update_check_interval = 0;         // unset

    // Font ligatures
    bool font_ligatures = false;           // unset

    // Inline image preview
    bool image_preview = false;
    int image_preview_max_height = 0;      // unset

    // Tab badge format
    std::string tab_badge_format;

    // Tab process icon map — now empty, populated by Lua defaults/icons.lua
    std::unordered_map<std::string, std::string> tab_process_icons;

    // Session
    bool session_autosave = false;         // unset
    int session_autosave_interval = 0;     // unset

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
