// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_shell_module.h
#pragma once

#include "termcore/lua_module.h"
#include <memory>
#include <string>

namespace sol { class protected_function; }

namespace termcore {

class ShellIntegrationConfig;

class LuaShellModule : public ILuaModule {
public:
    explicit LuaShellModule(ShellIntegrationConfig* config);

    std::string_view moduleName() const override { return "shell"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::PaneWrite;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

private:
    ShellIntegrationConfig* config_;
    std::shared_ptr<sol::protected_function> commandFinishFn_;
};

} // namespace termcore
