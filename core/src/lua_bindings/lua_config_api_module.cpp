// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_config_api_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_config_api_module.h"
#include "termcore/config.h"
#include "termcore/keybinding.h"

namespace termcore {
namespace {

uint32_t parseColor(const sol::object& obj) {
    if (obj.is<uint32_t>()) return obj.as<uint32_t>();
    if (obj.is<int64_t>()) return static_cast<uint32_t>(obj.as<int64_t>());
    if (obj.is<std::string>()) {
        std::string s = obj.as<std::string>();
        if (!s.empty() && s[0] == '#') s = s.substr(1);
        return static_cast<uint32_t>(std::stoul(s, nullptr, 16));
    }
    return 0;
}

std::string getStr(const sol::table& t, const char* key, const std::string& def) {
    auto v = t[key];
    if (!v.valid() || v.get_type() != sol::type::string) return def;
    return v.get<std::string>();
}

float getFloat(const sol::table& t, const char* key, float def) {
    auto v = t[key];
    if (!v.valid() || v.get_type() != sol::type::number) return def;
    return v.get<float>();
}

int getInt(const sol::table& t, const char* key, int def) {
    auto v = t[key];
    if (!v.valid() || v.get_type() != sol::type::number) return def;
    return v.get<int>();
}

bool getBool(const sol::table& t, const char* key, bool def) {
    auto v = t[key];
    if (!v.valid() || v.get_type() != sol::type::boolean) return def;
    return v.get<bool>();
}

void applyConfigTable(Config& cfg, const sol::table& t) {
    cfg.font_family = getStr(t, "font_family", cfg.font_family);
    cfg.font_size = getFloat(t, "font_size", cfg.font_size);

    if (auto ff = t["font_features"]; ff.valid() && ff.get_type() == sol::type::table) {
        cfg.font_features.clear();
        sol::table features = ff;
        for (auto& [k, v] : features) {
            if (v.is<std::string>()) cfg.font_features.push_back(v.as<std::string>());
        }
    }

    if (auto fb = t["font_fallback"]; fb.valid() && fb.get_type() == sol::type::table) {
        cfg.font_fallback.clear();
        sol::table fallbacks = fb;
        for (auto& [k, v] : fallbacks) {
            if (v.is<std::string>()) cfg.font_fallback.push_back(v.as<std::string>());
        }
    }

    cfg.font_subpixel = getStr(t, "font_subpixel", cfg.font_subpixel);
    cfg.font_hinting = getStr(t, "font_hinting", cfg.font_hinting);
    cfg.font_ligatures = getBool(t, "font_ligatures", cfg.font_ligatures);

    if (auto v = t["background"]; v.valid()) cfg.background = parseColor(v);
    if (auto v = t["foreground"]; v.valid()) cfg.foreground = parseColor(v);
    if (auto v = t["cursor_color"]; v.valid()) cfg.cursor_color = parseColor(v);
    if (auto v = t["selection_background"]; v.valid()) cfg.selection_background = parseColor(v);
    if (auto v = t["selection_foreground"]; v.valid()) cfg.selection_foreground = parseColor(v);

    if (auto p = t["palette"]; p.valid() && p.get_type() == sol::type::table) {
        sol::table palette = p;
        for (int i = 1; i <= 16; ++i) {
            auto v = palette[i];
            if (v.valid()) cfg.palette[i - 1] = parseColor(v);
        }
    }

    cfg.window_width = getInt(t, "window_width", cfg.window_width);
    cfg.window_height = getInt(t, "window_height", cfg.window_height);
    cfg.window_padding = getInt(t, "window_padding", cfg.window_padding);
    cfg.minimum_contrast = getFloat(t, "minimum_contrast", cfg.minimum_contrast);

    cfg.scrollback_limit = getInt(t, "scrollback_limit", cfg.scrollback_limit);
    cfg.cursor_style = getStr(t, "cursor_style", cfg.cursor_style);
    cfg.cursor_blink = getBool(t, "cursor_blink", cfg.cursor_blink);
    cfg.cursor_blink_interval = getFloat(t, "cursor_blink_interval", cfg.cursor_blink_interval);
    cfg.shell = getStr(t, "shell", cfg.shell);

    cfg.clipboard_paste_protection = getStr(t, "clipboard_paste_protection", cfg.clipboard_paste_protection);
    cfg.clipboard_paste_bracketed_safe = getBool(t, "clipboard_paste_bracketed_safe", cfg.clipboard_paste_bracketed_safe);
    cfg.allow_clipboard_write = getBool(t, "allow_clipboard_write", cfg.allow_clipboard_write);

    cfg.clickable_urls = getBool(t, "clickable_urls", cfg.clickable_urls);
    cfg.url_color = getInt(t, "url_color", cfg.url_color);

    cfg.notify_on_command_finish = getBool(t, "notify_on_command_finish", cfg.notify_on_command_finish);
    cfg.notify_after_seconds = getFloat(t, "notify_after_seconds", cfg.notify_after_seconds);

    cfg.background_opacity = getFloat(t, "background_opacity", cfg.background_opacity);
    cfg.background_blur = getFloat(t, "background_blur", cfg.background_blur);
    cfg.background_blur_mode = getStr(t, "background_blur_mode", cfg.background_blur_mode);
    cfg.background_blur_material = getStr(t, "background_blur_material", cfg.background_blur_material);

    cfg.custom_shader = getStr(t, "custom_shader", cfg.custom_shader);
    cfg.shader_intensity = getFloat(t, "shader_intensity", cfg.shader_intensity);

    cfg.sidebar_visible = getBool(t, "sidebar_visible", cfg.sidebar_visible);
    cfg.sidebar_width = getInt(t, "sidebar_width", cfg.sidebar_width);

    cfg.theme = getStr(t, "theme", cfg.theme);

    cfg.quick_terminal_hotkey = getStr(t, "quick_terminal_hotkey", cfg.quick_terminal_hotkey);
    cfg.quick_terminal_height = getFloat(t, "quick_terminal_height", cfg.quick_terminal_height);
    cfg.quick_terminal_animation_ms = getInt(t, "quick_terminal_animation_ms", cfg.quick_terminal_animation_ms);
    cfg.quick_terminal_position = getStr(t, "quick_terminal_position", cfg.quick_terminal_position);
    cfg.quick_terminal_auto_hide = getBool(t, "quick_terminal_auto_hide", cfg.quick_terminal_auto_hide);

    cfg.auto_detect_high_contrast = getBool(t, "auto_detect_high_contrast", cfg.auto_detect_high_contrast);
    cfg.respect_reduced_motion = getBool(t, "respect_reduced_motion", cfg.respect_reduced_motion);

    cfg.image_preview = getBool(t, "image_preview", cfg.image_preview);
    cfg.image_preview_max_height = getInt(t, "image_preview_max_height", cfg.image_preview_max_height);

    cfg.session_autosave = getBool(t, "session_autosave", cfg.session_autosave);
    cfg.session_autosave_interval = getInt(t, "session_autosave_interval", cfg.session_autosave_interval);

    cfg.keybinding_preset = getStr(t, "keybinding_preset", cfg.keybinding_preset);

    if (auto p = t["tab_process_icons"]; p.valid() && p.get_type() == sol::type::table) {
        sol::table icons = p;
        icons.for_each([&](const sol::object& key, const sol::object& val) {
            if (key.get_type() == sol::type::string && val.get_type() == sol::type::string) {
                cfg.tab_process_icons[key.as<std::string>()] = val.as<std::string>();
            }
        });
    }
}

} // anonymous namespace

LuaConfigApiModule::LuaConfigApiModule(Config* config, KeybindingManager* keybindings)
    : config_(config), keybindings_(keybindings) {}

void LuaConfigApiModule::registerBindings(void* luaState, void* terminalTable) {
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    // terminal.config({...}) — apply config values
    terminal.set_function("config", [this](sol::table t) {
        if (!config_) return;
        applyConfigTable(*config_, t);
    });

    // terminal.keymap("trigger", "action") — register keybinding
    terminal.set_function("keymap", [this](const std::string& trigger,
                                           const std::string& action) {
        if (!keybindings_) return;
        auto combo = KeybindingManager::parseCombo(trigger);
        auto act = KeybindingManager::parseAction(action);
        keybindings_->bind(combo, act, act == Action::Custom ? action : "");
    });

    // terminal.keymap_preset("name") — load preset keybindings
    terminal.set_function("keymap_preset", [this](const std::string& name) {
        if (!keybindings_) return;
        auto preset = parseKeymapPreset(name);
        keybindings_->loadPreset(preset);
    });

    // terminal.colorscheme("name", { ... }) — define/apply a color theme
    terminal.set_function("colorscheme",
        [this](const std::string& name, sol::table t) {
            if (!config_) return;
            if (config_->theme == name || config_->theme.empty()) {
                if (auto v = t["background"]; v.valid()) config_->background = parseColor(v);
                if (auto v = t["foreground"]; v.valid()) config_->foreground = parseColor(v);
                if (auto v = t["cursor_color"]; v.valid()) config_->cursor_color = parseColor(v);
                if (auto v = t["selection_background"]; v.valid()) config_->selection_background = parseColor(v);
                if (auto v = t["selection_foreground"]; v.valid()) config_->selection_foreground = parseColor(v);
                if (auto p = t["palette"]; p.valid() && p.get_type() == sol::type::table) {
                    sol::table palette = p;
                    for (int i = 1; i <= 16; ++i) {
                        auto v = palette[i];
                        if (v.valid()) config_->palette[i - 1] = parseColor(v);
                    }
                }
            }
        });
}

} // namespace termcore
