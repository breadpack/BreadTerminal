// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_session_module.h
#pragma once

#include "termcore/lua_module.h"
#include <memory>
#include <string>

namespace termcore {

class MultiSessionManager;

class LuaSessionModule : public ILuaModule {
public:
    explicit LuaSessionModule(MultiSessionManager* sessionMgr);

    std::string_view moduleName() const override { return "session"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Config;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

private:
    MultiSessionManager* sessionMgr_;
    // Stored as shared_ptr<void> to avoid sol.hpp in header
    std::shared_ptr<void> onSaveFn_;
    std::shared_ptr<void> onRestoreFn_;
    std::shared_ptr<void> namingFn_;
};

} // namespace termcore
