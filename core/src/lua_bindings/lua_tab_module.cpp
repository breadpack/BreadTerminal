// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_tab_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_tab_module.h"
#include "termcore/tab_controller.h"

namespace termcore {

LuaTabModule::LuaTabModule(TabController* tabCtrl)
    : tabCtrl_(tabCtrl) {}

void LuaTabModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    // Store lua state pointer for creating tables in callbacks
    luaPtr_ = luaState;
    sol::state* luaPtr = &lua;

    auto tab = terminal.create_named("tab");

    // terminal.tab.on_title_format(function(info) return "..." end)
    tab.set_function("on_title_format",
        [this, luaPtr](sol::protected_function fn) {
            auto luaFn = std::make_shared<sol::protected_function>(std::move(fn));
            titleFormatFn_ = [luaFn, luaPtr](const TabTitleInfo& info) -> std::string {
                sol::table tbl = luaPtr->create_table();
                tbl["tab_index"] = info.tab_index;
                tbl["process"] = info.process_name;
                tbl["cwd"] = info.working_dir;
                tbl["title"] = info.title;
                tbl["is_active"] = info.is_active;

                auto result = (*luaFn)(tbl);
                if (result.valid()) {
                    sol::object val = result;
                    if (val.is<std::string>()) {
                        return val.as<std::string>();
                    }
                }
                return "";  // fallback: use C++ default
            };
        });

    // terminal.tab.set_title(tab_id, title)
    tab.set_function("set_title",
        [this](int tab_id, const std::string& title) {
            tabCtrl_->setCustomTitle(tab_id, title);
        });

    // terminal.tab.get_info(tab_id) -- returns a table with tab info
    tab.set_function("get_info",
        [this, luaPtr](int tab_id) -> sol::object {
            auto tabs = tabCtrl_->tabBarInfo();
            if (tab_id < 0 || tab_id >= static_cast<int>(tabs.size())) {
                return sol::nil;
            }
            const auto& info = tabs[static_cast<size_t>(tab_id)];
            sol::table tbl = luaPtr->create_table();
            tbl["tab_index"]    = tab_id;
            tbl["title"]        = info.title;
            tbl["process"]      = info.process_name;
            tbl["icon"]         = info.icon_name;
            tbl["is_active"]    = info.active;
            tbl["has_unread"]   = info.has_unread;
            tbl["needs_attention"] = info.needs_attention;
            std::string ct = tabCtrl_->customTitle(tab_id);
            if (!ct.empty()) tbl["custom_title"] = ct;
            return sol::object(tbl);
        });

    // terminal.tab.list() -- returns array of tab info tables
    tab.set_function("list",
        [this, luaPtr]() -> sol::table {
            auto tabs = tabCtrl_->tabBarInfo();
            sol::table result = luaPtr->create_table();
            for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
                const auto& info = tabs[static_cast<size_t>(i)];
                sol::table tbl = luaPtr->create_table();
                tbl["tab_index"]    = i;
                tbl["title"]        = info.title;
                tbl["process"]      = info.process_name;
                tbl["icon"]         = info.icon_name;
                tbl["is_active"]    = info.active;
                tbl["has_unread"]   = info.has_unread;
                tbl["needs_attention"] = info.needs_attention;
                std::string ct = tabCtrl_->customTitle(i);
                if (!ct.empty()) tbl["custom_title"] = ct;
                result[i + 1] = tbl;
            }
            return result;
        });

    // terminal.tab.set_process_icon(process, icon)
    tab.set_function("set_process_icon",
        [this](const std::string& process, const std::string& icon) {
            if (tabCtrl_) tabCtrl_->setProcessIcon(process, icon);
        });
}

void LuaTabModule::clearCallbacks() {
    titleFormatFn_ = nullptr;
    luaPtr_ = nullptr;
}

} // namespace termcore
