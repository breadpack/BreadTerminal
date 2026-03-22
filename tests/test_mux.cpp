#include <gtest/gtest.h>
#include "termcore/mux.h"

using namespace termcore;

class MuxTest : public ::testing::Test {
protected:
    void SetUp() override {
        next_pane_ = 1;
        destroyed_.clear();
        mux_.setPaneCallbacks(
            [this](int, int) -> PaneId { return next_pane_++; },
            [this](PaneId id) { destroyed_.push_back(id); }
        );
    }

    Mux mux_;
    uint32_t next_pane_ = 1;
    std::vector<PaneId> destroyed_;
};

TEST_F(MuxTest, CreateWorkspace) {
    auto ws_id = mux_.createWorkspace("test");
    EXPECT_NE(ws_id, kInvalidWorkspace);
    auto* ws = mux_.getWorkspace(ws_id);
    ASSERT_NE(ws, nullptr);
    EXPECT_EQ(ws->name, "test");
    EXPECT_EQ(mux_.workspaceCount(), 1u);
}

TEST_F(MuxTest, CreateTab) {
    auto ws_id = mux_.createWorkspace();
    auto tab_id = mux_.createTab(ws_id);
    EXPECT_NE(tab_id, kInvalidTab);

    auto panes = mux_.allPanes(ws_id, tab_id);
    EXPECT_EQ(panes.size(), 1u);
    EXPECT_EQ(panes[0], 1u);
}

TEST_F(MuxTest, SplitPane) {
    auto ws_id = mux_.createWorkspace();
    auto tab_id = mux_.createTab(ws_id);
    auto panes = mux_.allPanes(ws_id, tab_id);
    ASSERT_EQ(panes.size(), 1u);

    PaneId original = panes[0];
    PaneId new_pane = mux_.splitPane(ws_id, tab_id, original, SplitDirection::Horizontal);
    EXPECT_NE(new_pane, kInvalidPane);

    panes = mux_.allPanes(ws_id, tab_id);
    EXPECT_EQ(panes.size(), 2u);
}

TEST_F(MuxTest, ClosePane) {
    auto ws_id = mux_.createWorkspace();
    auto tab_id = mux_.createTab(ws_id);
    auto panes = mux_.allPanes(ws_id, tab_id);
    PaneId p1 = panes[0];
    PaneId p2 = mux_.splitPane(ws_id, tab_id, p1, SplitDirection::Vertical);

    mux_.closePane(ws_id, tab_id, p2);

    panes = mux_.allPanes(ws_id, tab_id);
    EXPECT_EQ(panes.size(), 1u);
    EXPECT_EQ(panes[0], p1);
    EXPECT_EQ(destroyed_.size(), 1u);
    EXPECT_EQ(destroyed_[0], p2);
}

TEST_F(MuxTest, MultipleTabs) {
    auto ws_id = mux_.createWorkspace();
    auto t1 = mux_.createTab(ws_id);
    auto t2 = mux_.createTab(ws_id);
    EXPECT_NE(t1, t2);

    auto* ws = mux_.getWorkspace(ws_id);
    ASSERT_NE(ws, nullptr);
    EXPECT_EQ(ws->tabs.size(), 2u);

    // Active tab should be the last created
    auto* at = mux_.activeTab(ws_id);
    ASSERT_NE(at, nullptr);
    EXPECT_EQ(at->id, t2);
}

TEST_F(MuxTest, ActiveWorkspaceTracking) {
    auto ws1 = mux_.createWorkspace("ws1");
    auto ws2 = mux_.createWorkspace("ws2");

    // First workspace is active by default
    EXPECT_EQ(mux_.activeWorkspaceId(), ws1);

    mux_.setActiveWorkspace(ws2);
    EXPECT_EQ(mux_.activeWorkspaceId(), ws2);
}

TEST_F(MuxTest, ActiveTabTracking) {
    auto ws_id = mux_.createWorkspace();
    auto t1 = mux_.createTab(ws_id);
    auto t2 = mux_.createTab(ws_id);

    // Last created tab is active
    EXPECT_EQ(mux_.activeTab(ws_id)->id, t2);

    mux_.setActiveTab(ws_id, t1);
    EXPECT_EQ(mux_.activeTab(ws_id)->id, t1);
}

TEST_F(MuxTest, ActivePaneTracking) {
    auto ws_id = mux_.createWorkspace();
    auto tab_id = mux_.createTab(ws_id);
    auto panes = mux_.allPanes(ws_id, tab_id);
    PaneId p1 = panes[0];

    EXPECT_EQ(mux_.activePaneId(ws_id, tab_id), p1);

    PaneId p2 = mux_.splitPane(ws_id, tab_id, p1, SplitDirection::Horizontal);
    // Active pane should still be p1 (split doesn't change active)
    EXPECT_EQ(mux_.activePaneId(ws_id, tab_id), p1);

    mux_.setActivePane(ws_id, tab_id, p2);
    EXPECT_EQ(mux_.activePaneId(ws_id, tab_id), p2);
}

