// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_settings_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_settings_module.h"
#include "termcore/settings_model.h"

namespace termcore {

LuaSettingsModule::LuaSettingsModule(SettingsModel* model)
    : model_(model) {}

void LuaSettingsModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    (void)lua;
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    auto settings = terminal.create_named("settings");

    // terminal.settings.add_category("My Plugin", { field, ... })
    // Each field is a table with keys: key, label, type, default, options
    settings.set_function("add_category",
        [this](const std::string& name, sol::table fields) {
            std::vector<SettingItem> items;

            for (auto& kv : fields) {
                sol::table field = kv.second.as<sol::table>();

                std::string key   = field.get_or<std::string>("key",   "");
                std::string label = field.get_or<std::string>("label", "");
                std::string type  = field.get_or<std::string>("type",  "text");

                if (key.empty()) continue;

                SettingType setting_type = SettingType::Text;
                if (type == "toggle")   setting_type = SettingType::Toggle;
                else if (type == "number")   setting_type = SettingType::Number;
                else if (type == "dropdown") setting_type = SettingType::Dropdown;

                SettingMeta meta;

                // Collect dropdown options if provided
                sol::optional<sol::table> opts_opt = field.get<sol::optional<sol::table>>("options");
                if (opts_opt) {
                    for (auto& opt_kv : *opts_opt) {
                        meta.options.push_back(opt_kv.second.as<std::string>());
                    }
                }

                items.push_back({key, label, /*description*/"", setting_type, meta, false});
            }

            if (model_ && !items.empty()) {
                model_->addLuaCategory(name, items);
            }
        });
}

void LuaSettingsModule::clearCallbacks() {
    if (model_) {
        model_->clearLuaCategories();
    }
}

} // namespace termcore
