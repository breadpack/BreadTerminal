// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_notify_module.h
#pragma once

#include "termcore/lua_module.h"
#include <functional>
#include <string>

namespace termcore {

class NotificationStore;

class LuaNotifyModule : public ILuaModule {
public:
    explicit LuaNotifyModule(NotificationStore* store);

    std::string_view moduleName() const override { return "notify"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Notifications;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

private:
    NotificationStore* store_;
};

} // namespace termcore
