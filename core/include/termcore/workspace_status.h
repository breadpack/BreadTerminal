#ifndef TERMCORE_WORKSPACE_STATUS_H
#define TERMCORE_WORKSPACE_STATUS_H

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "termcore/agent.h"
#include "termcore/git_branch_detector.h"
#include "termcore/mux.h"
#include "termcore/notification.h"

namespace termcore {

/// Snapshot of a single workspace's status for the sidebar.
struct WorkspaceStatusSnapshot {
    WorkspaceId id = kInvalidWorkspace;
    std::string name;
    std::string git_branch;
    AgentState dominant_agent_state = AgentState::Inactive;
    size_t unread_notification_count = 0;
    std::string cwd;
    bool is_active = false;
};

/// Aggregates status from Mux, AgentTracker, NotificationStore, and git
/// to produce sidebar-ready snapshots.
class WorkspaceStatusProvider {
public:
    WorkspaceStatusProvider(Mux& mux, AgentTracker& agents,
                            NotificationStore& notifications);
    ~WorkspaceStatusProvider() = default;

    /// Set the current working directory for a workspace.
    void setCwd(WorkspaceId ws_id, const std::string& cwd);

    /// Get all workspace status snapshots.
    std::vector<WorkspaceStatusSnapshot> currentSnapshots();

    /// Callback type for status changes.
    using Callback = std::function<void(const std::vector<WorkspaceStatusSnapshot>&)>;

    /// Set callback for when status changes.
    void setOnChanged(Callback cb);

    /// Manually trigger a refresh and callback.
    void refresh();

private:
    /// Determine the dominant agent state across all panes in a workspace.
    AgentState dominantAgentState(WorkspaceId ws_id) const;

    /// Count unread notifications for all panes in a workspace.
    size_t unreadNotificationCount(WorkspaceId ws_id) const;

    Mux& mux_;
    AgentTracker& agents_;
    NotificationStore& notifications_;
    GitBranchDetector git_detector_;

    std::unordered_map<WorkspaceId, std::string> cwd_map_;
    Callback callback_;
};

} // namespace termcore

#endif // TERMCORE_WORKSPACE_STATUS_H
