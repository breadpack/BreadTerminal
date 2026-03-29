#pragma once

#include <set>
#include <string>
#include <vector>

namespace termcore {

struct ProviderEnvMapping {
    std::string bread_field;   // e.g. "agent_id"
    std::string tool_env_var;  // e.g. "CLAUDE_AGENT_ID"
};

struct ProviderHookEvent {
    std::string bread_event;   // e.g. "SubagentStart"
    std::string hook_name;     // e.g. "subagent-start"
    std::vector<ProviderEnvMapping> env_map;
};

struct ProviderHooksConfig {
    std::string config_dir;
    std::string settings_file;
    std::string settings_format;
    std::vector<ProviderHookEvent> events;
    bool empty() const { return config_dir.empty(); }
};

/// State pattern definition stored on a provider.
struct ProviderStatePattern {
    std::string state;    // e.g. "thinking", "tool_use", "waiting", "error", "idle"
    std::string pattern;  // Substring to match in terminal output
};

struct ProviderInfo {
    std::string id;
    std::string display_name;
    std::string agent_type;
    std::vector<std::string> detect_process;
    std::vector<std::string> detect_env;
    ProviderHooksConfig hooks;
    std::vector<ProviderStatePattern> state_patterns;
};

class ProviderRegistry {
public:
    void registerProvider(ProviderInfo info);
    const ProviderInfo* findById(const std::string& id) const;
    const ProviderInfo* findByAgentType(const std::string& agent_type) const;
    const ProviderInfo* detect(const std::string& process_name,
                               const std::vector<std::string>& env_vars) const;
    const std::vector<ProviderInfo>& all() const { return providers_; }
    void markInstalled(const std::string& provider_id);
    bool isInstalled(const std::string& provider_id) const;

    /// Configurable stale timeout (seconds). Default 60.
    void setStaleTimeout(int seconds) { stale_timeout_seconds_ = seconds; }
    int staleTimeout() const { return stale_timeout_seconds_; }

private:
    std::vector<ProviderInfo> providers_;
    std::set<std::string> installed_;
    int stale_timeout_seconds_ = 60;
};

}  // namespace termcore
