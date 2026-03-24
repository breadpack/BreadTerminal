// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_notify_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_notify_module.h"
#include "termcore/notification.h"

namespace termcore {

LuaNotifyModule::LuaNotifyModule(NotificationStore* store)
    : store_(store) {}

void LuaNotifyModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    sol::state* luaPtr = &lua;

    auto notify = terminal.create_named("notify");

    // terminal.notify.send("Title", "Body", "normal")
    notify.set_function("send",
        [this](std::string title, std::string body, sol::optional<std::string> urgencyStr) {
            if (!store_) return;
            NotificationUrgency urgency = NotificationUrgency::Normal;
            if (urgencyStr.has_value()) {
                const auto& u = urgencyStr.value();
                if (u == "low")      urgency = NotificationUrgency::Low;
                else if (u == "critical") urgency = NotificationUrgency::Critical;
            }
            store_->add(0, NotificationSource::Agent, urgency, title, body);
        });

    // terminal.notify.set_max(200)
    notify.set_function("set_max",
        [this](int n) {
            if (store_ && n > 0) {
                store_->setMaxNotifications(static_cast<size_t>(n));
            }
        });

    // terminal.notify.deduplicate(5)
    notify.set_function("deduplicate",
        [this](int seconds) {
            if (store_) {
                store_->setDeduplicateWindowSec(seconds);
            }
        });

    // terminal.notify.on_receive(function(n) end)
    notify.set_function("on_receive",
        [this, luaPtr](sol::protected_function fn) {
            auto luaFn = std::make_shared<sol::protected_function>(std::move(fn));
            if (store_) {
                store_->setCallback([luaFn, luaPtr](const Notification& n) {
                    sol::table tbl = luaPtr->create_table();
                    tbl["id"] = n.id;
                    tbl["title"] = n.title;
                    tbl["body"] = n.body;
                    tbl["read"] = n.read;
                    tbl["pane_id"] = n.pane_id;
                    (*luaFn)(tbl);
                });
            }
        });
}

void LuaNotifyModule::clearCallbacks() {
    if (store_) {
        store_->setCallback(nullptr);
    }
}

} // namespace termcore
