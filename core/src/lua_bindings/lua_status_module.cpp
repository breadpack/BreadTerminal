// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_status_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_status_module.h"
#include "termcore/pane_status.h"
#include "termcore/tab_controller.h"

namespace termcore {

LuaStatusModule::LuaStatusModule(TabController* tabCtrl)
    : tabCtrl_(tabCtrl) {}

void LuaStatusModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    (void)lua;
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    auto status = terminal.create_named("status");

    // terminal.status.set_pill(pane_id, {key="Build", value="OK", color="#00ff00"})
    status.set_function("set_pill",
        [this](int paneId, sol::table opts) {
            if (!tabCtrl_) return;
            auto* paneState = tabCtrl_->paneById(static_cast<PaneId>(paneId));
            if (!paneState) return;

            std::string key   = opts.get_or<std::string>("key", "");
            std::string value = opts.get_or<std::string>("value", "");
            uint32_t color = 0;

            // Accept color as hex string "#rrggbb" or integer
            sol::object colorObj = opts["color"];
            if (colorObj.is<std::string>()) {
                std::string colorStr = colorObj.as<std::string>();
                if (!colorStr.empty() && colorStr[0] == '#') {
                    colorStr = colorStr.substr(1);
                }
                try {
                    color = static_cast<uint32_t>(std::stoul(colorStr, nullptr, 16));
                } catch (...) {}
            } else if (colorObj.is<int>()) {
                color = static_cast<uint32_t>(colorObj.as<int>());
            }

            // Use a module-local store held by pane state data
            // Since PaneStatusStore is not accessible directly through TabController,
            // we hold our own store per module instance.
            store_.getOrCreate(static_cast<PaneId>(paneId))
                  .setStatusPillFromLua(key, value, color);
        });

    // terminal.status.set_progress(pane_id, value, label)
    status.set_function("set_progress",
        [this](int paneId, float value, sol::optional<std::string> label) {
            if (!tabCtrl_) return;
            auto* paneState = tabCtrl_->paneById(static_cast<PaneId>(paneId));
            if (!paneState) return;
            std::string lbl = label.value_or("");
            store_.getOrCreate(static_cast<PaneId>(paneId)).setProgress(value, lbl);
        });

    // terminal.status.log(pane_id, level, message)
    status.set_function("log",
        [this](int paneId, const std::string& level, const std::string& message) {
            if (!tabCtrl_) return;
            auto* paneState = tabCtrl_->paneById(static_cast<PaneId>(paneId));
            if (!paneState) return;
            LogEntry::Level lvl = LogEntry::Info;
            if (level == "success") lvl = LogEntry::Success;
            else if (level == "warning") lvl = LogEntry::Warning;
            else if (level == "error")   lvl = LogEntry::Error;
            store_.getOrCreate(static_cast<PaneId>(paneId))
                  .addLog(lvl, message, "lua");
        });
}

void LuaStatusModule::clearCallbacks() {
    // No stored callbacks to clear for this module
}

} // namespace termcore
