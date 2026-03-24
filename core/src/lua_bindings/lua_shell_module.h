// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_shell_module.h
#pragma once

#include "termcore/lua_module.h"
#include <memory>
#include <string>

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
    // Stored as shared_ptr<void> to avoid sol.hpp in header
    std::shared_ptr<void> commandFinishFn_;
};

} // namespace termcore
