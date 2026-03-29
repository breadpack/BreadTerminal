// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_json_module.h
#pragma once

#include "termcore/lua_module.h"

namespace termcore {

class LuaJsonModule : public ILuaModule {
public:
    std::string_view moduleName() const override { return "json"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Events;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override {}  // No callbacks to clear
};

} // namespace termcore
