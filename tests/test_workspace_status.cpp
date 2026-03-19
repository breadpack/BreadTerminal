#include "termcore/workspace_status.h"

#include <gtest/gtest.h>

class WorkspaceStatusTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup pane creation callback with incrementing IDs
        mux_.setPaneCallbacks(
            [this](int /*rows*/, int /*cols*/) -> termcore::PaneId {
                return next_pane_id_++;
            },
            [](termcore::PaneId) {}
        );
    }

    termcore::Mux mux_;
    termcore::AgentTracker agents_;
    termcore::NotificationStore notifications_;
    termcore::PaneId next_pane_id_ = 1;
};

TEST_F(WorkspaceStatusTest, EmptyMuxReturnsNoSnapshots) {
    termcore::WorkspaceStatusProvider provider(mux_, agents_, notifications_);
    auto snaps = provider.currentSnapshots();
    EXPECT_TRUE(snaps.empty());
}

TEST_F(WorkspaceStatusTest, SingleWorkspaceSnapshot) {
    auto ws_id = mux_.createWorkspace("dev");
    mux_.createTab(ws_id);

    termcore::WorkspaceStatusProvider provider(mux_, agents_, notifications_);
    provider.setCwd(ws_id, "/home/user/project");

    auto snaps = provider.currentSnapshots();
    ASSERT_EQ(snaps.size(), 1u);
    EXPECT_EQ(snaps[0].id, ws_id);
    EXPECT_EQ(snaps[0].name, "dev");
    EXPECT_TRUE(snaps[0].is_active);
    EXPECT_EQ(snaps[0].cwd, "/home/user/project");
    EXPECT_EQ(snaps[0].dominant_agent_state, termcore::AgentState::Inactive);
    EXPECT_EQ(snaps[0].unread_notification_count, 0u);
}

TEST_F(WorkspaceStatusTest, MultipleWorkspacesActiveTracking) {
    auto ws1 = mux_.createWorkspace("ws1");
    auto ws2 = mux_.createWorkspace("ws2");
    mux_.createTab(ws1);
    mux_.createTab(ws2);
    mux_.setActiveWorkspace(ws2);

    termcore::WorkspaceStatusProvider provider(mux_, agents_, notifications_);
    auto snaps = provider.currentSnapshots();

    ASSERT_EQ(snaps.size(), 2u);
    EXPECT_FALSE(snaps[0].is_active);  // ws1
    EXPECT_TRUE(snaps[1].is_active);   // ws2
}

TEST_F(WorkspaceStatusTest, DominantAgentStatePriority) {
    auto ws_id = mux_.createWorkspace("test");
    auto tab_id = mux_.createTab(ws_id);

    // Get the pane created by createTab
    auto panes = mux_.allPanes(ws_id, tab_id);
    ASSERT_EQ(panes.size(), 1u);
    auto pane1 = panes[0];

    // Split to get a second pane
    auto pane2 = mux_.splitPane(ws_id, tab_id, pane1,
                                termcore::SplitDirection::Horizontal);
    ASSERT_NE(pane2, termcore::kInvalidPane);

    // Set agent states: one Idle, one Running
    agents_.reportState(pane1, termcore::AgentType::ClaudeCode,
                        termcore::AgentState::Idle);
    agents_.reportState(pane2, termcore::AgentType::ClaudeCode,
                        termcore::AgentState::Running);

    termcore::WorkspaceStatusProvider provider(mux_, agents_, notifications_);
    auto snaps = provider.currentSnapshots();

    ASSERT_EQ(snaps.size(), 1u);
    // Running > Idle
    EXPECT_EQ(snaps[0].dominant_agent_state, termcore::AgentState::Running);
}

TEST_F(WorkspaceStatusTest, NeedsInputDominatesRunning) {
    auto ws_id = mux_.createWorkspace("test");
    auto tab_id = mux_.createTab(ws_id);

    auto panes = mux_.allPanes(ws_id, tab_id);
    auto pane1 = panes[0];
    auto pane2 = mux_.splitPane(ws_id, tab_id, pane1,
                                termcore::SplitDirection::Vertical);

    agents_.reportState(pane1, termcore::AgentType::ClaudeCode,
                        termcore::AgentState::Running);
    agents_.reportState(pane2, termcore::AgentType::ClaudeCode,
                        termcore::AgentState::NeedsInput);

    termcore::WorkspaceStatusProvider provider(mux_, agents_, notifications_);
    auto snaps = provider.currentSnapshots();

    ASSERT_EQ(snaps.size(), 1u);
    EXPECT_EQ(snaps[0].dominant_agent_state, termcore::AgentState::NeedsInput);
}

TEST_F(WorkspaceStatusTest, UnreadNotificationCounting) {
    auto ws_id = mux_.createWorkspace("test");
    auto tab_id = mux_.createTab(ws_id);
    auto panes = mux_.allPanes(ws_id, tab_id);
    auto pane1 = panes[0];

    // Add some notifications
    notifications_.add(pane1, termcore::NotificationSource::Agent,
                       termcore::NotificationUrgency::Normal,
                       "Title 1", "Body 1");
    notifications_.add(pane1, termcore::NotificationSource::Agent,
                       termcore::NotificationUrgency::Normal,
                       "Title 2", "Body 2");
    auto id3 = notifications_.add(pane1, termcore::NotificationSource::Agent,
                                  termcore::NotificationUrgency::Normal,
                                  "Title 3", "Body 3");

    // Mark one as read
    notifications_.markRead(id3);

    termcore::WorkspaceStatusProvider provider(mux_, agents_, notifications_);
    auto snaps = provider.currentSnapshots();

    ASSERT_EQ(snaps.size(), 1u);
    EXPECT_EQ(snaps[0].unread_notification_count, 2u);
}

TEST_F(WorkspaceStatusTest, MuxChangeCallbackTriggersRefresh) {
    termcore::WorkspaceStatusProvider provider(mux_, agents_, notifications_);

    int callback_count = 0;
    std::vector<termcore::WorkspaceStatusSnapshot> last_snapshots;

    provider.setOnChanged([&](const std::vector<termcore::WorkspaceStatusSnapshot>& snaps) {
        ++callback_count;
        last_snapshots = snaps;
    });

    // Creating a workspace fires mux on_changed which triggers refresh
    auto ws_id = mux_.createWorkspace("test");
    EXPECT_GE(callback_count, 1);
    ASSERT_EQ(last_snapshots.size(), 1u);
    EXPECT_EQ(last_snapshots[0].name, "test");
}

TEST_F(WorkspaceStatusTest, NoCwdMeansNoGitBranch) {
    auto ws_id = mux_.createWorkspace("test");
    mux_.createTab(ws_id);
    // Don't set CWD

    termcore::WorkspaceStatusProvider provider(mux_, agents_, notifications_);
    auto snaps = provider.currentSnapshots();

    ASSERT_EQ(snaps.size(), 1u);
    EXPECT_TRUE(snaps[0].git_branch.empty());
    EXPECT_TRUE(snaps[0].cwd.empty());
}

TEST_F(WorkspaceStatusTest, InactiveAgentStateWhenNoPanes) {
    auto ws_id = mux_.createWorkspace("empty");
    // No tabs or panes created

    termcore::WorkspaceStatusProvider provider(mux_, agents_, notifications_);
    auto snaps = provider.currentSnapshots();

    ASSERT_EQ(snaps.size(), 1u);
    EXPECT_EQ(snaps[0].dominant_agent_state, termcore::AgentState::Inactive);
}
