// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_command_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_command_module.h"
#include "termcore/command_palette.h"

namespace termcore {

LuaCommandModule::LuaCommandModule(CommandPalette* palette)
    : palette_(palette) {}

void LuaCommandModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    (void)lua;
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    auto cmd = terminal.create_named("command");

    // terminal.command.register("name", function() end)
    // terminal.command.register("name", function() end, {category="Plugin"})
    cmd.set_function("register",
        [this](std::string name, sol::protected_function fn, sol::optional<sol::table> opts) {
            std::string category = "Plugin";
            if (opts.has_value()) {
                auto cat = (*opts).get<sol::optional<std::string>>("category");
                if (cat.has_value()) category = *cat;
            }
            auto luaFn = std::make_shared<sol::protected_function>(std::move(fn));
            if (!palette_) return;
            palette_->registerLuaCommand(name,
                [luaFn]() {
                    auto result = (*luaFn)();
                    (void)result;
                },
                category);
        });

    // terminal.command.remove("name")
    cmd.set_function("remove",
        [this](std::string name) {
            if (!palette_) return;
            palette_->removeLuaCommand(name);
        });
}

void LuaCommandModule::clearCallbacks() {
    if (!palette_) return;
    // Remove all registered Lua commands from the palette
    // Collect names first to avoid modifying while iterating
    std::vector<std::string> names;
    for (const auto& cmd : palette_->luaCommands()) {
        names.push_back(cmd.name);
    }
    for (const auto& name : names) {
        palette_->removeLuaCommand(name);
    }
}

} // namespace termcore
