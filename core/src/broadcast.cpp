#include "termcore/broadcast.h"

#include <algorithm>

namespace termcore {

void BroadcastManager::setMode(BroadcastMode mode) {
    mode_ = mode;
}

void BroadcastManager::addTarget(PaneId pane_id) {
    selected_targets_.insert(pane_id);
}

void BroadcastManager::removeTarget(PaneId pane_id) {
    selected_targets_.erase(pane_id);
}

void BroadcastManager::clearTargets() {
    selected_targets_.clear();
}

bool BroadcastManager::hasTarget(PaneId pane_id) const {
    return selected_targets_.count(pane_id) > 0;
}

std::vector<PaneId> BroadcastManager::getTargets(const Mux& mux,
                                                   WorkspaceId ws_id,
                                                   TabId tab_id) const {
    switch (mode_) {
        case BroadcastMode::Off:
            return {};

        case BroadcastMode::AllPanes:
            return mux.allPanes(ws_id, tab_id);

        case BroadcastMode::SelectedPanes: {
            // Return only selected targets that still exist in the tab
            auto all = mux.allPanes(ws_id, tab_id);
            std::vector<PaneId> result;
            for (PaneId id : all) {
                if (selected_targets_.count(id) > 0) {
                    result.push_back(id);
                }
            }
            return result;
        }
    }
    return {};
}

void BroadcastManager::broadcastInput(const std::string& data, Mux& mux,
                                       WorkspaceId ws_id, TabId tab_id,
                                       PaneWriteCallback write_cb) {
    if (mode_ == BroadcastMode::Off) return;

    auto targets = getTargets(mux, ws_id, tab_id);
    for (PaneId id : targets) {
        write_cb(id, data);
    }
}

}  // namespace termcore
