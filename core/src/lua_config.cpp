#if TERMCORE_HAS_LUA

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "termcore/lua_config.h"
#include "termcore/keybinding.h"
#include "termcore/profile.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace termcore {
namespace {

// Global state for Lua config
static Config s_config;
static std::unique_ptr<LuaEngine> s_engine;

/// Parse a hex color from Lua: accepts 0xRRGGBB integer or "#RRGGBB" string.
uint32_t parseColor(const sol::object& obj) {
    if (obj.is<uint32_t>()) {
        return obj.as<uint32_t>();
    }
    if (obj.is<int64_t>()) {
        return static_cast<uint32_t>(obj.as<int64_t>());
    }
    if (obj.is<std::string>()) {
        std::string s = obj.as<std::string>();
        if (!s.empty() && s[0] == '#') s = s.substr(1);
        return static_cast<uint32_t>(std::stoul(s, nullptr, 16));
    }
    return 0;
}

/// Helper: get optional string from table
std::string getStr(const sol::table& t, const char* key, const std::string& def) {
    auto v = t[key];
    return v.valid() ? v.get<std::string>() : def;
}

/// Helper: get optional float from table
float getFloat(const sol::table& t, const char* key, float def) {
    auto v = t[key];
    return v.valid() ? v.get<float>() : def;
}

/// Helper: get optional int from table
int getInt(const sol::table& t, const char* key, int def) {
    auto v = t[key];
    return v.valid() ? v.get<int>() : def;
}

/// Helper: get optional color (hex int or "#RRGGBB" string) from table
uint32_t getColor(const sol::table& t, const char* key, uint32_t def) {
    auto v = t[key];
    return v.valid() ? parseColor(v) : def;
}

/// Helper: get optional bool from table
bool getBool(const sol::table& t, const char* key, bool def) {
    auto v = t[key];
    return v.valid() ? v.get<bool>() : def;
}

/// Apply a Lua table to Config struct.
/// Called from terminal.config({...})
void applyConfigTable(Config& cfg, const sol::table& t) {
    // Font
    cfg.font_family = getStr(t, "font_family", cfg.font_family);
    cfg.font_size = getFloat(t, "font_size", cfg.font_size);

    if (auto ff = t["font_features"]; ff.valid() && ff.get_type() == sol::type::table) {
        cfg.font_features.clear();
        sol::table features = ff;
        for (auto& [k, v] : features) {
            if (v.is<std::string>()) {
                cfg.font_features.push_back(v.as<std::string>());
            }
        }
    }

    // Colors
    if (auto v = t["background"]; v.valid()) cfg.background = parseColor(v);
    if (auto v = t["foreground"]; v.valid()) cfg.foreground = parseColor(v);
    if (auto v = t["cursor_color"]; v.valid()) cfg.cursor_color = parseColor(v);
    if (auto v = t["selection_background"]; v.valid()) cfg.selection_background = parseColor(v);
    if (auto v = t["selection_foreground"]; v.valid()) cfg.selection_foreground = parseColor(v);

    if (auto p = t["palette"]; p.valid() && p.get_type() == sol::type::table) {
        sol::table palette = p;
        for (int i = 1; i <= 16; ++i) {
            auto v = palette[i];
            if (v.valid()) {
                cfg.palette[i - 1] = parseColor(v);
            }
        }
    }

    // Window
    cfg.window_width = getInt(t, "window_width", cfg.window_width);
    cfg.window_height = getInt(t, "window_height", cfg.window_height);
    cfg.window_padding = getInt(t, "window_padding", cfg.window_padding);
    cfg.minimum_contrast = getFloat(t, "minimum_contrast", cfg.minimum_contrast);

    // Terminal
    cfg.scrollback_limit = getInt(t, "scrollback_limit", cfg.scrollback_limit);
    cfg.cursor_style = getStr(t, "cursor_style", cfg.cursor_style);
    cfg.cursor_blink = getBool(t, "cursor_blink", cfg.cursor_blink);
    cfg.cursor_blink_interval = getFloat(t, "cursor_blink_interval", cfg.cursor_blink_interval);
    cfg.shell = getStr(t, "shell", cfg.shell);

    // Clipboard
    cfg.clipboard_paste_protection = getStr(t, "clipboard_paste_protection", cfg.clipboard_paste_protection);
    cfg.clipboard_paste_bracketed_safe = getBool(t, "clipboard_paste_bracketed_safe", cfg.clipboard_paste_bracketed_safe);
    cfg.allow_clipboard_write = getBool(t, "allow_clipboard_write", cfg.allow_clipboard_write);

    // Clickable URLs
    cfg.clickable_urls = getBool(t, "clickable_urls", cfg.clickable_urls);
    cfg.url_color = getColor(t, "url_color", cfg.url_color);

    // Notifications
    cfg.notify_on_command_finish = getBool(t, "notify_on_command_finish", cfg.notify_on_command_finish);
    cfg.notify_after_seconds = getFloat(t, "notify_after_seconds", cfg.notify_after_seconds);

    // Appearance
    cfg.background_opacity = getFloat(t, "background_opacity", cfg.background_opacity);
    cfg.background_blur = getInt(t, "background_blur", cfg.background_blur);

    // Sidebar
    cfg.sidebar_visible = getBool(t, "sidebar_visible", cfg.sidebar_visible);
    cfg.sidebar_width = getInt(t, "sidebar_width", cfg.sidebar_width);

    // Theme
    cfg.theme = getStr(t, "theme", cfg.theme);

    // Quick terminal
    cfg.quick_terminal_hotkey = getStr(t, "quick_terminal_hotkey", cfg.quick_terminal_hotkey);
    cfg.quick_terminal_height = getFloat(t, "quick_terminal_height", cfg.quick_terminal_height);
    cfg.quick_terminal_animation_ms = getInt(t, "quick_terminal_animation_ms", cfg.quick_terminal_animation_ms);
    cfg.quick_terminal_position = getStr(t, "quick_terminal_position", cfg.quick_terminal_position);
    cfg.quick_terminal_auto_hide = getBool(t, "quick_terminal_auto_hide", cfg.quick_terminal_auto_hide);

    // Keybinding preset
    cfg.keybinding_preset = getStr(t, "keybinding_preset", cfg.keybinding_preset);
}

/// Initialize the Lua engine with terminal.config() and terminal.keymap() APIs.
void initConfigEngine(LuaEngine& engine, Config& cfg) {
    // terminal.config({...}) — set config values
    engine.registerFunction("__config_noop", [](const std::string&) -> std::string { return ""; });

    // We need direct sol access, so we use loadString to register the actual functions
    // after the engine is created. Instead, extend LuaEngine slightly.
    // For now, use loadString approach to define the Lua-side API.
}

/// Setup the sol state directly for config APIs.
/// This requires accessing the sol::state inside LuaEngine.
/// Since LuaEngine uses pimpl, we create a parallel sol state for config loading.
struct LuaConfigState {
    sol::state lua;
    Config config;
    std::vector<KeyBinding> keybindings;

