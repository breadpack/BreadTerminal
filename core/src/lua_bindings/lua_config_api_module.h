// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_config_api_module.h
#pragma once

#include "termcore/lua_module.h"

namespace termcore {

struct Config;
class KeybindingManager;

/// Exposes terminal.config(), terminal.keymap(), terminal.colorscheme()
/// to the LuaEngine so that embedded defaults/*.lua scripts can use them.
class LuaConfigApiModule : public ILuaModule {
public:
    LuaConfigApiModule(Config* config, KeybindingManager* keybindings);

    std::string_view moduleName() const override { return "config_api"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Config;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override {}

private:
    Config* config_;
    KeybindingManager* keybindings_;
};

} // namespace termcore
