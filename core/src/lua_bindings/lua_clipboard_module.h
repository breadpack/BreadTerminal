// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_clipboard_module.h
#pragma once

#include "termcore/lua_module.h"
#include <functional>
#include <string>

namespace termcore {

class ClipboardHistory;

class LuaClipboardModule : public ILuaModule {
public:
    explicit LuaClipboardModule(ClipboardHistory* clipboard);

    std::string_view moduleName() const override { return "clipboard"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Clipboard;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

private:
    ClipboardHistory* clipboard_;
};

} // namespace termcore
