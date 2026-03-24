// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_theme_module.h
#pragma once

#include "termcore/lua_module.h"
#include <functional>
#include <memory>
#include <string>

namespace termcore {

struct Config;

class LuaThemeModule : public ILuaModule {
public:
    explicit LuaThemeModule(Config* config);

    std::string_view moduleName() const override { return "theme"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Config;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

    // Schedule callback: called with current hour, returns theme name
    using ScheduleFn = std::function<std::string(int hour)>;
    ScheduleFn scheduleCallback() const { return scheduleFn_; }

private:
    Config* config_;
    ScheduleFn scheduleFn_;
};

} // namespace termcore
