// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_vi_module.h
#pragma once

#include "termcore/lua_module.h"
#include <string>

namespace termcore {

class ViCopyMode;

class LuaViModule : public ILuaModule {
public:
    explicit LuaViModule(ViCopyMode* vi);

    std::string_view moduleName() const override { return "vi"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Keybindings;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

private:
    ViCopyMode* vi_;
};

} // namespace termcore
