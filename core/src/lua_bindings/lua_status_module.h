// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_status_module.h
#pragma once

#include "termcore/lua_module.h"
#include "termcore/pane_status.h"
#include <functional>
#include <string>

namespace termcore {

class TabController;

class LuaStatusModule : public ILuaModule {
public:
    explicit LuaStatusModule(TabController* tabCtrl);

    std::string_view moduleName() const override { return "status"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::PaneRead;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

    // Expose the status store for the renderer/other components to read
    const PaneStatusStore& statusStore() const { return store_; }

private:
    TabController* tabCtrl_;
    PaneStatusStore store_;
};

} // namespace termcore
