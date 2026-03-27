#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace termcore {

class SshMuxSession;

using PaneId = uint32_t;
using TabId = uint32_t;
using WorkspaceId = uint32_t;

static constexpr PaneId kInvalidPane = 0;
static constexpr TabId kInvalidTab = 0;
static constexpr WorkspaceId kInvalidWorkspace = 0;

enum class SplitDirection { Horizontal, Vertical };

enum class BroadcastMode { Off, All, Selected };

enum class LayoutPreset {
    EvenHorizontal,   // All panes side by side horizontally
    EvenVertical,     // All panes stacked vertically
    Tiled,            // Grid arrangement
    MainLeft,         // One large pane on left, others stacked on right
    MainTop,          // One large pane on top, others side by side on bottom
};

struct SplitNode {
    bool is_leaf = true;
    PaneId pane_id = kInvalidPane;
    SplitDirection direction = SplitDirection::Horizontal;
    float ratio = 0.5f;
    std::unique_ptr<SplitNode> first;
    std::unique_ptr<SplitNode> second;
};

struct Tab {
    TabId id = kInvalidTab;
    std::string title;
    std::unique_ptr<SplitNode> root;
    PaneId active_pane = kInvalidPane;
    PaneId zoomed_pane = kInvalidPane;  // 0 = no zoom, >0 = this pane is zoomed
    int current_layout = -1;
};

struct Workspace {
    WorkspaceId id = kInvalidWorkspace;
    std::string name;
    std::vector<std::unique_ptr<Tab>> tabs;
    size_t active_tab_index = 0;
};

using PaneCreateCallback = std::function<PaneId(int rows, int cols)>;
using PaneDestroyCallback = std::function<void(PaneId)>;
using MuxChangeCallback = std::function<void()>;

class Mux {
public:
    Mux();
    ~Mux();

    void setPaneCallbacks(PaneCreateCallback create_cb, PaneDestroyCallback destroy_cb);

    /// Set a callback that fires when the mux structure changes.
    void setOnChanged(MuxChangeCallback cb);

    WorkspaceId createWorkspace(const std::string& name = "");
    void destroyWorkspace(WorkspaceId id);
    Workspace* getWorkspace(WorkspaceId id);
    WorkspaceId activeWorkspaceId() const;
    void setActiveWorkspace(WorkspaceId id);

    TabId createTab(WorkspaceId ws_id, int rows = 24, int cols = 80);
    void destroyTab(WorkspaceId ws_id, TabId tab_id);
    Tab* activeTab(WorkspaceId ws_id);
    void setActiveTab(WorkspaceId ws_id, TabId tab_id);

    /// Move a tab to a new position within the workspace.
    /// newIndex is clamped to [0, tabs.size()-1].
    void moveTab(WorkspaceId ws_id, TabId tab_id, int newIndex);

    PaneId splitPane(WorkspaceId ws_id, TabId tab_id, PaneId pane_id,
                     SplitDirection direction, int rows = 24, int cols = 80);
    void closePane(WorkspaceId ws_id, TabId tab_id, PaneId pane_id);
    PaneId activePaneId(WorkspaceId ws_id, TabId tab_id) const;
    void setActivePane(WorkspaceId ws_id, TabId tab_id, PaneId pane_id);
    std::vector<PaneId> allPanes(WorkspaceId ws_id, TabId tab_id) const;

    size_t workspaceCount() const { return workspaces_.size(); }

    /// Get all workspace IDs.
    std::vector<WorkspaceId> allWorkspaceIds() const;

    /// Get all tab IDs in a workspace.
    std::vector<TabId> allTabIds(WorkspaceId ws_id) const;

    /// Access split tree root (const) for a tab.
    const SplitNode* splitRoot(WorkspaceId ws_id, TabId tab_id) const;

    /// Toggle zoom on/off for the active pane in a tab.
    /// If zoomed, sets Tab::zoomed_pane to the active pane ID.
    /// If already zoomed, unzooms (sets to kInvalidPane).
    void toggleZoom(WorkspaceId ws_id, TabId tab_id);

