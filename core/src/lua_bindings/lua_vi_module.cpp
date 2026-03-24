// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_vi_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_vi_module.h"
#include "termcore/vi_copy_mode.h"

namespace termcore {

LuaViModule::LuaViModule(ViCopyMode* vi)
    : vi_(vi) {}

void LuaViModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    (void)lua;
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    auto vi = terminal.create_named("vi");

    // terminal.vi.set_word_chars("a-zA-Z0-9_-")
    vi.set_function("set_word_chars",
        [this](const std::string& chars) {
            vi_->setWordChars(chars);
        });

    // terminal.vi.on_yank(function(text) end)
    vi.set_function("on_yank",
        [this](sol::protected_function fn) {
            auto luaFn = std::make_shared<sol::protected_function>(std::move(fn));
            vi_->setOnYank([luaFn](const std::string& text) {
                auto result = (*luaFn)(text);
                (void)result;
            });
        });

    // terminal.vi.map("gd", function() end)
    vi.set_function("map",
        [this](const std::string& key, sol::protected_function fn) {
            auto luaFn = std::make_shared<sol::protected_function>(std::move(fn));
            vi_->mapKey(key, [luaFn]() {
                auto result = (*luaFn)();
                (void)result;
            });
        });
}

void LuaViModule::clearCallbacks() {
    if (vi_) vi_->clearLuaCallbacks();
}

} // namespace termcore
