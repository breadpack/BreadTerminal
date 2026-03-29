// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_timer_module.h
#pragma once

#include "termcore/lua_module.h"
#include <cstdint>
#include <memory>

namespace termcore {

class LuaTimerModule : public ILuaModule {
public:
    LuaTimerModule();
    ~LuaTimerModule();

    std::string_view moduleName() const override { return "timer"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Events;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

    // Called by host to advance timers. Returns number of timers fired.
    int tick(uint64_t now_ms);

    // Check if any timers are pending
    bool hasPendingTimers() const;

    // Get the number of active timers (for testing)
    size_t activeTimerCount() const;


private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termcore
