// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_event_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_event_module.h"

#include <unordered_map>
#include <vector>

namespace termcore {

struct LuaEventModule::EventHandlers {
    std::unordered_map<std::string, std::vector<std::shared_ptr<sol::protected_function>>> map;

    struct OnceHandler {
        std::shared_ptr<sol::protected_function> fn;
        std::shared_ptr<bool> fired;
    };
    std::unordered_map<std::string, std::vector<OnceHandler>> once_map;
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

    // terminal.event.emit("custom:event_name", optional_data_table)
    ev.set_function("emit",
        [this](std::string name, sol::optional<sol::table> data) {
            fireModuleEvent(name, data ? static_cast<void*>(&data.value()) : nullptr);
        });

    // terminal.event.once("event_name", function(data) end) — fires handler only once
    ev.set_function("once",
        [this](std::string name, sol::protected_function fn) {
            auto luaFn = std::make_shared<sol::protected_function>(std::move(fn));
            auto fired = std::make_shared<bool>(false);
            handlers_->once_map[name].push_back({std::move(luaFn), fired});
        });

    // terminal.event.off("event_name") — remove all handlers for an event
    ev.set_function("off",
        [this](std::string name) {
            handlers_->map.erase(name);
            handlers_->once_map.erase(name);
        });
}

void LuaEventModule::fireModuleEvent(const std::string& name, void* eventData) {
    // Copy handler lists to protect against modification during iteration.
    // Lua callbacks may call event.off() or event.on() which would invalidate iterators.

    // Fire regular handlers
    std::vector<std::shared_ptr<sol::protected_function>> regular_copy;
    {
        auto it = handlers_->map.find(name);
        if (it != handlers_->map.end()) {
            regular_copy = it->second;  // copy the vector
        }
    }
    for (auto& fn : regular_copy) {
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

    // Fire once handlers and remove them
    std::vector<EventHandlers::OnceHandler> once_copy;
    {
        auto once_it = handlers_->once_map.find(name);
        if (once_it != handlers_->once_map.end()) {
            once_copy = std::move(once_it->second);
            handlers_->once_map.erase(once_it);
        }
    }
    for (auto& handler : once_copy) {
        if (!handler.fn || *handler.fired) continue;
        *handler.fired = true;
        sol::protected_function_result result;
        if (eventData) {
            auto* tbl = static_cast<sol::table*>(eventData);
            result = (*handler.fn)(*tbl);
        } else {
            result = (*handler.fn)();
        }
        (void)result;
    }
}

void LuaEventModule::clearCallbacks() {
    handlers_->map.clear();
    handlers_->once_map.clear();
    luaPtr_ = nullptr;
}

} // namespace termcore
