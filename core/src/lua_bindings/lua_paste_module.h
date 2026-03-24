// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_paste_module.h
#pragma once

#include "termcore/lua_module.h"
#include <string>

namespace termcore {

class PasteGuard;

class LuaPasteModule : public ILuaModule {
public:
    explicit LuaPasteModule(PasteGuard* pasteGuard);

    std::string_view moduleName() const override { return "paste"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Clipboard;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

private:
    PasteGuard* pasteGuard_;
};

} // namespace termcore