TEST_F(MuxTest, NestedSplits) {
    auto ws_id = mux_.createWorkspace();
    auto tab_id = mux_.createTab(ws_id);
    auto panes = mux_.allPanes(ws_id, tab_id);
    PaneId p1 = panes[0];

    PaneId p2 = mux_.splitPane(ws_id, tab_id, p1, SplitDirection::Horizontal);
    PaneId p3 = mux_.splitPane(ws_id, tab_id, p2, SplitDirection::Vertical);

    panes = mux_.allPanes(ws_id, tab_id);
    EXPECT_EQ(panes.size(), 3u);

    // Close middle pane
    mux_.closePane(ws_id, tab_id, p2);
    panes = mux_.allPanes(ws_id, tab_id);
    EXPECT_EQ(panes.size(), 2u);

    // p1 and p3 should remain
    EXPECT_NE(std::find(panes.begin(), panes.end(), p1), panes.end());
    EXPECT_NE(std::find(panes.begin(), panes.end(), p3), panes.end());
}

TEST_F(MuxTest, AllPanesCorrectness) {
    auto ws_id = mux_.createWorkspace();
    auto tab_id = mux_.createTab(ws_id);
    auto panes = mux_.allPanes(ws_id, tab_id);
    PaneId p1 = panes[0];

    PaneId p2 = mux_.splitPane(ws_id, tab_id, p1, SplitDirection::Horizontal);
    PaneId p3 = mux_.splitPane(ws_id, tab_id, p1, SplitDirection::Vertical);
    PaneId p4 = mux_.splitPane(ws_id, tab_id, p2, SplitDirection::Horizontal);

    panes = mux_.allPanes(ws_id, tab_id);
    EXPECT_EQ(panes.size(), 4u);

    // All pane IDs should be present
    for (PaneId pid : {p1, p2, p3, p4}) {
        EXPECT_NE(std::find(panes.begin(), panes.end(), pid), panes.end())
            << "Missing pane " << pid;
    }
}

TEST_F(MuxTest, DestroyWorkspace) {
    auto ws_id = mux_.createWorkspace();
    auto tab_id = mux_.createTab(ws_id);
    auto panes = mux_.allPanes(ws_id, tab_id);
    PaneId p1 = panes[0];
    mux_.splitPane(ws_id, tab_id, p1, SplitDirection::Horizontal);

    mux_.destroyWorkspace(ws_id);

    EXPECT_EQ(mux_.workspaceCount(), 0u);
    EXPECT_EQ(mux_.getWorkspace(ws_id), nullptr);
    // Both panes should have been destroyed
    EXPECT_EQ(destroyed_.size(), 2u);
}

TEST_F(MuxTest, SplitDirectionStored) {
    auto ws_id = mux_.createWorkspace();
    auto tab_id = mux_.createTab(ws_id);
    auto panes = mux_.allPanes(ws_id, tab_id);
    PaneId p1 = panes[0];

    mux_.splitPane(ws_id, tab_id, p1, SplitDirection::Vertical);

    auto* tab = mux_.activeTab(ws_id);
    ASSERT_NE(tab, nullptr);
    ASSERT_NE(tab->root, nullptr);
    EXPECT_FALSE(tab->root->is_leaf);
    EXPECT_EQ(tab->root->direction, SplitDirection::Vertical);
}

TEST_F(MuxTest, DestroyTab) {
    auto ws_id = mux_.createWorkspace();
    auto t1 = mux_.createTab(ws_id);
    auto t2 = mux_.createTab(ws_id);

    mux_.destroyTab(ws_id, t1);

    auto* ws = mux_.getWorkspace(ws_id);
    EXPECT_EQ(ws->tabs.size(), 1u);
    EXPECT_EQ(ws->tabs[0]->id, t2);
}

TEST_F(MuxTest, ClosePaneUpdatesActivePane) {
    auto ws_id = mux_.createWorkspace();
    auto tab_id = mux_.createTab(ws_id);
    auto panes = mux_.allPanes(ws_id, tab_id);
    PaneId p1 = panes[0];
    PaneId p2 = mux_.splitPane(ws_id, tab_id, p1, SplitDirection::Horizontal);

    mux_.setActivePane(ws_id, tab_id, p2);
    EXPECT_EQ(mux_.activePaneId(ws_id, tab_id), p2);

    mux_.closePane(ws_id, tab_id, p2);
    // Active pane should switch to remaining pane
    EXPECT_EQ(mux_.activePaneId(ws_id, tab_id), p1);
}

