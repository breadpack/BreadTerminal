#include "termcore/config_value_adapter.h"

#include <string>

namespace termcore {

// ---------------------------------------------------------------------------
// String getters / setters
// ---------------------------------------------------------------------------

std::string getConfigString(const Config& cfg, const std::string& key) {
    if (key == "shell") return cfg.shell;
    if (key == "cursor_style") return cfg.cursor_style;
    if (key == "clipboard_paste_protection") return cfg.clipboard_paste_protection;
    if (key == "font_family") return cfg.font_family;
    if (key == "theme") return cfg.theme;
    if (key == "quick_terminal_hotkey") return cfg.quick_terminal_hotkey;
    if (key == "default_profile_id") return cfg.default_profile_id;
    if (key == "keybinding_preset") return cfg.keybinding_preset;
    return {};
}

void setConfigString(Config& cfg, const std::string& key, const std::string& val) {
    if (key == "shell") cfg.shell = val;
    else if (key == "cursor_style") cfg.cursor_style = val;
    else if (key == "clipboard_paste_protection") cfg.clipboard_paste_protection = val;
    else if (key == "font_family") cfg.font_family = val;
    else if (key == "theme") cfg.theme = val;
    else if (key == "quick_terminal_hotkey") cfg.quick_terminal_hotkey = val;
    else if (key == "default_profile_id") cfg.default_profile_id = val;
    else if (key == "keybinding_preset") cfg.keybinding_preset = val;
}

// ---------------------------------------------------------------------------
// Int getters / setters
// ---------------------------------------------------------------------------

int getConfigInt(const Config& cfg, const std::string& key) {
    if (key == "window_width") return cfg.window_width;
    if (key == "window_height") return cfg.window_height;
    if (key == "window_padding") return cfg.window_padding;
    if (key == "scrollback_limit") return cfg.scrollback_limit;
    if (key == "background_blur") return cfg.background_blur;
    if (key == "sidebar_width") return cfg.sidebar_width;
    return 0;
}

void setConfigInt(Config& cfg, const std::string& key, int val) {
    if (key == "window_width") cfg.window_width = val;
    else if (key == "window_height") cfg.window_height = val;
    else if (key == "window_padding") cfg.window_padding = val;
    else if (key == "scrollback_limit") cfg.scrollback_limit = val;
    else if (key == "background_blur") cfg.background_blur = val;
    else if (key == "sidebar_width") cfg.sidebar_width = val;
}

// ---------------------------------------------------------------------------
// Float getters / setters
// ---------------------------------------------------------------------------

float getConfigFloat(const Config& cfg, const std::string& key) {
    if (key == "font_size") return cfg.font_size;
    if (key == "background_opacity") return cfg.background_opacity;
    if (key == "cursor_blink_interval") return cfg.cursor_blink_interval;
    if (key == "minimum_contrast") return cfg.minimum_contrast;
    if (key == "notify_after_seconds") return cfg.notify_after_seconds;
    return 0.0f;
}

void setConfigFloat(Config& cfg, const std::string& key, float val) {
    if (key == "font_size") cfg.font_size = val;
    else if (key == "background_opacity") cfg.background_opacity = val;
    else if (key == "cursor_blink_interval") cfg.cursor_blink_interval = val;
    else if (key == "minimum_contrast") cfg.minimum_contrast = val;
    else if (key == "notify_after_seconds") cfg.notify_after_seconds = val;
}

// ---------------------------------------------------------------------------
// Bool getters / setters
// ---------------------------------------------------------------------------

bool getConfigBool(const Config& cfg, const std::string& key) {
    if (key == "cursor_blink") return cfg.cursor_blink;
    if (key == "clipboard_paste_bracketed_safe") return cfg.clipboard_paste_bracketed_safe;
    if (key == "allow_clipboard_write") return cfg.allow_clipboard_write;
    if (key == "sidebar_visible") return cfg.sidebar_visible;
    if (key == "notify_on_command_finish") return cfg.notify_on_command_finish;
    return false;
}

void setConfigBool(Config& cfg, const std::string& key, bool val) {
    if (key == "cursor_blink") cfg.cursor_blink = val;
    else if (key == "clipboard_paste_bracketed_safe") cfg.clipboard_paste_bracketed_safe = val;
    else if (key == "allow_clipboard_write") cfg.allow_clipboard_write = val;
    else if (key == "sidebar_visible") cfg.sidebar_visible = val;
    else if (key == "notify_on_command_finish") cfg.notify_on_command_finish = val;
}

// ---------------------------------------------------------------------------
// Color getters / setters
// ---------------------------------------------------------------------------

uint32_t getConfigColor(const Config& cfg, const std::string& key) {
    if (key == "background") return cfg.background;
    if (key == "foreground") return cfg.foreground;
    if (key == "cursor_color") return cfg.cursor_color;
    if (key == "selection_background") return cfg.selection_background;
    if (key == "selection_foreground") return cfg.selection_foreground;
    // palette colors: "palette_0" through "palette_15"
    if (key.rfind("palette_", 0) == 0) {
        int idx = std::stoi(key.substr(8));
        if (idx >= 0 && idx < 16) return cfg.palette[idx];
    }
    return 0;
}

void setConfigColor(Config& cfg, const std::string& key, uint32_t val) {
    if (key == "background") cfg.background = val;
    else if (key == "foreground") cfg.foreground = val;
    else if (key == "cursor_color") cfg.cursor_color = val;
    else if (key == "selection_background") cfg.selection_background = val;
    else if (key == "selection_foreground") cfg.selection_foreground = val;
    else if (key.rfind("palette_", 0) == 0) {
        int idx = std::stoi(key.substr(8));
        if (idx >= 0 && idx < 16) cfg.palette[idx] = val;
    }
}

} // namespace termcore