    void init() {
        lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table,
                           sol::lib::math, sol::lib::io);

        auto terminal = lua.create_named_table("terminal");
        terminal["version"] = "0.1.0";

        // terminal.config({...})
        terminal.set_function("config", [this](sol::table t) {
            applyConfigTable(config, t);
        });

        // terminal.keymap("trigger", "action")
        terminal.set_function("keymap", [this](const std::string& trigger,
                                                const std::string& action) {
            keybindings.push_back({trigger, action});
        });

        // terminal.keymap_preset("name") — load a preset keybinding set
        // Available: "Ghostty", "Kitty", "tmux", "Warp", "Windows Terminal", "Alacritty", "iTerm2"
        terminal.set_function("keymap_preset", [this](const std::string& name) {
            config.keybinding_preset = name;
        });

        // terminal.colorscheme("name", { background = ..., ... })
        // Applies theme inline if the name matches config.theme,
        // or stores it for later retrieval.
        terminal.set_function("colorscheme", [this](const std::string& name,
                                                     sol::table t) {
            // If this colorscheme matches the current theme, apply it
            if (config.theme == name || config.theme.empty()) {
                if (auto v = t["background"]; v.valid()) config.background = parseColor(v);
                if (auto v = t["foreground"]; v.valid()) config.foreground = parseColor(v);
                if (auto v = t["cursor_color"]; v.valid()) config.cursor_color = parseColor(v);
                if (auto v = t["selection_background"]; v.valid()) config.selection_background = parseColor(v);
                if (auto v = t["selection_foreground"]; v.valid()) config.selection_foreground = parseColor(v);
                if (auto p = t["palette"]; p.valid() && p.get_type() == sol::type::table) {
                    sol::table palette = p;
                    for (int i = 1; i <= 16; ++i) {
                        auto v = palette[i];
                        if (v.valid()) config.palette[i - 1] = parseColor(v);
                    }
                }
            }
        });

