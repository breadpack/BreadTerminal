#include "termcore/agent_orchestrator.h"

#include <algorithm>

namespace termcore {

AgentOrchestrator::AgentOrchestrator(Mux& mux)
    : mux_(mux) {}

AgentOrchestrator::~AgentOrchestrator() = default;

void AgentOrchestrator::setSendTextCallback(SendTextCallback cb) {
    send_text_cb_ = std::move(cb);
}

void AgentOrchestrator::setReadScreenCallback(ReadScreenCallback cb) {
    read_screen_cb_ = std::move(cb);
}

std::vector<PaneId> AgentOrchestrator::launchAgents(
    const AgentLaunchConfig& config, int rows, int cols) {

    std::vector<PaneId> pane_ids;
    if (config.count <= 0) return pane_ids;

    // Create a dedicated workspace for the agent batch
    auto ws_id = mux_.createWorkspace("agents");
    if (ws_id == kInvalidWorkspace) return pane_ids;
    last_workspace_id_ = ws_id;

    // Create the first tab (which creates the first pane)
    auto tab_id = mux_.createTab(ws_id, rows, cols);
    if (tab_id == kInvalidTab) {
        mux_.destroyWorkspace(ws_id);
        return pane_ids;
    }

    // Collect the initial pane
    auto initial_panes = mux_.allPanes(ws_id, tab_id);
    if (initial_panes.empty()) {
        mux_.destroyWorkspace(ws_id);
        return pane_ids;
    }
    pane_ids.push_back(initial_panes[0]);

    // Determine split direction from layout
    SplitDirection split_dir = SplitDirection::Horizontal;
    if (config.layout == "vertical") {
        split_dir = SplitDirection::Vertical;
    }
    // For "grid", we alternate directions handled by applyLayout later

    // Create additional panes by splitting
    for (int i = 1; i < config.count; ++i) {
        // Split from the last created pane
        auto new_pane = mux_.splitPane(ws_id, tab_id, pane_ids.back(),
                                        split_dir, rows, cols);
        if (new_pane == kInvalidPane) break;
        pane_ids.push_back(new_pane);
    }

    // Apply the requested layout preset
    if (config.layout == "grid") {
        mux_.applyLayout(ws_id, tab_id, LayoutPreset::Tiled);
    } else if (config.layout == "horizontal") {
        mux_.applyLayout(ws_id, tab_id, LayoutPreset::EvenHorizontal);
    } else if (config.layout == "vertical") {
        mux_.applyLayout(ws_id, tab_id, LayoutPreset::EvenVertical);
    }

    // Track all panes and send the CLI command to each
    auto now = std::chrono::steady_clock::now();
    for (auto pid : pane_ids) {
        PaneTracking tracking;
        tracking.pane_id = pid;
        tracking.workspace_id = ws_id;
        tracking.last_activity = now;
        tracked_panes_[pid] = tracking;

        // Send the agent CLI command to each pane
        if (send_text_cb_ && !config.cli_command.empty()) {
            send_text_cb_(pid, config.cli_command + "\r");
        }
    }

    return pane_ids;
}

void AgentOrchestrator::orchestrate(
    const std::vector<AgentAssignment>& assignments) {

    if (!send_text_cb_) return;

    for (const auto& assignment : assignments) {
        if (assignment.pane_id == kInvalidPane) continue;
        if (assignment.command.empty()) continue;

        send_text_cb_(assignment.pane_id, assignment.command + "\r");

        // Update activity timestamp
        auto it = tracked_panes_.find(assignment.pane_id);
        if (it != tracked_panes_.end()) {
            it->second.last_activity = std::chrono::steady_clock::now();
        }
    }
}

std::vector<AgentPaneStatus> AgentOrchestrator::readAllStatus() {
    std::vector<AgentPaneStatus> statuses;
    statuses.reserve(tracked_panes_.size());

    auto now = std::chrono::steady_clock::now();
    constexpr int kDefaultScreenLines = 10;

    for (auto& [pid, tracking] : tracked_panes_) {
        AgentPaneStatus status;
        status.pane_id = pid;

        // Read screen content if callback is available
        if (read_screen_cb_) {
            status.last_output = read_screen_cb_(pid, kDefaultScreenLines);
            // If there's new output, update the activity timestamp
            if (!status.last_output.empty()) {
                tracking.last_activity = now;
            }
        }

        // Determine idle state
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - tracking.last_activity);
        status.is_idle = elapsed.count() > 5000;  // 5 seconds default

        status.agent_state = AgentState::Inactive;

        statuses.push_back(std::move(status));
    }

    return statuses;
}

std::vector<PaneId> AgentOrchestrator::getIdlePanes(float threshold_seconds) {
    std::vector<PaneId> idle;
    auto now = std::chrono::steady_clock::now();
    auto threshold_ms = static_cast<int64_t>(threshold_seconds * 1000.0f);

    for (const auto& [pid, tracking] : tracked_panes_) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - tracking.last_activity);
        if (elapsed.count() > threshold_ms) {
            idle.push_back(pid);
        }
    }

    return idle;
}

void AgentOrchestrator::closeAll(WorkspaceId ws_id) {
    if (ws_id == kInvalidWorkspace) return;

    // Remove tracked panes belonging to this workspace
    for (auto it = tracked_panes_.begin(); it != tracked_panes_.end(); ) {
        if (it->second.workspace_id == ws_id) {
            it = tracked_panes_.erase(it);
        } else {
            ++it;
        }
    }

    // Destroy the workspace (which closes all tabs and panes)
    mux_.destroyWorkspace(ws_id);

    if (last_workspace_id_ == ws_id) {
        last_workspace_id_ = kInvalidWorkspace;
    }
}

}  // namespace termcore
