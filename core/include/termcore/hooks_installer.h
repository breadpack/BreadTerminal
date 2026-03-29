#pragma once

#include <string>
#include <vector>

namespace termcore {

struct HookEntry {
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

/// Get built-in provider hook entries.
std::vector<HookEntry> getBuiltinHookEntries();

/// Check if hooks are installed for a given provider (hardcoded entries).
bool isHooksInstalled(const std::string& provider_id);

/// Install hooks for a specific provider by ID (hardcoded entries). Returns 0 on success.
int installHooksForProvider(const std::string& provider_id);

struct ProviderHooksConfig;  // forward declaration from provider_registry.h

/// Check if hooks are installed using ProviderRegistry hook config.
bool isHooksInstalledFromConfig(const ProviderHooksConfig& hooks);

/// Install hooks using ProviderRegistry hook config. Returns 0 on success.
int installHooksFromConfig(const std::string& provider_id, const ProviderHooksConfig& hooks);

}  // namespace termcore
