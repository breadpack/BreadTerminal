#pragma once
#include "termcore/lua_module.h"

namespace termcore {
class ProviderRegistry;
class AgentTracker;

class LuaProviderModule : public ILuaModule {
public:
    explicit LuaProviderModule(ProviderRegistry* registry,
                               AgentTracker* agentTracker = nullptr);
    std::string_view moduleName() const override { return "provider"; }
    PluginCapability requiredCapability() const override { return PluginCapability::Config; }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override {}

    void setAgentTracker(AgentTracker* tracker) { agentTracker_ = tracker; }

private:
    ProviderRegistry* registry_;
    AgentTracker* agentTracker_ = nullptr;
};
}  // namespace termcore
