#ifndef TERMCORE_QUICK_TERMINAL_H
#define TERMCORE_QUICK_TERMINAL_H

#include "termcore/config.h"

#include <cstdint>
#include <string>

namespace termcore {

/// Parsed quick terminal configuration extracted from Config.
struct QuickTerminalConfig {
    std::string hotkey;                   // e.g., "ctrl+`", "F12"
    float height_percent = 0.4f;          // 0.1 - 1.0
    int animation_duration_ms = 150;      // slide animation duration
    std::string position = "top";         // top, bottom, left, right
    bool auto_hide_on_focus_loss = true;

    /// Build from a Config struct.
    static QuickTerminalConfig fromConfig(const Config& cfg) {
        QuickTerminalConfig qt;
        qt.hotkey = cfg.quick_terminal_hotkey;
        qt.height_percent = cfg.quick_terminal_height;
        if (qt.height_percent < 0.1f) qt.height_percent = 0.1f;
        if (qt.height_percent > 1.0f) qt.height_percent = 1.0f;
        qt.animation_duration_ms = cfg.quick_terminal_animation_ms;
        qt.position = cfg.quick_terminal_position;
        qt.auto_hide_on_focus_loss = cfg.quick_terminal_auto_hide;
        return qt;
    }

    /// Returns true if the quick terminal feature is enabled (hotkey configured).
    bool enabled() const { return !hotkey.empty(); }
};

/// Parsed hotkey: modifier flags + virtual key code (platform-neutral representation).
struct ParsedHotkey {
    uint8_t mods = 0;   // Bitmask: 1=Alt, 2=Ctrl, 4=Shift, 8=Win
    uint32_t vk = 0;    // Virtual key code (platform-specific)

    bool valid() const { return vk != 0; }
};

/// Parse a hotkey string like "ctrl+`", "ctrl+shift+f12" into ParsedHotkey.
/// Modifier names: ctrl, alt, shift, win/super.
/// Key names: a-z, 0-9, f1-f24, backtick/`, space, tab, etc.
ParsedHotkey parseHotkey(const std::string& hotkey_str);

} // namespace termcore
#endif
