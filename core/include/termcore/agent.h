#ifndef TERMCORE_AGENT_H
#define TERMCORE_AGENT_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace termcore {

/// Known AI coding agent types
enum class AgentType : uint8_t {
    Unknown,
    ClaudeCode,    // Claude Code (Anthropic)
    Codex,         // OpenAI Codex CLI
    GeminiCli,     // Google Gemini CLI
    Aider,         // Aider
    OpenCode,      // OpenCode
    Goose,         // Goose
    Amp,           // Amp
    Cline,         // Cline
    Custom,        // User-defined agent
};

/// Agent lifecycle state
enum class AgentState : uint8_t {
    Inactive,      // Not running
    Starting,      // Process started, not yet ready
    Idle,          // Waiting at prompt for user input
    Running,       // Processing/thinking
    Thinking,      // Agent is processing (e.g., Claude "Thinking...")
    ToolUse,       // Agent is executing a tool
    Waiting,       // Agent is waiting for user input/approval
    NeedsInput,    // Waiting for permission/approval (legacy alias for Waiting)
    Error,         // Agent encountered an error
    Exited,        // Process has terminated
};

/// Information about a detected agent
struct AgentInfo {
    AgentType type = AgentType::Unknown;
    AgentState state = AgentState::Inactive;
    std::string name;           // Human-readable name
    std::string process_name;   // Process name for detection
    int pid = -1;               // Process ID (-1 if unknown)
    uint32_t pane_id = 0;       // Which pane this agent is in
    std::chrono::steady_clock::time_point last_activity;
    std::string last_message;   // Last status message
    std::string custom_label;   // Custom label set via MCP
    std::string custom_icon;    // Custom icon set via MCP
};

/// Pattern-based state detection rule.
/// Matches terminal output text to determine agent state transitions.
struct AgentStatePattern {
    AgentType agent_type = AgentType::Unknown;  // Which agent this applies to (Unknown = all)
    AgentState target_state = AgentState::Idle;
    std::string pattern;                         // Substring to match in terminal output
    bool is_regex = false;                       // If true, treat pattern as regex
};

/// Callback for agent state changes
using AgentStateCallback = std::function<void(uint32_t pane_id, const AgentInfo& info)>;

/// Agent detection and tracking system
class AgentTracker {
public:
    AgentTracker();
    ~AgentTracker();

    /// Register an agent detection pattern
    void registerAgent(AgentType type, const std::string& name,
                       const std::string& process_name,
                       const std::vector<std::string>& env_markers = {},
                       const std::string& hook_provider_id = "");

    /// Detect agent from process name or environment variables
    AgentType detectAgent(const std::string& process_name,
                          const std::vector<std::string>& env_vars = {}) const;

    /// Report agent state change (called from hooks/OSC handlers)
    void reportState(uint32_t pane_id, AgentType type, AgentState state,
                     const std::string& message = "");

    /// Report agent start (detected a new agent process)
    void reportStart(uint32_t pane_id, AgentType type, int pid);

    /// Report agent exit
    void reportExit(uint32_t pane_id);

    /// Get agent info for a pane (nullptr if no agent)
    const AgentInfo* getAgent(uint32_t pane_id) const;

    /// Get all tracked agents
    std::vector<const AgentInfo*> allAgents() const;

    /// Get agents in a specific state
    std::vector<const AgentInfo*> agentsInState(AgentState state) const;

    /// Check if any agent needs input (for notification purposes)
    bool anyNeedsInput() const;

    /// Sweep stale entries: check PIDs, remove dead agents
    /// Call periodically (e.g., every 30 seconds)
    void sweepStale();

    /// Set callback for agent state changes
    void setStateCallback(AgentStateCallback cb) { callback_ = std::move(cb); }

    void setProviderRegistry(class ProviderRegistry* registry) { provider_registry_ = registry; }
    void setNotificationStore(class NotificationStore* store) { notification_store_ = store; }

    /// Set stale timeout (seconds) for sweepStale(). Default 60.
    void setStaleTimeout(int seconds) { stale_timeout_seconds_ = seconds; }
    int staleTimeout() const { return stale_timeout_seconds_; }

    /// Load state patterns from ProviderRegistry (replaces hardcoded defaults).
    void loadStatePatternsFrom(const class ProviderRegistry& registry);

    /// Get registered agent patterns count
    size_t registeredCount() const { return patterns_.size(); }

    /// Add a pattern-based state detection rule.
    /// When terminal output matches the pattern, the agent transitions to target_state.
    void addStatePattern(const AgentStatePattern& pattern);

    /// Evaluate terminal output against registered state patterns for a pane.
    /// Returns true if a state transition occurred.
    bool evaluateOutput(uint32_t pane_id, const std::string& output);

    /// Get all registered state patterns.
    const std::vector<AgentStatePattern>& statePatterns() const { return state_patterns_; }

    /// Convert AgentState to string representation.
    static std::string stateToString(AgentState state);

    /// Parse string to AgentState. Returns Inactive on unrecognized input.
    static AgentState stringToState(const std::string& str);

private:
    struct AgentPattern {
        AgentType type;
        std::string name;
        std::string process_name;
        std::vector<std::string> env_markers;
        std::string hook_provider_id;  // ID for auto-installing hooks (empty = no hooks)
    };

    std::vector<AgentPattern> patterns_;
    std::unordered_map<uint32_t, AgentInfo> agents_;  // pane_id -> AgentInfo
    AgentStateCallback callback_;
    std::vector<AgentStatePattern> state_patterns_;
    ProviderRegistry* provider_registry_ = nullptr;
    NotificationStore* notification_store_ = nullptr;
    std::set<std::string> notified_providers_;
    int stale_timeout_seconds_ = 60;

    void initDefaultPatterns();
    void initDefaultStatePatterns();
};

} // namespace termcore
#endif