        // terminal.on("event", handler) — for plugin-style event handlers
        terminal.set_function("on", [](const std::string&, sol::protected_function) {
            // Event handlers are a no-op during config loading.
            // They will be re-registered when the LuaEngine is initialized later.
        });

        // terminal.profile({...}) — define a shell profile
        terminal.set_function("profile", [this](sol::table t) {
            Profile p;
            p.id = getStr(t, "id", "");
            if (p.id.empty()) return;

            p.name = getStr(t, "name", "");
            p.command = getStr(t, "command", "");
            if (auto a = t["args"]; a.valid() && a.get_type() == sol::type::table) {
                sol::table args = a;
                for (auto& [k, v] : args) {
                    if (v.is<std::string>()) p.args.push_back(v.as<std::string>());
                }
            }
            p.working_dir = getStr(t, "working_dir", "");
            p.icon = getStr(t, "icon", "");

            // Optional appearance (type-checked)
            if (auto v = t["theme"]; v.valid() && v.get_type() == sol::type::string)
                p.theme = v.get<std::string>();
            if (auto v = t["font_family"]; v.valid() && v.get_type() == sol::type::string)
                p.font_family = v.get<std::string>();
            if (auto v = t["font_size"]; v.valid() && v.get_type() == sol::type::number)
                p.font_size = v.get<float>();
            if (auto v = t["cursor_style"]; v.valid() && v.get_type() == sol::type::string)
                p.cursor_style = v.get<std::string>();

            config.profiles.push_back(p);
        });

        // terminal.default_profile("id") — set the default profile
        terminal.set_function("default_profile", [this](const std::string& id) {
            config.default_profile_id = id;
        });

        // terminal.hide_profile("id") — hide a profile from the UI
        terminal.set_function("hide_profile", [this](const std::string& id) {
            auto& v = config.hidden_profile_ids;
            if (std::find(v.begin(), v.end(), id) == v.end())
                v.push_back(id);
        });

        // Utility: terminal.platform
#if defined(__APPLE__)
        terminal["platform"] = "macos";
#elif defined(_WIN32)
        terminal["platform"] = "windows";
#else
        terminal["platform"] = "linux";
#endif
    }

    bool loadFile(const std::string& path) {
        auto result = lua.safe_script_file(path, sol::script_pass_on_error);
        if (!result.valid()) {
            sol::error err = result;
            last_error = err.what();
            return false;
        }
        // Merge keybindings into config
        config.keybindings = keybindings;
        return true;
    }

    bool loadString(const std::string& code) {
        auto result = lua.safe_script(code, sol::script_pass_on_error);
        if (!result.valid()) {
            sol::error err = result;
            last_error = err.what();
            return false;
        }
        config.keybindings = keybindings;
        return true;
    }

    std::string last_error;
};

static std::unique_ptr<LuaConfigState> s_lua_state;

} // anonymous namespace

Result<void> loadConfigLua(const std::string& path) {
    s_lua_state = std::make_unique<LuaConfigState>();
    s_lua_state->init();
    if (!s_lua_state->loadFile(path)) {
        return Error("failed to load config: " + s_lua_state->last_error);
    }
    s_config = s_lua_state->config;
    return {};
}

Result<void> loadConfigLuaString(const std::string& code) {
    s_lua_state = std::make_unique<LuaConfigState>();
    s_lua_state->init();
    if (!s_lua_state->loadString(code)) {
        return Error("failed to parse config: " + s_lua_state->last_error);
    }
    s_config = s_lua_state->config;
    return {};
}

const Config& luaConfig() {
    return s_config;
}

LuaEngine* luaConfigEngine() {
    return s_engine.get();
}

std::string defaultLuaConfigPath() {
    std::string base = defaultConfigPath();
    if (base.empty()) return "";

    // Try config.lua in the same directory as config
    namespace fs = std::filesystem;
    fs::path dir = fs::path(base).parent_path();
    fs::path lua_path = dir / "config.lua";

    if (fs::exists(lua_path)) {
        return lua_path.string();
    }
    return "";
}

