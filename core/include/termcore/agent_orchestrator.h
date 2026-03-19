#pragma once

#include "termcore/agent.h"
#include "termcore/mux.h"

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace termcore {

/// Assignment: send a command string to a specific pane.
struct AgentAssignment {
    PaneId pane_id = kInvalidPane;
    std::string command;  // text to send to the pane
};

/// Configuration for launching a batch of agents.
struct AgentLaunchConfig {
    std::string cli_command;  // e.g., "claude", "codex"
    int count = 1;            // number of agents to launch
    std::string layout;       // "grid", "horizontal", "vertical"
};

/// Status snapshot of a single agent pane.
struct AgentPaneStatus {
    PaneId pane_id = kInvalidPane;
    std::string last_output;  // last N lines of output
    bool is_idle = false;     // no output for > threshold seconds
    AgentState agent_state = AgentState::Inactive;
};

/// Callback to send text data to a pane's PTY.
using SendTextCallback = std::function<void(PaneId, const std::string&)>;

/// Callback to read the last N lines from a pane's screen buffer.
using ReadScreenCallback = std::function<std::string(PaneId, int)>;

/// Coordinates multi-agent workflows: launching agents in a workspace,
/// distributing commands, and reading back status.
class AgentOrchestrator {
public:
    explicit AgentOrchestrator(Mux& mux);
    ~AgentOrchestrator();

    /// Set the callback used to write text to a pane's PTY.
    void setSendTextCallback(SendTextCallback cb);

    /// Set the callback used to read screen content from a pane.
    void setReadScreenCallback(ReadScreenCallback cb);

    /// Launch N agents in a new workspace with the specified layout.
    /// Returns the list of pane IDs created for the agents.
    std::vector<PaneId> launchAgents(const AgentLaunchConfig& config,
                                      int rows = 24, int cols = 80);

    /// Send commands to specific agent panes.
    void orchestrate(const std::vector<AgentAssignment>& assignments);

    /// Read output status from all tracked agent panes.
    std::vector<AgentPaneStatus> readAllStatus();

    /// Return panes that have been idle (no output) longer than the threshold.
    std::vector<PaneId> getIdlePanes(float threshold_seconds = 5.0f);

    /// Close all agent panes in a workspace and destroy the workspace.
    void closeAll(WorkspaceId ws_id);

    /// Get the workspace ID of the most recently launched agent group.
    WorkspaceId lastWorkspaceId() const { return last_workspace_id_; }

private:
    Mux& mux_;
    SendTextCallback send_text_cb_;
    ReadScreenCallback read_screen_cb_;

    /// Workspace created by the last launchAgents call.
    WorkspaceId last_workspace_id_ = kInvalidWorkspace;

    /// Per-pane tracking: timestamp of last output activity.
    struct PaneTracking {
        PaneId pane_id = kInvalidPane;
        WorkspaceId workspace_id = kInvalidWorkspace;
        std::chrono::steady_clock::time_point last_activity;
    };
    std::unordered_map<PaneId, PaneTracking> tracked_panes_;
};

}  // namespace termcore
