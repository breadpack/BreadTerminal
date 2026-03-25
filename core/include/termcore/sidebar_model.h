#pragma once

#include "termcore/agent.h"
#include "termcore/agent_tree_tracker.h"
#include "termcore/mux.h"
#include "termcore/notification.h"
#include "termcore/socket/command_dispatcher.h"
#include "termcore/tab_controller.h"

#include <string>
#include <vector>

namespace termcore {

struct SidebarEntry {
    PaneId pane_id = kInvalidPane;
    std::string title;
    std::string icon;
    AgentState agent_state = AgentState::Inactive;
    std::string git_branch;
    std::string cwd;
    std::vector<PaneStatus> pills;
    PaneProgress progress;
    bool has_unread = false;
    float attention_intensity = 0.0f;
    std::vector<SubagentNode> subagents;
    bool expanded = true;
};

class SidebarModel {
public:
    explicit SidebarModel(const AgentTreeTracker& tree_tracker);

    void update(const AgentTracker& agents,
                const NotificationStore& notifications,
                const TabController& tabs,
                const CommandDispatcher& dispatcher);

    const std::vector<SidebarEntry>& entries() const { return entries_; }
    void setExpanded(PaneId pane, bool expanded);
    bool isExpanded(PaneId pane) const;

private:
    const AgentTreeTracker& tree_tracker_;
    std::vector<SidebarEntry> entries_;
    std::unordered_map<PaneId, bool> expanded_state_;
};

} // namespace termcore