bool writeDefaultLuaConfig(const std::string& path) {
    namespace fs = std::filesystem;
    if (fs::exists(path)) return false;

    // Create parent directories
    auto parent = fs::path(path).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        fs::create_directories(parent, ec);
    }

    std::ofstream f(path);
    if (!f) return false;

    f << R"(-- BreadTerminal Configuration
-- Lua-based config file. Edit this to customize your terminal.
-- Docs: https://github.com/breadpack/BreadTerminal

-- Font settings
terminal.config({
    font_family = ")" <<
#if defined(__APPLE__)
    "Menlo"
#elif defined(_WIN32)
    "Consolas"
#else
    "monospace"
#endif
    << R"(",
    font_size = 14,
    -- font_features = { "calt", "liga" },
})

-- Appearance
terminal.config({
    theme = "Catppuccin Mocha",
    background_opacity = 1.0,
    background_blur = 0,  -- 0=none, 1=low, 2=medium, 3=high
    -- window_padding = 4,
})

-- Terminal behavior
terminal.config({
    scrollback_limit = 10000,
    cursor_style = "block",   -- "block", "underline", "bar"
    cursor_blink = true,
    -- shell = "/bin/zsh",
})

-- Clipboard
terminal.config({
    clipboard_paste_protection = "multiline",  -- "never", "multiline", "always"
    clipboard_paste_bracketed_safe = true,
    allow_clipboard_write = false,
})

-- Notifications
terminal.config({
    notify_on_command_finish = true,
    notify_after_seconds = 5.0,
})

-- Quick terminal (visor mode)
-- terminal.config({ quick_terminal_hotkey = "ctrl+`" })

-- Keybinding preset: use keybindings from another terminal emulator.
-- Available presets: "Ghostty", "Kitty", "tmux", "Warp", "Windows Terminal", "Alacritty", "iTerm2"
-- terminal.keymap_preset("Ghostty")  -- or use: terminal.config({ keybinding_preset = "Ghostty" })

-- Custom keybindings (applied on top of the preset or default)
-- Platform modifier: use "ctrl" on Windows/Linux, "cmd" on macOS.
local mod = terminal.platform == "macos" and "cmd" or "ctrl"

-- Examples (uncomment to customize):
-- terminal.keymap(mod .. "+t",          "new_tab")
-- terminal.keymap(mod .. "+w",          "close_tab")
-- terminal.keymap(mod .. "+shift+]",    "next_tab")
-- terminal.keymap(mod .. "+shift+[",    "prev_tab")
-- terminal.keymap(mod .. "+d",          "split_right")
-- terminal.keymap(mod .. "+shift+d",    "split_down")
-- terminal.keymap(mod .. "+f",          "search_open")
-- terminal.keymap(mod .. "+,",          "open_settings")
-- terminal.keymap(mod .. "+shift+t",    "open_theme_hub")
-- terminal.keymap(mod .. "+shift+p",    "open_font_hub")
-- terminal.keymap(mod .. "+shift+x",    "enter_copy_mode")
-- terminal.keymap(mod .. "+enter",      "toggle_fullscreen")

-- Custom colorscheme (optional)
-- terminal.colorscheme("my_custom_theme", {
--     background = 0x1a1b26,
--     foreground = 0xc0caf5,
--     cursor_color = 0xc0caf5,
--     palette = {
--         0x15161e, 0xf7768e, 0x9ece6a, 0xe0af68,
--         0x7aa2f7, 0xbb9af7, 0x7dcfff, 0xa9b1d6,
--         0x414868, 0xf7768e, 0x9ece6a, 0xe0af68,
--         0x7aa2f7, 0xbb9af7, 0x7dcfff, 0xc0caf5,
--     },
-- })

-- Plugin support: you can register event handlers
-- terminal.on("bell", function(data)
--     terminal.log("Bell rang!")
-- end)
)";

    return true;
}

} // namespace termcore

#else // !TERMCORE_HAS_LUA

// Stub implementations when Lua is not available
#include "termcore/lua_config.h"

namespace termcore {

static Config s_stub_config;

Result<void> loadConfigLua(const std::string&) { return Error("Lua not available"); }
Result<void> loadConfigLuaString(const std::string&) { return Error("Lua not available"); }
const Config& luaConfig() { return s_stub_config; }
LuaEngine* luaConfigEngine() { return nullptr; }
std::string defaultLuaConfigPath() { return ""; }
bool writeDefaultLuaConfig(const std::string&) { return false; }

} // namespace termcore

#endif // TERMCORE_HAS_LUA