TEST_F(MuxTest, DestroyActiveWorkspaceSwitches) {
    auto ws1 = mux_.createWorkspace("ws1");
    auto ws2 = mux_.createWorkspace("ws2");

    mux_.setActiveWorkspace(ws1);
    EXPECT_EQ(mux_.activeWorkspaceId(), ws1);

    mux_.destroyWorkspace(ws1);
    EXPECT_EQ(mux_.activeWorkspaceId(), ws2);
}

// --- Broadcast input tests ---

TEST_F(MuxTest, BroadcastModeCycling) {
    EXPECT_EQ(mux_.broadcastMode(), BroadcastMode::Off);

    mux_.toggleBroadcast();
    EXPECT_EQ(mux_.broadcastMode(), BroadcastMode::All);

    mux_.toggleBroadcast();
    EXPECT_EQ(mux_.broadcastMode(), BroadcastMode::Selected);

    mux_.toggleBroadcast();
    EXPECT_EQ(mux_.broadcastMode(), BroadcastMode::Off);
}

TEST_F(MuxTest, BroadcastSetMode) {
    mux_.setBroadcastMode(BroadcastMode::Selected);
    EXPECT_EQ(mux_.broadcastMode(), BroadcastMode::Selected);

    mux_.setBroadcastMode(BroadcastMode::All);
    EXPECT_EQ(mux_.broadcastMode(), BroadcastMode::All);

    mux_.setBroadcastMode(BroadcastMode::Off);
    EXPECT_EQ(mux_.broadcastMode(), BroadcastMode::Off);
}

TEST_F(MuxTest, BroadcastGetPaneIdsAllMode) {
    auto ws_id = mux_.createWorkspace();
    auto tab_id = mux_.createTab(ws_id);
    auto panes = mux_.allPanes(ws_id, tab_id);
    PaneId p1 = panes[0];
    PaneId p2 = mux_.splitPane(ws_id, tab_id, p1, SplitDirection::Horizontal);

    // Off mode returns empty
    EXPECT_TRUE(mux_.getBroadcastPaneIds().empty());

    // All mode returns all panes in active tab
    mux_.setBroadcastMode(BroadcastMode::All);
    auto broadcastPanes = mux_.getBroadcastPaneIds();
    EXPECT_EQ(broadcastPanes.size(), 2u);
    EXPECT_NE(std::find(broadcastPanes.begin(), broadcastPanes.end(), p1), broadcastPanes.end());
    EXPECT_NE(std::find(broadcastPanes.begin(), broadcastPanes.end(), p2), broadcastPanes.end());
}

TEST_F(MuxTest, BroadcastGetPaneIdsSelectedMode) {
    auto ws_id = mux_.createWorkspace();
    auto tab_id = mux_.createTab(ws_id);
    auto panes = mux_.allPanes(ws_id, tab_id);
    PaneId p1 = panes[0];
    PaneId p2 = mux_.splitPane(ws_id, tab_id, p1, SplitDirection::Horizontal);
    PaneId p3 = mux_.splitPane(ws_id, tab_id, p2, SplitDirection::Vertical);

    mux_.setBroadcastMode(BroadcastMode::Selected);

    // No targets yet
    EXPECT_TRUE(mux_.getBroadcastPaneIds().empty());

    // Add targets
    mux_.addBroadcastTarget(p1);
    mux_.addBroadcastTarget(p3);

    auto broadcastPanes = mux_.getBroadcastPaneIds();
    EXPECT_EQ(broadcastPanes.size(), 2u);
    EXPECT_NE(std::find(broadcastPanes.begin(), broadcastPanes.end(), p1), broadcastPanes.end());
    EXPECT_NE(std::find(broadcastPanes.begin(), broadcastPanes.end(), p3), broadcastPanes.end());
}

TEST_F(MuxTest, BroadcastAddRemoveTargets) {
    mux_.addBroadcastTarget(1);
    mux_.addBroadcastTarget(2);
    mux_.addBroadcastTarget(3);

    mux_.setBroadcastMode(BroadcastMode::Selected);
    EXPECT_EQ(mux_.getBroadcastPaneIds().size(), 3u);

    mux_.removeBroadcastTarget(2);
    EXPECT_EQ(mux_.getBroadcastPaneIds().size(), 2u);

    mux_.clearBroadcastTargets();
    EXPECT_TRUE(mux_.getBroadcastPaneIds().empty());
}

TEST_F(MuxTest, BroadcastAddInvalidPaneIgnored) {
    mux_.addBroadcastTarget(kInvalidPane);
    mux_.setBroadcastMode(BroadcastMode::Selected);
    EXPECT_TRUE(mux_.getBroadcastPaneIds().empty());
}
