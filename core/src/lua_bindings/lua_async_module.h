// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_async_module.h
#pragma once

#include "termcore/lua_module.h"
#include <cstdint>
#include <memory>

namespace termcore {

class LuaTimerModule;

class LuaAsyncModule : public ILuaModule {
public:
    LuaAsyncModule();
    ~LuaAsyncModule();

    std::string_view moduleName() const override { return "async"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Events;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

    // Resume pending coroutines. Called by host.
    void tick(uint64_t now_ms);

    // Set timer module reference for sleep support
    void setTimerModule(LuaTimerModule* timer);

    bool hasPendingCoroutines() const;

    // Get the number of active coroutines (for testing)
    size_t activeCoroutineCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termcore
