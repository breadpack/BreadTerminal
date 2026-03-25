#include "termcore/sidebar_model.h"

namespace termcore {

SidebarModel::SidebarModel(const AgentTreeTracker& tree_tracker)
    : tree_tracker_(tree_tracker) {}

void SidebarModel::update(const AgentTracker& agents,
                           const NotificationStore& notifications,
                           const TabController& tabs,
                           const CommandDispatcher& dispatcher) {
    entries_.clear();
    auto tab_infos = tabs.tabBarInfo();

    // Get the active workspace and iterate tabs to find pane IDs.
    // TabInfo doesn't expose pane_id directly, so we use Mux through tabs.
    const Mux* mux = const_cast<TabController&>(tabs).mux();
    WorkspaceId ws = const_cast<TabController&>(tabs).workspaceId();
    auto tab_ids = mux->allTabIds(ws);

    for (size_t i = 0; i < tab_infos.size() && i < tab_ids.size(); ++i) {
        SidebarEntry entry;

        // Get the active pane for this tab
        PaneId pane = mux->activePaneId(ws, tab_ids[i]);
        entry.pane_id = pane;
        entry.title = tab_infos[i].title;
        entry.icon = tab_infos[i].icon_name;
        entry.has_unread = tab_infos[i].has_unread;

        // Agent state from AgentTracker
        if (pane != kInvalidPane) {
            const AgentInfo* agent = agents.getAgent(pane);
            if (agent) {
                entry.agent_state = agent->state;
            }

            // Unread notifications
            if (notifications.hasUnread(pane)) {
                entry.has_unread = true;
            }

            // Status pills and progress
            entry.pills = dispatcher.getPaneStatuses(pane);
            const PaneProgress* prog = dispatcher.getPaneProgress(pane);
            if (prog) {
                entry.progress = *prog;
            }

            // Subagent tree
            entry.subagents = tree_tracker_.rootAgents(pane);
        }

        // Expansion state
        entry.expanded = isExpanded(pane);

        entries_.push_back(std::move(entry));
    }
}

void SidebarModel::setExpanded(PaneId pane, bool expanded) {
    expanded_state_[pane] = expanded;
}

bool SidebarModel::isExpanded(PaneId pane) const {
    auto it = expanded_state_.find(pane);
    if (it == expanded_state_.end()) return true; // default expanded
    return it->second;
}

} // namespace termcore
