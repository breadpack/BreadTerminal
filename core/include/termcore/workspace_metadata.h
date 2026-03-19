#ifndef TERMCORE_WORKSPACE_METADATA_H
#define TERMCORE_WORKSPACE_METADATA_H

#include <cstdint>
#include <string>
#include <vector>

#include "termcore/agent.h"
#include "termcore/mux.h"
#include "termcore/port_detector.h"
#include "termcore/pr_detector.h"

namespace termcore {

/// Aggregated metadata for a single pane, suitable for sidebar display.
struct PaneMetadata {
    PaneId pane_id = kInvalidPane;
    std::string working_dir;
    std::string git_branch;
    PRInfo pr_info;
    std::vector<ListeningPort> ports;
    AgentState agent_status = AgentState::Inactive;
    std::string latest_notification;
};

/// Aggregated metadata for a workspace, containing all its panes.
struct WorkspaceMetadata {
    WorkspaceId workspace_id = kInvalidWorkspace;
    std::string name;
    std::vector<PaneMetadata> panes;
};

} // namespace termcore

#endif // TERMCORE_WORKSPACE_METADATA_H
