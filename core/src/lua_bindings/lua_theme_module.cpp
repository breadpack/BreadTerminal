// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_theme_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_theme_module.h"
#include "termcore/config.h"
#include "termcore/theme_loader.h"

namespace termcore {

LuaThemeModule::LuaThemeModule(Config* config)
    : config_(config) {}

void LuaThemeModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    sol::state* luaPtr = &lua;

    auto theme = terminal.create_named("theme");

    // terminal.theme.switch("name") -- sets config_->theme = name
    theme.set_function("switch",
        [this](std::string name) {
            if (config_) config_->theme = std::move(name);
        });

    // terminal.theme.current() -- returns config_->theme
    theme.set_function("current",
        [this]() -> std::string {
            if (config_) return config_->theme;
            return "";
        });

    // terminal.theme.list() -- returns table of available theme names
    theme.set_function("list",
        [luaPtr]() -> sol::table {
            sol::table result = luaPtr->create_table();
            auto themes = allAvailableThemes();
            int i = 1;
            for (const auto& t : themes) {
                result[i++] = t.name;
            }
            return result;
        });

    // terminal.theme.on_schedule(function(hour) return "dark" end)
    theme.set_function("on_schedule",
        [this](sol::protected_function fn) {
            auto luaFn = std::make_shared<sol::protected_function>(std::move(fn));
            scheduleFn_ = [luaFn](int hour) -> std::string {
                auto result = (*luaFn)(hour);
                if (result.valid()) {
                    sol::object val = result;
                    if (val.is<std::string>()) {
                        return val.as<std::string>();
                    }
                }
                return "";
            };
        });
}

void LuaThemeModule::clearCallbacks() {
    scheduleFn_ = nullptr;
}

} // namespace termcore
