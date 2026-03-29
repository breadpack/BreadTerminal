#include "lua_provider_module.h"
#include "termcore/provider_registry.h"
#include "termcore/agent.h"
#include <sol/sol.hpp>

namespace termcore {

LuaProviderModule::LuaProviderModule(ProviderRegistry* registry,
                                     AgentTracker* agentTracker)
    : registry_(registry), agentTracker_(agentTracker) {}

void LuaProviderModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);
    (void)lua;

    terminal.set_function("provider",
        [this](const std::string& id, sol::table opts) {
            if (!registry_) return;
            ProviderInfo info;
            info.id = id;
            info.display_name = opts.get_or<std::string>("display_name", id);
            info.agent_type = opts.get_or<std::string>("agent_type", "Unknown");

            sol::optional<sol::table> procs = opts["detect_process"];
            if (procs) {
                for (auto& kv : *procs) {
                    if (kv.second.is<std::string>())
                        info.detect_process.push_back(kv.second.as<std::string>());
                }
            }

            sol::optional<sol::table> envs = opts["detect_env"];
            if (envs) {
                for (auto& kv : *envs) {
                    if (kv.second.is<std::string>())
                        info.detect_env.push_back(kv.second.as<std::string>());
                }
            }

            // Parse state_patterns
            sol::optional<sol::table> patterns = opts["state_patterns"];
            if (patterns) {
                for (auto& kv : *patterns) {
                    if (!kv.second.is<sol::table>()) continue;
                    sol::table entry = kv.second.as<sol::table>();
                    ProviderStatePattern sp;
                    sp.state = entry.get_or<std::string>("state", "");
                    sp.pattern = entry.get_or<std::string>("pattern", "");
                    if (!sp.state.empty() && !sp.pattern.empty()) {
                        info.state_patterns.push_back(std::move(sp));
                    }
                }
            }

            sol::optional<sol::table> hooks = opts["hooks"];
            if (hooks) {
                info.hooks.config_dir = hooks->get_or<std::string>("config_dir", "");
                info.hooks.settings_file = hooks->get_or<std::string>("settings_file", "");
                info.hooks.settings_format = hooks->get_or<std::string>("settings_format", "json");

                sol::optional<sol::table> events = (*hooks)["events"];
                if (events) {
                    for (auto& kv : *events) {
                        if (!kv.second.is<sol::table>()) continue;
                        sol::table evt = kv.second.as<sol::table>();
                        ProviderHookEvent he;
                        he.bread_event = evt.get_or<std::string>("bread_event", "");
                        he.hook_name = evt.get_or<std::string>("hook_name", "");

                        sol::optional<sol::table> emap = evt["env_map"];
                        if (emap) {
                            for (auto& m : *emap) {
                                if (m.first.is<std::string>() && m.second.is<std::string>()) {
                                    he.env_map.push_back({
                                        m.first.as<std::string>(),
                                        m.second.as<std::string>()
                                    });
                                }
                            }
                        }
                        info.hooks.events.push_back(std::move(he));
                    }
                }
            }

            registry_->registerProvider(std::move(info));
        });

    // terminal.agent sub-table with set_stale_timeout()
    sol::table agent = terminal["agent"].get_or_create<sol::table>();
    agent.set_function("set_stale_timeout",
        [this](int seconds) {
            if (registry_) {
                registry_->setStaleTimeout(seconds);
            }
            if (agentTracker_) {
                agentTracker_->setStaleTimeout(seconds);
            }
        });
}

}  // namespace termcore
