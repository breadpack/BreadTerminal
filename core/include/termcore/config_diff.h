#ifndef TERMCORE_CONFIG_DIFF_H
#define TERMCORE_CONFIG_DIFF_H

#include "termcore/config.h"
#include <cstdint>

namespace termcore {

/// Flags indicating which config groups changed
enum class ConfigDirtyFlags : uint32_t {
    None        = 0,
    Colors      = 1 << 0,
    Font        = 1 << 1,
    CursorStyle = 1 << 2,
    Keybindings = 1 << 3,
    Scrollback  = 1 << 4,
    WindowSize  = 1 << 5,
    Theme       = 1 << 6,
    Opacity     = 1 << 7,   // background_opacity, background_blur
    Clipboard   = 1 << 8,   // clipboard_paste_protection, clipboard_paste_bracketed_safe
    Sidebar     = 1 << 9,   // sidebar_visible, sidebar_width
    Notification = 1 << 10,  // notify_on_command_finish, notify_after_seconds
};

inline ConfigDirtyFlags operator|(ConfigDirtyFlags a, ConfigDirtyFlags b) {
    return static_cast<ConfigDirtyFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline ConfigDirtyFlags& operator|=(ConfigDirtyFlags& a, ConfigDirtyFlags b) {
    a = a | b;
    return a;
}

inline bool hasFlag(ConfigDirtyFlags flags, ConfigDirtyFlags bit) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(bit)) != 0;
}

/// Compare two configs and return flags for all groups that differ.
ConfigDirtyFlags diffConfig(const Config& old_cfg, const Config& new_cfg);

} // namespace termcore

#endif
