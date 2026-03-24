// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_mux_module.h
#pragma once

#include "termcore/lua_module.h"
#include "termcore/mux.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace termcore {

class TabController;

class LuaMuxModule : public ILuaModule {
public:
    LuaMuxModule(Mux* mux, TabController* tabCtrl);

    std::string_view moduleName() const override { return "mux"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::PaneWrite;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

private:
    Mux* mux_;
    TabController* tabCtrl_;

    // Named layout callbacks: layout name -> Lua function handler
    struct LayoutCallbacks;
    std::shared_ptr<LayoutCallbacks> layoutCallbacks_;
};

} // namespace termcore
