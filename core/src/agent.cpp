#include "termcore/agent.h"
#include "termcore/provider_registry.h"
#include "termcore/string_utils.h"

#include <algorithm>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#endif

namespace termcore {

namespace {

/// Check if a process with given PID is still alive
bool isProcessAlive(int pid) {
    if (pid <= 0) return false;

#ifdef _WIN32
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                  static_cast<DWORD>(pid));
    if (hProcess == nullptr) return false;

    DWORD exitCode = 0;
    BOOL result = GetExitCodeProcess(hProcess, &exitCode);
    CloseHandle(hProcess);

    return result && exitCode == STILL_ACTIVE;
#else
    int ret = kill(static_cast<pid_t>(pid), 0);
    if (ret == 0) return true;
    return errno != ESRCH;
#endif
}

} // anonymous namespace

AgentTracker::AgentTracker() {
    initDefaultPatterns();
    initDefaultStatePatterns();
}

AgentTracker::~AgentTracker() = default;

void AgentTracker::initDefaultPatterns() {
    registerAgent(AgentType::ClaudeCode, "Claude Code", "claude",
                  {"CLAUDE_CODE_SESSION"}, "claude_code");
    registerAgent(AgentType::Codex, "Codex", "codex",
                  {"CODEX_SESSION"}, "codex");
    registerAgent(AgentType::GeminiCli, "Gemini CLI", "gemini",
                  {"GEMINI_CLI"}, "gemini_cli");
    registerAgent(AgentType::Aider, "Aider", "aider", {});
    registerAgent(AgentType::OpenCode, "OpenCode", "opencode", {});
    registerAgent(AgentType::Goose, "Goose", "goose", {});
    registerAgent(AgentType::Amp, "Amp", "amp", {});
    registerAgent(AgentType::Cline, "Cline", "cline", {});
}

void AgentTracker::registerAgent(AgentType type, const std::string& name,
                                 const std::string& process_name,
                                 const std::vector<std::string>& env_markers,
                                 const std::string& hook_provider_id) {
    patterns_.push_back({type, name, process_name, env_markers, hook_provider_id});
}

AgentType AgentTracker::detectAgent(const std::string& process_name,
                                    const std::vector<std::string>& env_vars) const {
    for (const auto& pattern : patterns_) {
        // Check process name (case-insensitive contains)
        if (containsIgnoreCase(process_name, pattern.process_name)) {
            return pattern.type;
        }

        // Check environment variable markers
        for (const auto& marker : pattern.env_markers) {
            for (const auto& env : env_vars) {
                if (containsIgnoreCase(env, marker)) {
                    return pattern.type;
                }
            }
        }
    }

    return AgentType::Unknown;
}

void AgentTracker::reportState(uint32_t pane_id, AgentType type,
                               AgentState state, const std::string& message) {
    auto& info = agents_[pane_id];
    info.type = type;
    info.state = state;
    info.pane_id = pane_id;
    info.last_activity = std::chrono::steady_clock::now();
    if (!message.empty()) {
        info.last_message = message;
    }

    if (callback_) {
        callback_(pane_id, info);
    }
}

void AgentTracker::reportStart(uint32_t pane_id, AgentType type, int pid) {
    auto& info = agents_[pane_id];
    info.type = type;
    info.state = AgentState::Starting;
    info.pid = pid;
    info.pane_id = pane_id;
    info.last_activity = std::chrono::steady_clock::now();

    // Look up human-readable name from patterns
    for (const auto& pattern : patterns_) {
        if (pattern.type == type) {
            info.name = pattern.name;
            info.process_name = pattern.process_name;
            break;
        }
    }

    if (callback_) {
        callback_(pane_id, info);
    }
    // Hook installation is handled by LuaHooksModule via TabController::pollAgentDetection()
}

void AgentTracker::reportExit(uint32_t pane_id) {
    auto it = agents_.find(pane_id);
    if (it == agents_.end()) return;

    it->second.state = AgentState::Exited;
    it->second.last_activity = std::chrono::steady_clock::now();

    if (callback_) {
        callback_(pane_id, it->second);
    }
}

const AgentInfo* AgentTracker::getAgent(uint32_t pane_id) const {
    auto it = agents_.find(pane_id);
    if (it == agents_.end()) return nullptr;
    return &it->second;
}

std::vector<const AgentInfo*> AgentTracker::allAgents() const {
    std::vector<const AgentInfo*> result;
    result.reserve(agents_.size());
    for (const auto& [id, info] : agents_) {
        result.push_back(&info);
    }
    return result;
}

std::vector<const AgentInfo*> AgentTracker::agentsInState(AgentState state) const {
    std::vector<const AgentInfo*> result;
    for (const auto& [id, info] : agents_) {
        if (info.state == state) {
            result.push_back(&info);
        }
    }
    return result;
}

bool AgentTracker::anyNeedsInput() const {
    for (const auto& [id, info] : agents_) {
        if (info.state == AgentState::NeedsInput) {
            return true;
        }
    }
    return false;
}

