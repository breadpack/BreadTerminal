#pragma once

#include "termcore/lua_module.h"
#include <memory>
#include <string>

namespace termcore {

class ProviderRegistry;

class LuaHooksModule : public ILuaModule {
public:
    explicit LuaHooksModule(ProviderRegistry* registry);

    std::string_view moduleName() const override { return "hooks"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Config;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

    /// Called by C++ when a new AI CLI provider is detected in a pane.
    /// Fires all registered Lua "on_provider_detected" handlers.
    void fireProviderDetected(const std::string& provider_id, uint32_t pane_id);

private:
    ProviderRegistry* registry_;
    void* luaPtr_ = nullptr;
    struct Handlers;
    std::shared_ptr<Handlers> handlers_;
};

} // namespace termcore
