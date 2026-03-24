// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_settings_module.h
#pragma once

#include "termcore/lua_module.h"
#include <functional>
#include <string>
#include <vector>

namespace termcore {

class SettingsModel;

/// A single Lua-defined settings field.
struct LuaSettingField {
    std::string key;
    std::string label;
    std::string type;          // "toggle", "text", "number", "dropdown"
    std::string default_value; // string representation
    std::vector<std::string> options; // for type="dropdown"
};

class LuaSettingsModule : public ILuaModule {
public:
    explicit LuaSettingsModule(SettingsModel* model);

    std::string_view moduleName() const override { return "settings"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::UI;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

private:
    SettingsModel* model_;
};

} // namespace termcore
