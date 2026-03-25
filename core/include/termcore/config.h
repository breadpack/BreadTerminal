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
/// Default member values match config.lua defaults.
/// Users can override any value in their config.lua.
struct Config {
    // Font
    std::string font_family = "Menlo";
    float font_size = 14.0f;
    std::vector<std::string> font_features;
    std::vector<std::string> font_fallback;
    std::string font_subpixel = "auto";
    std::string font_hinting = "auto";

    // Colors
    uint32_t background = 0x1e1e2e;
    uint32_t foreground = 0xcdd6f4;
    uint32_t cursor_color = 0;
    uint32_t selection_background = 0;
    uint32_t selection_foreground = 0;
    uint32_t palette[16] = {};            // populated by Lua defaults

    // Window
    int window_width = 800;
    int window_height = 600;
    int window_padding = 0;

    // Minimum contrast ratio
    float minimum_contrast = 1.0f;

    // Quick terminal / visor mode
    std::string quick_terminal_hotkey;
    float quick_terminal_height = 0.4f;
    int quick_terminal_animation_ms = 150;
    std::string quick_terminal_position = "top";
    bool quick_terminal_auto_hide = true;

    // Terminal
    int scrollback_limit = 10000;
    std::string cursor_style = "block";
    bool cursor_blink = true;
    float cursor_blink_interval = 0.5f;
    std::string shell;

    // Clipboard paste protection
    std::string clipboard_paste_protection = "multiline";
    bool clipboard_paste_bracketed_safe = true;

    // OSC 52 clipboard write from applications
    bool allow_clipboard_write = false;

    // Clickable URLs
    bool clickable_urls = true;
    uint32_t url_color = 0x89b4fa;

    // Command completion notifications
    bool notify_on_command_finish = true;
    float notify_after_seconds = 5.0f;

    // Shader
    std::string custom_shader = "none";
    float shader_intensity = 1.0f;

    // Background
    float background_opacity = 1.0f;
    float background_blur = 0.5f;
    std::string background_blur_mode = "none";
    std::string background_blur_material = "none";

    // Sidebar
    bool sidebar_visible = false;
    int sidebar_width = 220;

    // TMUX compatibility (for Agent Team support)
    bool tmux_compat_enabled = true;

    // Theme
    std::string theme;

    // Accessibility
    bool auto_detect_high_contrast = true;
    bool respect_reduced_motion = true;

    // Keybindings
    std::string keybinding_preset;
    std::vector<KeyBinding> keybindings;

    // Profiles (user-defined only — auto-detected are runtime)
    std::vector<Profile> profiles;
    std::string default_profile_id;
    std::vector<std::string> hidden_profile_ids;

    // Update checking
    bool check_for_updates = false;
    int update_check_interval = 0;

    // Font ligatures
    bool font_ligatures = true;

    // Inline image preview
    bool image_preview = false;
    int image_preview_max_height = 10;

    // Tab badge format
    std::string tab_badge_format;

    // Tab process icon map — now empty, populated by Lua defaults/icons.lua
    std::unordered_map<std::string, std::string> tab_process_icons;

    // Session
    bool session_autosave = true;
    int session_autosave_interval = 30;

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
