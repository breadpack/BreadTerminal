// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_session_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_session_module.h"
#include "termcore/session_manager.h"

namespace termcore {

LuaSessionModule::LuaSessionModule(MultiSessionManager* sessionMgr)
    : sessionMgr_(sessionMgr) {}

void LuaSessionModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    (void)lua;
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    auto session = terminal.create_named("session");

    // terminal.session.on_save(function(name) end)
    session.set_function("on_save",
        [this](sol::protected_function fn) {
            auto fnPtr = std::make_shared<sol::protected_function>(std::move(fn));
            onSaveFn_ = fnPtr;
            if (sessionMgr_) {
                sessionMgr_->onSave = [fnPtr](const std::string& name) {
                    if (fnPtr && fnPtr->valid()) {
                        (*fnPtr)(name);
                    }
                };
            }
        });

    // terminal.session.on_restore(function(name) end)
    session.set_function("on_restore",
        [this](sol::protected_function fn) {
            auto fnPtr = std::make_shared<sol::protected_function>(std::move(fn));
            onRestoreFn_ = fnPtr;
            if (sessionMgr_) {
                sessionMgr_->onRestore = [fnPtr](const std::string& name) {
                    if (fnPtr && fnPtr->valid()) {
                        (*fnPtr)(name);
                    }
                };
            }
        });

    // terminal.session.set_naming(function() return "custom-name" end)
    session.set_function("set_naming",
        [this](sol::protected_function fn) {
            auto fnPtr = std::make_shared<sol::protected_function>(std::move(fn));
            namingFn_ = fnPtr;
            if (sessionMgr_) {
                sessionMgr_->namingCallback = [fnPtr]() -> std::string {
                    if (fnPtr && fnPtr->valid()) {
                        auto result = (*fnPtr)();
                        if (result.valid()) {
                            sol::object val = result;
                            if (val.is<std::string>()) {
                                return val.as<std::string>();
                            }
                        }
                    }
                    return "";
                };
            }
        });
}

void LuaSessionModule::clearCallbacks() {
    onSaveFn_.reset();
    onRestoreFn_.reset();
    namingFn_.reset();
    if (sessionMgr_) {
        sessionMgr_->onSave     = nullptr;
        sessionMgr_->onRestore  = nullptr;
        sessionMgr_->namingCallback = nullptr;
    }
}

} // namespace termcore
