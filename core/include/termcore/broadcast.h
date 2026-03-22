#pragma once

#include "termcore/mux.h"
#include <set>
#include <string>
#include <vector>
#include <functional>

namespace termcore {

enum class BroadcastMode { Off, AllPanes, SelectedPanes };

/// Callback that writes data to a specific pane's PTY.
using PaneWriteCallback = std::function<void(PaneId pane_id, const std::string& data)>;

/// Manages broadcast input mode: sends keyboard input to multiple panes simultaneously.
class BroadcastManager {
public:
    BroadcastManager() = default;
    ~BroadcastManager() = default;

    void setMode(BroadcastMode mode);
    BroadcastMode mode() const { return mode_; }

    /// Add a pane to the selected-panes target list.
    void addTarget(PaneId pane_id);

    /// Remove a pane from the selected-panes target list.
    void removeTarget(PaneId pane_id);

    /// Clear all selected targets.
    void clearTargets();

    /// Check if a specific pane is in the selected-panes target list.
    bool hasTarget(PaneId pane_id) const;

    /// Returns pane IDs to broadcast to, based on current mode.
    /// In AllPanes mode, returns all visible panes from the active tab.
    /// In SelectedPanes mode, returns only the manually selected panes.
    /// In Off mode, returns empty vector.
    std::vector<PaneId> getTargets(const Mux& mux, WorkspaceId ws_id, TabId tab_id) const;

    /// Write data to all target panes via the write callback.
    void broadcastInput(const std::string& data, Mux& mux,
                        WorkspaceId ws_id, TabId tab_id,
                        PaneWriteCallback write_cb);

    /// Returns true if broadcast is active (mode is not Off).
    bool isActive() const { return mode_ != BroadcastMode::Off; }

    /// Set the write callback for pane data.
    void setPaneWriteCallback(PaneWriteCallback cb) { write_cb_ = std::move(cb); }

private:
    BroadcastMode mode_ = BroadcastMode::Off;
    std::set<PaneId> selected_targets_;
    PaneWriteCallback write_cb_;
};

}  // namespace termcore
