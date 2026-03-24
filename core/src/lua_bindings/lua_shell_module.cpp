// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_shell_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_shell_module.h"
#include "termcore/shell_integration.h"

namespace termcore {

LuaShellModule::LuaShellModule(ShellIntegrationConfig* config)
    : config_(config) {}

void LuaShellModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    (void)lua;
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    auto shell = terminal.create_named("shell");

    // terminal.shell.set_env("KEY", "value")
    shell.set_function("set_env",
        [this](const std::string& key, const std::string& value) {
            if (config_) {
                config_->addCustomEnv(key, value);
            }
        });

    // terminal.shell.on_command_finish(function(exit_code, duration) end)
    shell.set_function("on_command_finish",
        [this](sol::protected_function fn) {
            commandFinishFn_ = std::make_shared<sol::protected_function>(std::move(fn));
            if (config_) {
                auto fnPtr = commandFinishFn_;
                config_->onCommandFinish = [fnPtr](int exitCode, double duration) {
                    if (fnPtr && fnPtr->valid()) {
                        (*fnPtr)(exitCode, duration);
                    }
                };
            }
        });

    // terminal.shell.set_ssh_term("xterm-256color")
    shell.set_function("set_ssh_term",
        [this](const std::string& term) {
            if (config_) {
                config_->setSshTerm(term);
            }
        });
}

void LuaShellModule::clearCallbacks() {
    commandFinishFn_.reset();
    if (config_) {
        config_->onCommandFinish = nullptr;
    }
}

} // namespace termcore
