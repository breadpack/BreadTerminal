// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_event_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_event_module.h"

#include <unordered_map>
#include <vector>

namespace termcore {

struct LuaEventModule::EventHandlers {
    std::unordered_map<std::string, std::vector<std::shared_ptr<sol::protected_function>>> map;
};

LuaEventModule::LuaEventModule()
    : handlers_(std::make_shared<EventHandlers>()) {}

void LuaEventModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    (void)lua;
    auto& terminal = *static_cast<sol::table*>(terminalTable);
    luaPtr_ = luaState;

    auto ev = terminal.create_named("event");

    // terminal.event.on("event_name", function(data) end)
    ev.set_function("on",
        [this](std::string name, sol::protected_function fn) {
            auto luaFn = std::make_shared<sol::protected_function>(std::move(fn));
            handlers_->map[name].push_back(std::move(luaFn));
        });
}

void LuaEventModule::fireModuleEvent(const std::string& name, void* eventData) {
    auto it = handlers_->map.find(name);
    if (it == handlers_->map.end()) return;

    for (auto& fn : it->second) {
        if (!fn) continue;
        sol::protected_function_result result;
        if (eventData) {
            auto* tbl = static_cast<sol::table*>(eventData);
            result = (*fn)(*tbl);
        } else {
            result = (*fn)();
        }
        (void)result;
    }
}

void LuaEventModule::clearCallbacks() {
    handlers_->map.clear();
    luaPtr_ = nullptr;
}

} // namespace termcore
