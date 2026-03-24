// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_tab_module.h
#pragma once

#include "termcore/lua_module.h"
#include <functional>
#include <string>

namespace termcore {

class TabController;

// Info struct passed to Lua title format callback
struct TabTitleInfo {
    int tab_index = 0;
    std::string process_name;
    std::string working_dir;
    std::string title;
    bool is_active = false;
};

class LuaTabModule : public ILuaModule {
public:
    explicit LuaTabModule(TabController* tabCtrl);

    std::string_view moduleName() const override { return "tab"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Events;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

    // Called by TabController to format a tab title via Lua
    using TitleFormatFn = std::function<std::string(const TabTitleInfo&)>;
    TitleFormatFn titleFormatCallback() const { return titleFormatFn_; }

private:
    TabController* tabCtrl_;
    TitleFormatFn titleFormatFn_;
    void* luaPtr_ = nullptr;  // sol::state* — stored as void* to avoid sol.hpp in header
};

} // namespace termcore
