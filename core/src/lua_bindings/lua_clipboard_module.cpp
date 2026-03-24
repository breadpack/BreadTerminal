// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_clipboard_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_clipboard_module.h"
#include "termcore/clipboard_history.h"

namespace termcore {

LuaClipboardModule::LuaClipboardModule(ClipboardHistory* clipboard)
    : clipboard_(clipboard) {}

void LuaClipboardModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    (void)lua;

    auto clipboard = terminal.create_named("clipboard");

    // terminal.clipboard.set_history_size(50)
    clipboard.set_function("set_history_size",
        [this](int n) {
            if (clipboard_ && n > 0) {
                clipboard_->setMaxEntries(static_cast<size_t>(n));
            }
        });

    // terminal.clipboard.set_preview_length(120)
    clipboard.set_function("set_preview_length",
        [this](int n) {
            if (clipboard_ && n > 0) {
                clipboard_->setPreviewMaxLength(static_cast<size_t>(n));
            }
        });

    // terminal.clipboard.on_copy(function(text) end)
    clipboard.set_function("on_copy",
        [this](sol::protected_function fn) {
            auto luaFn = std::make_shared<sol::protected_function>(std::move(fn));
            if (clipboard_) {
                clipboard_->onCopyCallback = [luaFn](const std::string& text) {
                    (*luaFn)(text);
                };
            }
        });
}

void LuaClipboardModule::clearCallbacks() {
    if (clipboard_) {
        clipboard_->onCopyCallback = nullptr;
    }
}

} // namespace termcore