    /// Check if a tab has a zoomed pane.
    bool isZoomed(WorkspaceId ws_id, TabId tab_id) const;

    /// Get the zoomed pane ID (kInvalidPane if not zoomed).
    PaneId zoomedPane(WorkspaceId ws_id, TabId tab_id) const;

    /// Apply a layout preset to a tab, rearranging existing panes.
    void applyLayout(WorkspaceId ws_id, TabId tab_id, LayoutPreset preset);

    /// Cycle through layout presets.
    void nextLayout(WorkspaceId ws_id, TabId tab_id);

    /// Set all split ratios to 0.5 (equal spacing).
    void equalizeSplits(WorkspaceId ws_id, TabId tab_id);

    // --- Broadcast input ---

    /// Get the current broadcast mode.
    BroadcastMode broadcastMode() const;

    /// Set the broadcast mode.
    void setBroadcastMode(BroadcastMode mode);

    /// Cycle broadcast mode: Off → All → Selected → Off.
    void toggleBroadcast();

    /// Add a pane to the selected broadcast targets.
    void addBroadcastTarget(PaneId paneId);

    /// Remove a pane from the selected broadcast targets.
    void removeBroadcastTarget(PaneId paneId);

    /// Clear all selected broadcast targets.
    void clearBroadcastTargets();

    /// Get pane IDs that should receive broadcast input.
    /// All mode: all panes in active workspace's active tab.
    /// Selected mode: only the selected broadcast targets.
    /// Off mode: empty.
    std::vector<PaneId> getBroadcastPaneIds() const;

    // --- SSH mux integration ---

    /// Register an existing pane as backed by an SSH mux channel.
    void addSshPane(PaneId pane_id, int channel_id,
                    std::shared_ptr<SshMuxSession> session);

    /// Remove the SSH-backing association for a pane.
    void removeSshPane(PaneId pane_id);

    /// Query whether a pane is backed by an SSH channel.
    bool isSshPane(PaneId pane_id) const;

    /// Get the SshMuxSession for a pane, or nullptr.
    std::shared_ptr<SshMuxSession> sshSessionForPane(PaneId pane_id) const;

    /// Get the SSH channel id for a pane, or -1.
    int sshChannelForPane(PaneId pane_id) const;

private:
    SplitNode* findNode(SplitNode* node, PaneId pane_id);
    SplitNode* findParent(SplitNode* node, PaneId pane_id);
    void collectPanes(const SplitNode* node, std::vector<PaneId>& out) const;
    void destroyAllPanes(const SplitNode* node);
    void equalizeSplitsRecursive(SplitNode* node);
    std::unique_ptr<SplitNode> buildLayoutTree(const std::vector<PaneId>& panes,
                                                LayoutPreset preset);
    std::unique_ptr<SplitNode> buildEvenChain(const std::vector<PaneId>& panes,
                                               SplitDirection direction);
    std::unique_ptr<SplitNode> buildTiled(const std::vector<PaneId>& panes);
    Tab* findTab(WorkspaceId ws_id, TabId tab_id);
    const Tab* findTab(WorkspaceId ws_id, TabId tab_id) const;

    std::vector<std::unique_ptr<Workspace>> workspaces_;
    WorkspaceId active_workspace_ = kInvalidWorkspace;
    WorkspaceId next_workspace_id_ = 1;
    TabId next_tab_id_ = 1;

    void fireOnChanged();

    PaneCreateCallback create_cb_;
    PaneDestroyCallback destroy_cb_;
    MuxChangeCallback on_changed_;

    struct SshPaneBinding {
        int channel_id = -1;
        std::shared_ptr<SshMuxSession> session;
    };
    std::unordered_map<PaneId, SshPaneBinding> ssh_panes_;

    BroadcastMode broadcast_mode_ = BroadcastMode::Off;
    std::set<PaneId> broadcast_targets_;
};

}  // namespace termcore
