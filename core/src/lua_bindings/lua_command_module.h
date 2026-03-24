// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_command_module.h
#pragma once

#include "termcore/lua_module.h"
#include <functional>
#include <string>

namespace termcore {

class CommandPalette;

class LuaCommandModule : public ILuaModule {
public:
    explicit LuaCommandModule(CommandPalette* palette);

    std::string_view moduleName() const override { return "command"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Keybindings;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

private:
    CommandPalette* palette_;
};

} // namespace termcore
