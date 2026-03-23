#include "termcore/config_field_registry.h"

#include <cmath>
#include <cstring>

namespace termcore {

// =========================================================================
// Single source of truth for all simple config fields.
// Adding a new field: add ONE entry here. Accessors, diff, and settings
// model will pick it up automatically.
// =========================================================================

const StringFieldDesc kStringFields[] = {
    // key                          member                              dirty flag
    {"shell",                       &Config::shell,                     ConfigDirtyFlags::None},
    {"cursor_style",                &Config::cursor_style,              ConfigDirtyFlags::CursorStyle},
    {"clipboard_paste_protection",  &Config::clipboard_paste_protection,ConfigDirtyFlags::Clipboard},
    {"font_family",                 &Config::font_family,               ConfigDirtyFlags::Font},
    {"font_subpixel",               &Config::font_subpixel,             ConfigDirtyFlags::Font},
    {"font_hinting",                &Config::font_hinting,              ConfigDirtyFlags::Font},
    {"theme",                       &Config::theme,                     ConfigDirtyFlags::Theme},
    {"default_profile_id",          &Config::default_profile_id,        ConfigDirtyFlags::None},
    {"keybinding_preset",           &Config::keybinding_preset,         ConfigDirtyFlags::Keybindings},
    {"background_blur_mode",        &Config::background_blur_mode,      ConfigDirtyFlags::Opacity},
    {"background_blur_material",    &Config::background_blur_material,  ConfigDirtyFlags::Opacity},
    {"quick_terminal_hotkey",       &Config::quick_terminal_hotkey,     ConfigDirtyFlags::None},
    {"quick_terminal_position",     &Config::quick_terminal_position,   ConfigDirtyFlags::None},
    {"custom_shader",               &Config::custom_shader,             ConfigDirtyFlags::None},
    {"tab_badge_format",            &Config::tab_badge_format,          ConfigDirtyFlags::None},
};
const size_t kStringFieldCount = sizeof(kStringFields) / sizeof(kStringFields[0]);

const FloatFieldDesc kFloatFields[] = {
    {"font_size",                   &Config::font_size,                 ConfigDirtyFlags::Font},
    {"background_opacity",          &Config::background_opacity,        ConfigDirtyFlags::Opacity},
    {"background_blur",             &Config::background_blur,           ConfigDirtyFlags::Opacity},
    {"cursor_blink_interval",       &Config::cursor_blink_interval,     ConfigDirtyFlags::CursorStyle},
    {"minimum_contrast",            &Config::minimum_contrast,          ConfigDirtyFlags::Colors},
    {"notify_after_seconds",        &Config::notify_after_seconds,      ConfigDirtyFlags::Notification},
    {"shader_intensity",            &Config::shader_intensity,          ConfigDirtyFlags::None},
    {"quick_terminal_height",       &Config::quick_terminal_height,     ConfigDirtyFlags::None},
};
const size_t kFloatFieldCount = sizeof(kFloatFields) / sizeof(kFloatFields[0]);

const IntFieldDesc kIntFields[] = {
    {"window_width",                &Config::window_width,              ConfigDirtyFlags::WindowSize},
    {"window_height",               &Config::window_height,             ConfigDirtyFlags::WindowSize},
    {"window_padding",              &Config::window_padding,            ConfigDirtyFlags::WindowSize},
    {"scrollback_limit",            &Config::scrollback_limit,          ConfigDirtyFlags::Scrollback},
    {"sidebar_width",               &Config::sidebar_width,             ConfigDirtyFlags::Sidebar},
    {"quick_terminal_animation_ms", &Config::quick_terminal_animation_ms, ConfigDirtyFlags::None},
    {"update_check_interval",       &Config::update_check_interval,     ConfigDirtyFlags::None},
    {"image_preview_max_height",    &Config::image_preview_max_height,  ConfigDirtyFlags::None},
    {"session_autosave_interval",   &Config::session_autosave_interval, ConfigDirtyFlags::None},
};
const size_t kIntFieldCount = sizeof(kIntFields) / sizeof(kIntFields[0]);

const BoolFieldDesc kBoolFields[] = {
    {"cursor_blink",                &Config::cursor_blink,              ConfigDirtyFlags::CursorStyle},
    {"clipboard_paste_bracketed_safe", &Config::clipboard_paste_bracketed_safe, ConfigDirtyFlags::Clipboard},
    {"allow_clipboard_write",       &Config::allow_clipboard_write,     ConfigDirtyFlags::None},
    {"sidebar_visible",             &Config::sidebar_visible,           ConfigDirtyFlags::Sidebar},
    {"notify_on_command_finish",    &Config::notify_on_command_finish,  ConfigDirtyFlags::Notification},
    {"clickable_urls",              &Config::clickable_urls,            ConfigDirtyFlags::None},
    {"quick_terminal_auto_hide",    &Config::quick_terminal_auto_hide,  ConfigDirtyFlags::None},
    {"auto_detect_high_contrast",   &Config::auto_detect_high_contrast, ConfigDirtyFlags::None},
    {"respect_reduced_motion",      &Config::respect_reduced_motion,    ConfigDirtyFlags::None},
    {"check_for_updates",           &Config::check_for_updates,         ConfigDirtyFlags::None},
    {"font_ligatures",              &Config::font_ligatures,            ConfigDirtyFlags::Font},
    {"image_preview",               &Config::image_preview,             ConfigDirtyFlags::None},
    {"session_autosave",            &Config::session_autosave,          ConfigDirtyFlags::None},
};
const size_t kBoolFieldCount = sizeof(kBoolFields) / sizeof(kBoolFields[0]);

const ColorFieldDesc kColorFields[] = {
    {"background",                  &Config::background,                ConfigDirtyFlags::Colors},
    {"foreground",                  &Config::foreground,                ConfigDirtyFlags::Colors},
    {"cursor_color",                &Config::cursor_color,              ConfigDirtyFlags::Colors},
    {"selection_background",        &Config::selection_background,      ConfigDirtyFlags::Colors},
    {"selection_foreground",        &Config::selection_foreground,      ConfigDirtyFlags::Colors},
    {"url_color",                   &Config::url_color,                 ConfigDirtyFlags::Colors},
};
const size_t kColorFieldCount = sizeof(kColorFields) / sizeof(kColorFields[0]);

// =========================================================================
// Lookup helpers
// =========================================================================

const StringFieldDesc* findStringField(const std::string& key) {
    for (size_t i = 0; i < kStringFieldCount; ++i)
        if (key == kStringFields[i].key) return &kStringFields[i];
    return nullptr;
}

const FloatFieldDesc* findFloatField(const std::string& key) {
    for (size_t i = 0; i < kFloatFieldCount; ++i)
        if (key == kFloatFields[i].key) return &kFloatFields[i];
    return nullptr;
}

const IntFieldDesc* findIntField(const std::string& key) {
    for (size_t i = 0; i < kIntFieldCount; ++i)
        if (key == kIntFields[i].key) return &kIntFields[i];
    return nullptr;
}

const BoolFieldDesc* findBoolField(const std::string& key) {
    for (size_t i = 0; i < kBoolFieldCount; ++i)
        if (key == kBoolFields[i].key) return &kBoolFields[i];
    return nullptr;
}

const ColorFieldDesc* findColorField(const std::string& key) {
    for (size_t i = 0; i < kColorFieldCount; ++i)
        if (key == kColorFields[i].key) return &kColorFields[i];
    return nullptr;
}

// =========================================================================
// Diff helper — covers all registry fields
// =========================================================================

ConfigDirtyFlags diffRegistryFields(const Config& old_cfg, const Config& new_cfg) {
    ConfigDirtyFlags flags = ConfigDirtyFlags::None;

    for (size_t i = 0; i < kStringFieldCount; ++i) {
        if (old_cfg.*kStringFields[i].member != new_cfg.*kStringFields[i].member)
            flags |= kStringFields[i].dirty;
    }
    for (size_t i = 0; i < kFloatFieldCount; ++i) {
        if (std::abs(old_cfg.*kFloatFields[i].member - new_cfg.*kFloatFields[i].member) > 0.001f)
            flags |= kFloatFields[i].dirty;
    }
    for (size_t i = 0; i < kIntFieldCount; ++i) {
        if (old_cfg.*kIntFields[i].member != new_cfg.*kIntFields[i].member)
            flags |= kIntFields[i].dirty;
    }
    for (size_t i = 0; i < kBoolFieldCount; ++i) {
        if (old_cfg.*kBoolFields[i].member != new_cfg.*kBoolFields[i].member)
            flags |= kBoolFields[i].dirty;
    }
    for (size_t i = 0; i < kColorFieldCount; ++i) {
        if (old_cfg.*kColorFields[i].member != new_cfg.*kColorFields[i].member)
            flags |= kColorFields[i].dirty;
    }

    return flags;
}

} // namespace termcore
