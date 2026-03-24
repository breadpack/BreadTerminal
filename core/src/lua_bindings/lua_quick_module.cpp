// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_quick_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_quick_module.h"
#include "termcore/config.h"

namespace termcore {

LuaQuickModule::LuaQuickModule(Config* config)
    : config_(config) {}

void LuaQuickModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    (void)lua;
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    auto quick = terminal.create_named("quick");

    // terminal.quick.set_animation("slide", {duration=200, easing="ease-out"})
    // Note: easing string is accepted but stored as duration only (Config has ms field).
    quick.set_function("set_animation",
        [this](const std::string& /*style*/, sol::optional<sol::table> opts) {
            if (config_ && opts) {
                sol::optional<int> dur = opts->get<sol::optional<int>>("duration");
                if (dur) config_->quick_terminal_animation_ms = *dur;
            }
        });

    // terminal.quick.set_size(0.4)  -- fraction of screen (0.1 to 1.0)
    quick.set_function("set_size",
        [this](float size) {
            if (config_) {
                if (size < 0.1f) size = 0.1f;
                if (size > 1.0f) size = 1.0f;
                config_->quick_terminal_height = size;
            }
        });

    // terminal.quick.set_position("top")  -- "top", "bottom", "left", "right"
    quick.set_function("set_position",
        [this](const std::string& pos) {
            if (config_) {
                if (pos == "top" || pos == "bottom" || pos == "left" || pos == "right") {
                    config_->quick_terminal_position = pos;
                }
            }
        });
}

void LuaQuickModule::clearCallbacks() {
    // No stored Lua functions — nothing to clear.
}

} // namespace termcore
