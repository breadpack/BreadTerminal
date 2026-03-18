#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace termcore {

using PaneId = uint32_t;
using TabId = uint32_t;
using WorkspaceId = uint32_t;

static constexpr PaneId kInvalidPane = 0;
static constexpr TabId kInvalidTab = 0;
static constexpr WorkspaceId kInvalidWorkspace = 0;

enum class SplitDirection { Horizontal, Vertical };

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
};

struct Workspace {
    WorkspaceId id = kInvalidWorkspace;
    std::string name;
    std::vector<std::unique_ptr<Tab>> tabs;
    size_t active_tab_index = 0;
};

using PaneCreateCallback = std::function<PaneId(int rows, int cols)>;
using PaneDestroyCallback = std::function<void(PaneId)>;

class Mux {
public:
    Mux();
    ~Mux();

    void setPaneCallbacks(PaneCreateCallback create_cb, PaneDestroyCallback destroy_cb);

    WorkspaceId createWorkspace(const std::string& name = "");
    void destroyWorkspace(WorkspaceId id);
    Workspace* getWorkspace(WorkspaceId id);
    WorkspaceId activeWorkspaceId() const;
    void setActiveWorkspace(WorkspaceId id);

    TabId createTab(WorkspaceId ws_id, int rows = 24, int cols = 80);
    void destroyTab(WorkspaceId ws_id, TabId tab_id);
    Tab* activeTab(WorkspaceId ws_id);
    void setActiveTab(WorkspaceId ws_id, TabId tab_id);

    PaneId splitPane(WorkspaceId ws_id, TabId tab_id, PaneId pane_id,
                     SplitDirection direction, int rows = 24, int cols = 80);
    void closePane(WorkspaceId ws_id, TabId tab_id, PaneId pane_id);
    PaneId activePaneId(WorkspaceId ws_id, TabId tab_id);
    void setActivePane(WorkspaceId ws_id, TabId tab_id, PaneId pane_id);
    std::vector<PaneId> allPanes(WorkspaceId ws_id, TabId tab_id) const;

    size_t workspaceCount() const { return workspaces_.size(); }

private:
    SplitNode* findNode(SplitNode* node, PaneId pane_id);
    SplitNode* findParent(SplitNode* node, PaneId pane_id);
    void collectPanes(const SplitNode* node, std::vector<PaneId>& out) const;
    void destroyAllPanes(const SplitNode* node);
    Tab* findTab(WorkspaceId ws_id, TabId tab_id);
    const Tab* findTab(WorkspaceId ws_id, TabId tab_id) const;

    std::vector<std::unique_ptr<Workspace>> workspaces_;
    WorkspaceId active_workspace_ = kInvalidWorkspace;
    WorkspaceId next_workspace_id_ = 1;
    TabId next_tab_id_ = 1;

    PaneCreateCallback create_cb_;
    PaneDestroyCallback destroy_cb_;
};

}  // namespace termcore
