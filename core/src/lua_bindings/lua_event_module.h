// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_event_module.h
#pragma once

#include "termcore/lua_module.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace termcore {

class LuaEventModule : public ILuaModule {
public:
    LuaEventModule();

    std::string_view moduleName() const override { return "event"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Events;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

    // Called by C++ code to fire an event to all registered Lua handlers.
    // eventData is a sol::table* cast to void* (may be nullptr for no-data events).
    void fireModuleEvent(const std::string& name, void* eventData);

private:
    // Opaque handler storage — implementation details in .cpp
    struct EventHandlers;
    std::shared_ptr<EventHandlers> handlers_;
    void* luaPtr_ = nullptr;  // sol::state*, set in registerBindings
};

} // namespace termcore
