// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_quick_module.h
#pragma once

#include "termcore/lua_module.h"
#include <string>

namespace termcore {

struct Config;

class LuaQuickModule : public ILuaModule {
public:
    explicit LuaQuickModule(Config* config);

    std::string_view moduleName() const override { return "quick"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Config;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

private:
    Config* config_;
};

} // namespace termcore
