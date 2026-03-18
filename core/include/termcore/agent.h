#ifndef TERMCORE_AGENT_H
#define TERMCORE_AGENT_H

#include <chrono>
#include <cstdint>
#include <functional>
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
    NeedsInput,    // Waiting for permission/approval
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
                       const std::vector<std::string>& env_markers = {});

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

    /// Get registered agent patterns count
    size_t registeredCount() const { return patterns_.size(); }

private:
    struct AgentPattern {
        AgentType type;
        std::string name;
        std::string process_name;
        std::vector<std::string> env_markers;
    };

    std::vector<AgentPattern> patterns_;
    std::unordered_map<uint32_t, AgentInfo> agents_;  // pane_id -> AgentInfo
    AgentStateCallback callback_;

    void initDefaultPatterns();
};

} // namespace termcore
#endif
