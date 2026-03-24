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

    // terminal.tab.set_title(tab_id, title) -- stub, full impl in Phase 2
    tab.set_function("set_title", [](int, std::string) {});

    // terminal.tab.get_info(tab_id) -- stub, full impl in Phase 2
    tab.set_function("get_info", [](int) -> sol::object { return sol::nil; });

    // terminal.tab.list() -- stub, full impl in Phase 2
    tab.set_function("list", []() -> sol::object { return sol::nil; });
}

void LuaTabModule::clearCallbacks() {
    titleFormatFn_ = nullptr;
}

} // namespace termcore
