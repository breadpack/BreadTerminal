#include <gtest/gtest.h>
#include "termcore/broadcast.h"

using namespace termcore;

class BroadcastTest : public ::testing::Test {
protected:
    void SetUp() override {
        next_pane_ = 1;
        mux_.setPaneCallbacks(
            [this](int, int) -> PaneId { return next_pane_++; },
            [this](PaneId) {}
        );
        ws_id_ = mux_.createWorkspace("test");
        tab_id_ = mux_.createTab(ws_id_);
    }

    Mux mux_;
    BroadcastManager broadcast_;
    uint32_t next_pane_ = 1;
    WorkspaceId ws_id_ = kInvalidWorkspace;
    TabId tab_id_ = kInvalidTab;
};

// --- Mode switching ---

TEST_F(BroadcastTest, DefaultModeIsOff) {
    EXPECT_EQ(broadcast_.mode(), BroadcastMode::Off);
    EXPECT_FALSE(broadcast_.isActive());
}

TEST_F(BroadcastTest, SetModeAllPanes) {
    broadcast_.setMode(BroadcastMode::AllPanes);
    EXPECT_EQ(broadcast_.mode(), BroadcastMode::AllPanes);
    EXPECT_TRUE(broadcast_.isActive());
}

TEST_F(BroadcastTest, SetModeSelectedPanes) {
    broadcast_.setMode(BroadcastMode::SelectedPanes);
    EXPECT_EQ(broadcast_.mode(), BroadcastMode::SelectedPanes);
    EXPECT_TRUE(broadcast_.isActive());
}

TEST_F(BroadcastTest, CycleModes) {
    broadcast_.setMode(BroadcastMode::Off);
    EXPECT_EQ(broadcast_.mode(), BroadcastMode::Off);

    broadcast_.setMode(BroadcastMode::AllPanes);
    EXPECT_EQ(broadcast_.mode(), BroadcastMode::AllPanes);

    broadcast_.setMode(BroadcastMode::SelectedPanes);
    EXPECT_EQ(broadcast_.mode(), BroadcastMode::SelectedPanes);

    broadcast_.setMode(BroadcastMode::Off);
    EXPECT_EQ(broadcast_.mode(), BroadcastMode::Off);
    EXPECT_FALSE(broadcast_.isActive());
}

// --- Target management ---

TEST_F(BroadcastTest, AddAndRemoveTarget) {
    broadcast_.addTarget(1);
    broadcast_.addTarget(2);
    EXPECT_TRUE(broadcast_.hasTarget(1));
    EXPECT_TRUE(broadcast_.hasTarget(2));
    EXPECT_FALSE(broadcast_.hasTarget(3));

    broadcast_.removeTarget(1);
    EXPECT_FALSE(broadcast_.hasTarget(1));
    EXPECT_TRUE(broadcast_.hasTarget(2));
}

TEST_F(BroadcastTest, ClearTargets) {
    broadcast_.addTarget(1);
    broadcast_.addTarget(2);
    broadcast_.addTarget(3);
    broadcast_.clearTargets();
    EXPECT_FALSE(broadcast_.hasTarget(1));
    EXPECT_FALSE(broadcast_.hasTarget(2));
    EXPECT_FALSE(broadcast_.hasTarget(3));
}

TEST_F(BroadcastTest, DuplicateAddIsIdempotent) {
    broadcast_.addTarget(1);
    broadcast_.addTarget(1);
    EXPECT_TRUE(broadcast_.hasTarget(1));
    broadcast_.removeTarget(1);
    EXPECT_FALSE(broadcast_.hasTarget(1));
}

// --- getTargets ---

TEST_F(BroadcastTest, GetTargetsOffReturnsEmpty) {
    broadcast_.setMode(BroadcastMode::Off);
    auto targets = broadcast_.getTargets(mux_, ws_id_, tab_id_);
    EXPECT_TRUE(targets.empty());
}

TEST_F(BroadcastTest, GetTargetsAllPanesReturnsSinglePane) {
    broadcast_.setMode(BroadcastMode::AllPanes);
    auto targets = broadcast_.getTargets(mux_, ws_id_, tab_id_);
    EXPECT_EQ(targets.size(), 1u);
}

