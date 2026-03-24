// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_search_module.h
#pragma once

#include "termcore/lua_module.h"
#include <functional>
#include <string>

namespace termcore {

class SearchController;

class LuaSearchModule : public ILuaModule {
public:
    explicit LuaSearchModule(SearchController* searchCtrl);

    std::string_view moduleName() const override { return "search"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::PaneRead;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

private:
    SearchController* searchCtrl_;
};

} // namespace termcore
