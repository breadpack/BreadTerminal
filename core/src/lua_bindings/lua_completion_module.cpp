#include "lua_completion_module.h"
#include "termcore/completion_manager.h"
#include <sol/sol.hpp>
#include <algorithm>

namespace termcore {

LuaCompletionModule::LuaCompletionModule(CompletionManager* manager)
    : manager_(manager) {}

void LuaCompletionModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    sol::table completion = terminal["completion"].get_or_create<sol::table>();

    completion["register_provider"] = [this](const std::string& name,
                                              sol::table options) {
        if (!manager_) return;

        int priority = options.get_or("priority", 50);
        bool async = options.get_or("async", false);

        sol::optional<sol::protected_function> onInputOpt = options["on_input"];

        if (!async && onInputOpt) {
            auto onInput = std::make_shared<sol::protected_function>(*onInputOpt);

            CompletionManager::Provider prov;
            prov.name = name;
            prov.priority = priority;
            prov.getSuggestion = [onInput](const std::string& input,
                                            const std::string& cwd) -> std::string {
                sol::state_view sv(onInput->lua_state());
                sol::table ctx = sv.create_table();
                ctx["text"] = input;
                ctx["cwd"] = cwd;
                auto result = (*onInput)(ctx);
                if (result.valid()) {
                    sol::object val = result;
                    if (val.is<std::string>()) {
                        return val.as<std::string>();
                    }
                }
                return "";
            };
            manager_->registerProvider(std::move(prov));
        } else if (async) {
            CompletionManager::Provider prov;
            prov.name = name;
            prov.priority = priority;
            prov.getSuggestion = nullptr;
            manager_->registerProvider(std::move(prov));
        }
        luaProviderNames_.push_back(name);
    };

    completion["remove_provider"] = [this](const std::string& name) {
        if (!manager_) return;
        manager_->removeProvider(name);
        luaProviderNames_.erase(
            std::remove(luaProviderNames_.begin(), luaProviderNames_.end(), name),
            luaProviderNames_.end());
    };

    completion["set_suggestion"] = [this](const std::string& providerName,
                                           const std::string& text) {
        if (!manager_) return;
        manager_->setSuggestion(providerName, text);
    };

    completion["set_enabled"] = [this](bool enabled) {
        if (!manager_) return;
        manager_->setEnabled(enabled);
    };
}

void LuaCompletionModule::clearCallbacks() {
    if (manager_) {
        for (const auto& name : luaProviderNames_) {
            manager_->removeProvider(name);
        }
    }
    luaProviderNames_.clear();
}

} // namespace termcore
