// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_search_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_search_module.h"
#include "termcore/search_controller.h"

namespace termcore {

LuaSearchModule::LuaSearchModule(SearchController* searchCtrl)
    : searchCtrl_(searchCtrl) {}

void LuaSearchModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    sol::state* luaPtr = &lua;

    auto search = terminal.create_named("search");

    // terminal.search.set_debounce(100)
    search.set_function("set_debounce",
        [this](int ms) {
            if (searchCtrl_) {
                searchCtrl_->setDebounceMs(ms);
            }
        });

    // terminal.search.on_result(function(matches) end)
    search.set_function("on_result",
        [this, luaPtr](sol::protected_function fn) {
            auto luaFn = std::make_shared<sol::protected_function>(std::move(fn));
            if (searchCtrl_) {
                searchCtrl_->onResultCallback = [luaFn, luaPtr](const std::vector<SearchMatch>& matches) {
                    sol::table tbl = luaPtr->create_table();
                    for (size_t i = 0; i < matches.size(); ++i) {
                        sol::table m = luaPtr->create_table();
                        m["row"] = matches[i].row;
                        m["start_col"] = matches[i].start_col;
                        m["end_col"] = matches[i].end_col;
                        tbl[static_cast<int>(i + 1)] = m;
                    }
                    (*luaFn)(tbl);
                };
            }
        });
}

void LuaSearchModule::clearCallbacks() {
    if (searchCtrl_) {
        searchCtrl_->onResultCallback = nullptr;
    }
}

} // namespace termcore