TEST_F(BroadcastTest, GetTargetsAllPanesReturnsAllPanes) {
    // Split to create multiple panes
    auto panes = mux_.allPanes(ws_id_, tab_id_);
    ASSERT_EQ(panes.size(), 1u);
    mux_.splitPane(ws_id_, tab_id_, panes[0], SplitDirection::Horizontal);
    auto all = mux_.allPanes(ws_id_, tab_id_);
    ASSERT_EQ(all.size(), 2u);

    broadcast_.setMode(BroadcastMode::AllPanes);
    auto targets = broadcast_.getTargets(mux_, ws_id_, tab_id_);
    EXPECT_EQ(targets.size(), 2u);
    EXPECT_EQ(targets[0], all[0]);
    EXPECT_EQ(targets[1], all[1]);
}

TEST_F(BroadcastTest, GetTargetsSelectedPanesReturnsOnlySelected) {
    auto panes = mux_.allPanes(ws_id_, tab_id_);
    PaneId p2 = mux_.splitPane(ws_id_, tab_id_, panes[0], SplitDirection::Horizontal);
    PaneId p3 = mux_.splitPane(ws_id_, tab_id_, p2, SplitDirection::Vertical);

    broadcast_.setMode(BroadcastMode::SelectedPanes);
    broadcast_.addTarget(panes[0]);
    broadcast_.addTarget(p3);

    auto targets = broadcast_.getTargets(mux_, ws_id_, tab_id_);
    EXPECT_EQ(targets.size(), 2u);
    // Should contain panes[0] and p3 but not p2
    bool has_p0 = false, has_p2 = false, has_p3 = false;
    for (auto id : targets) {
        if (id == panes[0]) has_p0 = true;
        if (id == p2) has_p2 = true;
        if (id == p3) has_p3 = true;
    }
    EXPECT_TRUE(has_p0);
    EXPECT_FALSE(has_p2);
    EXPECT_TRUE(has_p3);
}

TEST_F(BroadcastTest, GetTargetsSelectedPanesFiltersDeletedPanes) {
    auto panes = mux_.allPanes(ws_id_, tab_id_);
    PaneId p2 = mux_.splitPane(ws_id_, tab_id_, panes[0], SplitDirection::Horizontal);

    broadcast_.setMode(BroadcastMode::SelectedPanes);
    broadcast_.addTarget(panes[0]);
    broadcast_.addTarget(p2);
    // Target a non-existent pane
    broadcast_.addTarget(999);

    auto targets = broadcast_.getTargets(mux_, ws_id_, tab_id_);
    EXPECT_EQ(targets.size(), 2u);
}

// --- broadcastInput ---

TEST_F(BroadcastTest, BroadcastWritesToAllTargets) {
    auto panes = mux_.allPanes(ws_id_, tab_id_);
    PaneId p2 = mux_.splitPane(ws_id_, tab_id_, panes[0], SplitDirection::Horizontal);

    broadcast_.setMode(BroadcastMode::AllPanes);

    std::vector<std::pair<PaneId, std::string>> writes;
    auto write_cb = [&writes](PaneId id, const std::string& data) {
        writes.emplace_back(id, data);
    };

    broadcast_.broadcastInput("hello", mux_, ws_id_, tab_id_, write_cb);

    ASSERT_EQ(writes.size(), 2u);
    EXPECT_EQ(writes[0].second, "hello");
    EXPECT_EQ(writes[1].second, "hello");
    // Both panes should have received the data
    bool got_p1 = false, got_p2 = false;
    for (auto& [id, data] : writes) {
        if (id == panes[0]) got_p1 = true;
        if (id == p2) got_p2 = true;
    }
    EXPECT_TRUE(got_p1);
    EXPECT_TRUE(got_p2);
}

TEST_F(BroadcastTest, BroadcastOffDoesNotWrite) {
    broadcast_.setMode(BroadcastMode::Off);

    std::vector<std::pair<PaneId, std::string>> writes;
    auto write_cb = [&writes](PaneId id, const std::string& data) {
        writes.emplace_back(id, data);
    };

    broadcast_.broadcastInput("hello", mux_, ws_id_, tab_id_, write_cb);
    EXPECT_TRUE(writes.empty());
}

// --- Mux::getVisiblePaneIds ---

TEST_F(BroadcastTest, GetVisiblePaneIds) {
    auto visible = mux_.getVisiblePaneIds();
    EXPECT_EQ(visible.size(), 1u);

    auto panes = mux_.allPanes(ws_id_, tab_id_);
    mux_.splitPane(ws_id_, tab_id_, panes[0], SplitDirection::Horizontal);

    visible = mux_.getVisiblePaneIds();
    EXPECT_EQ(visible.size(), 2u);
}

TEST_F(BroadcastTest, GetVisiblePaneIdsEmptyWhenNoWorkspace) {
    Mux empty_mux;
    auto visible = empty_mux.getVisiblePaneIds();
    EXPECT_TRUE(visible.empty());
}
