#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_hooks_module.h"
#include "termcore/hooks_installer.h"
#include "termcore/provider_registry.h"

namespace termcore {

struct LuaHooksModule::Handlers {
    std::vector<std::shared_ptr<sol::protected_function>> on_detected;
};

LuaHooksModule::LuaHooksModule(ProviderRegistry* registry)
    : registry_(registry)
    , handlers_(std::make_shared<Handlers>()) {}

void LuaHooksModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);
    luaPtr_ = luaState;

    auto hooks = terminal.create_named("hooks");

    // terminal.hooks.is_installed(provider_id) -> bool
    hooks.set_function("is_installed",
        [this](const std::string& provider_id) -> bool {
            if (!registry_) return true;
            const auto* info = registry_->findById(provider_id);
            if (!info || info->hooks.empty()) return true;
            return isHooksInstalledFromConfig(info->hooks);
        });

    // terminal.hooks.install(provider_id) -> bool
    hooks.set_function("install",
        [this](const std::string& provider_id) -> bool {
            if (!registry_) return false;
            const auto* info = registry_->findById(provider_id);
            if (!info || info->hooks.empty()) return false;
            return installHooksFromConfig(provider_id, info->hooks) == 0;
        });

    // terminal.hooks.on_provider_detected(function(data) end)
    hooks.set_function("on_provider_detected",
        [this](sol::protected_function fn) {
            handlers_->on_detected.push_back(
                std::make_shared<sol::protected_function>(std::move(fn)));
        });
}

void LuaHooksModule::fireProviderDetected(const std::string& provider_id, uint32_t pane_id) {
    if (!luaPtr_ || handlers_->on_detected.empty()) return;

    auto& lua = *static_cast<sol::state*>(luaPtr_);
    sol::table data = lua.create_table();
    data["provider_id"] = provider_id;
    data["pane_id"] = pane_id;

    for (auto& fn : handlers_->on_detected) {
        if (!fn) continue;
        auto result = (*fn)(data);
        (void)result;
    }
}

void LuaHooksModule::clearCallbacks() {
    handlers_->on_detected.clear();
    luaPtr_ = nullptr;
}

} // namespace termcore