void AgentTracker::sweepStale() {
    for (auto it = agents_.begin(); it != agents_.end(); ) {
        auto& info = it->second;

        // Only check agents with a known PID that haven't already exited
        if (info.pid > 0 && info.state != AgentState::Exited) {
            if (!isProcessAlive(info.pid)) {
                info.state = AgentState::Exited;
                info.last_activity = std::chrono::steady_clock::now();
                if (callback_) {
                    callback_(it->first, info);
                }
            }
        }

        // Remove agents that have been Exited for longer than stale timeout
        if (info.state == AgentState::Exited) {
            auto elapsed = std::chrono::steady_clock::now() - info.last_activity;
            if (elapsed > std::chrono::seconds(stale_timeout_seconds_)) {
                it = agents_.erase(it);
                continue;
            }
        }

        ++it;
    }
}

void AgentTracker::initDefaultStatePatterns() {
    // State patterns are now loaded from providers.lua via loadStatePatternsFrom().
    // This method is kept as a no-op for backward compatibility.
}

void AgentTracker::loadStatePatternsFrom(const ProviderRegistry& registry) {
    state_patterns_.clear();

    for (const auto& provider : registry.all()) {
        // Map provider agent_type string to AgentType enum
        AgentType agent_type = AgentType::Unknown;
        for (const auto& pat : patterns_) {
            if (pat.name == provider.display_name || pat.process_name == provider.id) {
                agent_type = pat.type;
                break;
            }
        }
        // Also try matching by agent_type string
        if (agent_type == AgentType::Unknown) {
            if (provider.agent_type == "ClaudeCode") agent_type = AgentType::ClaudeCode;
            else if (provider.agent_type == "Codex") agent_type = AgentType::Codex;
            else if (provider.agent_type == "GeminiCli") agent_type = AgentType::GeminiCli;
            else if (provider.agent_type == "Aider") agent_type = AgentType::Aider;
            else if (provider.agent_type == "OpenCode") agent_type = AgentType::OpenCode;
            else if (provider.agent_type == "Goose") agent_type = AgentType::Goose;
            else if (provider.agent_type == "Amp") agent_type = AgentType::Amp;
            else if (provider.agent_type == "Cline") agent_type = AgentType::Cline;
            else if (provider.agent_type == "Custom") agent_type = AgentType::Custom;
        }

        for (const auto& sp : provider.state_patterns) {
            AgentState state = stringToState(sp.state);
            state_patterns_.push_back({agent_type, state, sp.pattern, false});
        }
    }

    // Apply stale timeout from registry
    stale_timeout_seconds_ = registry.staleTimeout();
}

void AgentTracker::addStatePattern(const AgentStatePattern& pattern) {
    state_patterns_.push_back(pattern);
}

bool AgentTracker::evaluateOutput(uint32_t pane_id, const std::string& output) {
    auto it = agents_.find(pane_id);
    if (it == agents_.end()) return false;

    auto& info = it->second;

    for (const auto& sp : state_patterns_) {
        // Check agent type match: Unknown matches all
        if (sp.agent_type != AgentType::Unknown && sp.agent_type != info.type) {
            continue;
        }

        bool matched = false;
        if (!sp.is_regex) {
            matched = containsIgnoreCase(output, sp.pattern);
        } else {
            // Simple substring match for regex patterns as well (regex support
            // would require <regex> which is heavy; substring is sufficient)
            matched = containsIgnoreCase(output, sp.pattern);
        }

        if (matched && info.state != sp.target_state) {
            info.state = sp.target_state;
            info.last_activity = std::chrono::steady_clock::now();
            if (callback_) {
                callback_(pane_id, info);
            }
            return true;
        }
    }

    return false;
}

std::string AgentTracker::stateToString(AgentState state) {
    switch (state) {
        case AgentState::Inactive:   return "inactive";
        case AgentState::Starting:   return "starting";
        case AgentState::Idle:       return "idle";
        case AgentState::Running:    return "running";
        case AgentState::Thinking:   return "thinking";
        case AgentState::ToolUse:    return "tool_use";
        case AgentState::Waiting:    return "waiting";
        case AgentState::NeedsInput: return "needs_input";
        case AgentState::Error:      return "error";
        case AgentState::Exited:     return "exited";
    }
    return "unknown";
}

AgentState AgentTracker::stringToState(const std::string& str) {
    if (str == "inactive")    return AgentState::Inactive;
    if (str == "starting")    return AgentState::Starting;
    if (str == "idle")        return AgentState::Idle;
    if (str == "running")     return AgentState::Running;
    if (str == "thinking")    return AgentState::Thinking;
    if (str == "tool_use")    return AgentState::ToolUse;
    if (str == "waiting")     return AgentState::Waiting;
    if (str == "needs_input") return AgentState::NeedsInput;
    if (str == "error")       return AgentState::Error;
    if (str == "exited")      return AgentState::Exited;
    return AgentState::Inactive;
}

} // namespace termcore
