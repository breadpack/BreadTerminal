#include "termcore/agent.h"

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

/// Case-insensitive substring search
bool containsIgnoreCase(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return false;
    if (needle.size() > haystack.size()) return false;

    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    return it != haystack.end();
}

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
}

AgentTracker::~AgentTracker() = default;

void AgentTracker::initDefaultPatterns() {
    registerAgent(AgentType::ClaudeCode, "Claude Code", "claude",
                  {"CLAUDE_CODE_SESSION"});
    registerAgent(AgentType::Codex, "Codex", "codex",
                  {"CODEX_SESSION"});
    registerAgent(AgentType::GeminiCli, "Gemini CLI", "gemini",
                  {"GEMINI_CLI"});
    registerAgent(AgentType::Aider, "Aider", "aider", {});
    registerAgent(AgentType::OpenCode, "OpenCode", "opencode", {});
    registerAgent(AgentType::Goose, "Goose", "goose", {});
    registerAgent(AgentType::Amp, "Amp", "amp", {});
    registerAgent(AgentType::Cline, "Cline", "cline", {});
}

void AgentTracker::registerAgent(AgentType type, const std::string& name,
                                 const std::string& process_name,
                                 const std::vector<std::string>& env_markers) {
    patterns_.push_back({type, name, process_name, env_markers});
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

        // Remove agents that have been Exited for more than 60 seconds
        if (info.state == AgentState::Exited) {
            auto elapsed = std::chrono::steady_clock::now() - info.last_activity;
            if (elapsed > std::chrono::seconds(60)) {
                it = agents_.erase(it);
                continue;
            }
        }

        ++it;
    }
}

} // namespace termcore
