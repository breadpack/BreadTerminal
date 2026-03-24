// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_workspace_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_workspace_module.h"
#include "termcore/workspace_status.h"

namespace termcore {

LuaWorkspaceModule::LuaWorkspaceModule(WorkspaceStatusProvider* provider)
    : provider_(provider) {}

void LuaWorkspaceModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    luaPtr_ = luaState;
    sol::state* luaPtr = &lua;

    auto workspace = terminal.create_named("workspace");

    // terminal.workspace.on_status_change(function() end)
    workspace.set_function("on_status_change",
        [this](sol::protected_function fn) {
            auto luaFn = std::make_shared<sol::protected_function>(std::move(fn));
            provider_->setOnChanged(
                [luaFn](const std::vector<WorkspaceStatusSnapshot>&) {
                    auto result = (*luaFn)();
                    (void)result;
                });
        });

    // terminal.workspace.set_cwd(workspace_id, "/path")
    workspace.set_function("set_cwd",
        [this](uint32_t ws_id, const std::string& cwd) {
            provider_->setCwd(ws_id, cwd);
        });

    // terminal.workspace.get_status() -- returns current status snapshot as array of tables
    workspace.set_function("get_status",
        [this, luaPtr]() -> sol::table {
            auto snapshots = provider_->currentSnapshots();
            sol::table result = luaPtr->create_table();
            int i = 1;
            for (const auto& snap : snapshots) {
                sol::table tbl = luaPtr->create_table();
                tbl["id"]                     = snap.id;
                tbl["name"]                   = snap.name;
                tbl["git_branch"]             = snap.git_branch;
                tbl["cwd"]                    = snap.cwd;
                tbl["is_active"]              = snap.is_active;
                tbl["unread_notifications"]   = snap.unread_notification_count;
                result[i++] = tbl;
            }
            return result;
        });
}

void LuaWorkspaceModule::clearCallbacks() {
    // Reset the provider callback to a no-op to sever Lua references
    provider_->setOnChanged(nullptr);
    luaPtr_ = nullptr;
}

} // namespace termcore
