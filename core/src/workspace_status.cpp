#include "termcore/workspace_status.h"

namespace termcore {

// Agent state priority: higher value = more dominant
static int agentStatePriority(AgentState state) {
    switch (state) {
        case AgentState::NeedsInput: return 5;
        case AgentState::Running:    return 4;
        case AgentState::Starting:   return 3;
        case AgentState::Idle:       return 2;
        case AgentState::Inactive:   return 1;
        case AgentState::Exited:     return 0;
    }
    return 0;
}

WorkspaceStatusProvider::WorkspaceStatusProvider(Mux& mux, AgentTracker& agents,
                                                 NotificationStore& notifications)
    : mux_(mux), agents_(agents), notifications_(notifications) {
    // Wire mux changes to trigger refresh
    mux_.setOnChanged([this]() { refresh(); });
}

void WorkspaceStatusProvider::setCwd(WorkspaceId ws_id, const std::string& cwd) {
    cwd_map_[ws_id] = cwd;
}

std::vector<WorkspaceStatusSnapshot> WorkspaceStatusProvider::currentSnapshots() {
    std::vector<WorkspaceStatusSnapshot> snapshots;
    auto ws_ids = mux_.allWorkspaceIds();
    WorkspaceId active_ws = mux_.activeWorkspaceId();

    for (auto ws_id : ws_ids) {
        auto* ws = mux_.getWorkspace(ws_id);
        if (!ws) continue;

        WorkspaceStatusSnapshot snap;
        snap.id = ws_id;
        snap.name = ws->name;
        snap.is_active = (ws_id == active_ws);
        snap.dominant_agent_state = dominantAgentState(ws_id);
        snap.unread_notification_count = unreadNotificationCount(ws_id);

        // CWD and git branch
        auto cwd_it = cwd_map_.find(ws_id);
        if (cwd_it != cwd_map_.end()) {
            snap.cwd = cwd_it->second;
            snap.git_branch = git_detector_.readBranch(cwd_it->second);
        }

        snapshots.push_back(std::move(snap));
    }

    return snapshots;
}

void WorkspaceStatusProvider::setOnChanged(Callback cb) {
    callback_ = std::move(cb);
}

void WorkspaceStatusProvider::refresh() {
    if (callback_) {
        callback_(currentSnapshots());
    }
}

AgentState WorkspaceStatusProvider::dominantAgentState(WorkspaceId ws_id) const {
    AgentState dominant = AgentState::Inactive;
    int best_priority = agentStatePriority(dominant);

    // Iterate over all tabs and panes in this workspace
    auto tab_ids = mux_.allTabIds(ws_id);
    for (auto tab_id : tab_ids) {
        auto panes = mux_.allPanes(ws_id, tab_id);
        for (auto pane_id : panes) {
            const auto* info = agents_.getAgent(pane_id);
            if (info) {
                int p = agentStatePriority(info->state);
                if (p > best_priority) {
                    best_priority = p;
                    dominant = info->state;
                }
            }
        }
    }

    return dominant;
}

size_t WorkspaceStatusProvider::unreadNotificationCount(WorkspaceId ws_id) const {
    size_t count = 0;

    auto tab_ids = mux_.allTabIds(ws_id);
    for (auto tab_id : tab_ids) {
        auto panes = mux_.allPanes(ws_id, tab_id);
        for (auto pane_id : panes) {
            auto pane_notifs = notifications_.forPane(pane_id);
            for (const auto* n : pane_notifs) {
                if (!n->read) {
                    ++count;
                }
            }
        }
    }

    return count;
}

} // namespace termcore
