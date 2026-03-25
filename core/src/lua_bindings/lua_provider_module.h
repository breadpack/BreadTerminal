#pragma once
#include "termcore/lua_module.h"

namespace termcore {
class ProviderRegistry;

class LuaProviderModule : public ILuaModule {
public:
    explicit LuaProviderModule(ProviderRegistry* registry);
    std::string_view moduleName() const override { return "provider"; }
    PluginCapability requiredCapability() const override { return PluginCapability::Config; }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override {}
private:
    ProviderRegistry* registry_;
};
}  // namespace termcore
