#ifndef TERMCORE_LUA_COMPLETION_MODULE_H
#define TERMCORE_LUA_COMPLETION_MODULE_H

#include "termcore/lua_module.h"
#include <string>
#include <vector>

namespace termcore {

class CompletionManager;

class LuaCompletionModule : public ILuaModule {
public:
    explicit LuaCompletionModule(CompletionManager* manager);

    std::string_view moduleName() const override { return "completion"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::PaneWrite;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

private:
    CompletionManager* manager_;
    std::vector<std::string> luaProviderNames_;
};

} // namespace termcore

#endif // TERMCORE_LUA_COMPLETION_MODULE_H
