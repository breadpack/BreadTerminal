#ifndef TERMCORE_LUA_CONFIG_H
#define TERMCORE_LUA_CONFIG_H

#include "termcore/config.h"
#include "termcore/lua_engine.h"

#include <string>

namespace termcore {

/// Load configuration from a Lua file (e.g., config.lua).
/// The Lua script can call:
///   terminal.config({ font_family = "...", font_size = 14, ... })
///   terminal.keymap("ctrl+t", "new_tab")
///   terminal.colorscheme("my_theme", { background = 0x1e1e2e, ... })
///
/// Returns true if the file was loaded successfully.
/// The resulting Config is stored and can be retrieved via luaConfig().
bool loadConfigLua(const std::string& path);

/// Load configuration from a Lua string (for testing).
bool loadConfigLuaString(const std::string& code);

/// Get the config populated by the last successful Lua load.
const Config& luaConfig();

/// Get the Lua engine used for config (for plugin/event integration).
LuaEngine* luaConfigEngine();

/// Get the default Lua config file path.
/// Returns ~/.config/breadterminal/config.lua (Linux),
///         ~/Library/Application Support/BreadTerminal/config.lua (macOS),
///         %APPDATA%/BreadTerminal/config.lua (Windows).
/// Returns empty string if not found; caller should fall back to defaultConfigPath().
std::string defaultLuaConfigPath();

/// Write a default config.lua template if none exists.
bool writeDefaultLuaConfig(const std::string& path);

} // namespace termcore
#endif
