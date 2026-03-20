#include "termcore/config_diff.h"

#include <cmath>
#include <cstring>

namespace termcore {

ConfigDirtyFlags diffConfig(const Config& old_cfg, const Config& new_cfg) {
    ConfigDirtyFlags flags = ConfigDirtyFlags::None;

    // --- Colors ---
    if (old_cfg.background != new_cfg.background ||
        old_cfg.foreground != new_cfg.foreground ||
        old_cfg.cursor_color != new_cfg.cursor_color ||
        old_cfg.selection_background != new_cfg.selection_background ||
        old_cfg.selection_foreground != new_cfg.selection_foreground ||
        std::memcmp(old_cfg.palette, new_cfg.palette, sizeof(old_cfg.palette)) != 0) {
        flags |= ConfigDirtyFlags::Colors;
    }

    // --- Font ---
    if (old_cfg.font_family != new_cfg.font_family ||
        old_cfg.font_size != new_cfg.font_size ||
        old_cfg.font_features != new_cfg.font_features) {
        flags |= ConfigDirtyFlags::Font;
    }

    // --- CursorStyle ---
    if (old_cfg.cursor_style != new_cfg.cursor_style ||
        old_cfg.cursor_blink != new_cfg.cursor_blink ||
        std::abs(old_cfg.cursor_blink_interval - new_cfg.cursor_blink_interval) > 0.001f) {
        flags |= ConfigDirtyFlags::CursorStyle;
    }

    // --- Keybindings ---
    if (old_cfg.keybindings.size() != new_cfg.keybindings.size()) {
        flags |= ConfigDirtyFlags::Keybindings;
    } else {
        for (size_t i = 0; i < old_cfg.keybindings.size(); ++i) {
            if (old_cfg.keybindings[i].trigger != new_cfg.keybindings[i].trigger ||
                old_cfg.keybindings[i].action != new_cfg.keybindings[i].action) {
                flags |= ConfigDirtyFlags::Keybindings;
                break;
            }
        }
    }

    // --- Scrollback ---
    if (old_cfg.scrollback_limit != new_cfg.scrollback_limit) {
        flags |= ConfigDirtyFlags::Scrollback;
    }

    // --- WindowSize ---
    if (old_cfg.window_width != new_cfg.window_width ||
        old_cfg.window_height != new_cfg.window_height ||
        old_cfg.window_padding != new_cfg.window_padding) {
        flags |= ConfigDirtyFlags::WindowSize;
    }

    // --- Minimum contrast ---
    if (std::abs(old_cfg.minimum_contrast - new_cfg.minimum_contrast) > 0.001f) {
        flags |= ConfigDirtyFlags::Colors;
    }

    // --- Theme ---
    if (old_cfg.theme != new_cfg.theme) {
        flags |= ConfigDirtyFlags::Theme;
    }

    // --- Opacity ---
    if (std::abs(old_cfg.background_opacity - new_cfg.background_opacity) > 0.001f ||
        old_cfg.background_blur != new_cfg.background_blur) {
        flags |= ConfigDirtyFlags::Opacity;
    }

    // --- Clipboard ---
    if (old_cfg.clipboard_paste_protection != new_cfg.clipboard_paste_protection ||
        old_cfg.clipboard_paste_bracketed_safe != new_cfg.clipboard_paste_bracketed_safe) {
        flags |= ConfigDirtyFlags::Clipboard;
    }

    // --- Sidebar ---
    if (old_cfg.sidebar_visible != new_cfg.sidebar_visible ||
        old_cfg.sidebar_width != new_cfg.sidebar_width) {
        flags |= ConfigDirtyFlags::Sidebar;
    }

    // --- Notification ---
    if (old_cfg.notify_on_command_finish != new_cfg.notify_on_command_finish ||
        std::abs(old_cfg.notify_after_seconds - new_cfg.notify_after_seconds) > 0.001f) {
        flags |= ConfigDirtyFlags::Notification;
    }

    return flags;
}

} // namespace termcore
