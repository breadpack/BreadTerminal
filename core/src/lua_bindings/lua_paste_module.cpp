// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_paste_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_paste_module.h"
#include "termcore/paste_guard.h"

namespace termcore {

LuaPasteModule::LuaPasteModule(PasteGuard* pasteGuard)
    : pasteGuard_(pasteGuard) {}

void LuaPasteModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    (void)lua;

    auto paste = terminal.create_named("paste");

    // terminal.paste.add_danger("DROP TABLE", "Dangerous SQL")
    paste.set_function("add_danger",
        [this](std::string pattern, std::string description) {
            if (pasteGuard_) {
                pasteGuard_->addCustomDanger(pattern, description);
            }
        });

    // terminal.paste.add_compound_danger("rm -rf", "~", "Home directory wipe")
    // Both pattern1 and pattern2 must appear in the pasted text.
    paste.set_function("add_compound_danger",
        [this](std::string pattern1, std::string pattern2, std::string description) {
            if (pasteGuard_) {
                pasteGuard_->addCompoundDanger(pattern1, pattern2, description);
            }
        });

    // terminal.paste.add_pipe_danger("curl", "Curl piped to shell")
    // Checks that sourceCmd appears before a pipe and sh/bash/zsh after it.
    paste.set_function("add_pipe_danger",
        [this](std::string sourceCmd, std::string description) {
            if (pasteGuard_) {
                pasteGuard_->addPipeDanger(sourceCmd, description);
            }
        });

    // terminal.paste.whitelist("sudo apt update")
    paste.set_function("whitelist",
        [this](std::string pattern) {
            if (pasteGuard_) {
                pasteGuard_->addWhitelist(pattern);
            }
        });

    // terminal.paste.set_mode("multiline")
    paste.set_function("set_mode",
        [this](std::string mode) {
            if (pasteGuard_) {
                pasteGuard_->setModeFromString(mode);
            }
        });
}

void LuaPasteModule::clearCallbacks() {
    // No callbacks to clear; stateful patterns are kept across unload intentionally
    // but could be reset if needed. For now, this is a no-op.
}

} // namespace termcore
