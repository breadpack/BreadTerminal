// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_workspace_module.h
#pragma once

#include "termcore/lua_module.h"
#include <functional>
#include <string>

namespace termcore {

class WorkspaceStatusProvider;

class LuaWorkspaceModule : public ILuaModule {
public:
    explicit LuaWorkspaceModule(WorkspaceStatusProvider* provider);

    std::string_view moduleName() const override { return "workspace"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Events;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

private:
    WorkspaceStatusProvider* provider_;
    void* luaPtr_ = nullptr;  // sol::state* — stored as void* to avoid sol.hpp in header
};

} // namespace termcore
