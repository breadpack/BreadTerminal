#pragma once

#include <string>
#include <vector>

namespace termcore {
struct ProviderInfo;
struct ProviderHooksConfig;
}

namespace bread {

// Forward declaration of ProviderRegistry-like data for standalone CLI
struct ProviderEntry {
    std::string id;
    std::string display_name;
    std::string config_dir;
    std::string settings_file;
    std::string settings_format;
    struct HookEvent {
        std::string bread_event;
        std::string hook_name;
        std::vector<std::pair<std::string, std::string>> env_map;
    };
    std::vector<HookEvent> events;
    bool has_hooks() const { return !config_dir.empty(); }
};

/// Get built-in provider entries (standalone, no Lua needed)
std::vector<ProviderEntry> getBuiltinProviders();

/// Install hooks for a specific provider by ID.
int installHooksForProvider(const std::string& provider_id);

/// Install hooks for all providers that have hook configs.
int installAllHooks();

/// Show installation status for all providers.
int showHooksStatus();

/// Legacy: install Claude Code hooks (backwards compatible).
int installHooks();

}  // namespace bread
