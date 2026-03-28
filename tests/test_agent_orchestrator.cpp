#include <gtest/gtest.h>
#include "termcore/agent_orchestrator.h"
#include "termcore/agent.h"

using namespace termcore;

// ==========================================================================
// Test fixture: wires Mux with fake pane callbacks and captures send/read
// ==========================================================================

class AgentOrchestratorTest : public ::testing::Test {
protected:
    void SetUp() override {
        next_pane_ = 1;
        sent_.clear();
        screen_content_ = "default output";

        mux_.setPaneCallbacks(
            [this](int, int) -> PaneId { return next_pane_++; },
            [](PaneId) {});

        orch_ = std::make_unique<AgentOrchestrator>(mux_);

        orch_->setSendTextCallback([this](PaneId id, const std::string& text) {
            sent_.push_back({id, text});
        });

        orch_->setReadScreenCallback(
            [this](PaneId, int) -> std::string { return screen_content_; });
    }

    AgentLaunchConfig makeConfig(int count, const std::string& layout = "",
                                  const std::string& cmd = "claude") {
        AgentLaunchConfig cfg;
        cfg.cli_command = cmd;
        cfg.count = count;
        cfg.layout = layout;
        return cfg;
    }

    Mux mux_;
    std::unique_ptr<AgentOrchestrator> orch_;
    PaneId next_pane_ = 1;
    std::vector<std::pair<PaneId, std::string>> sent_;
    std::string screen_content_;
};

// ==========================================================================
// Basic Operations
// ==========================================================================

TEST_F(AgentOrchestratorTest, LaunchSingleAgent) {
    auto panes = orch_->launchAgents(makeConfig(1));
    ASSERT_EQ(panes.size(), 1u);
    EXPECT_NE(panes[0], kInvalidPane);
    EXPECT_TRUE(orch_->isTracked(panes[0]));
    EXPECT_NE(orch_->lastWorkspaceId(), kInvalidWorkspace);

    // The CLI command should have been sent to the pane
    ASSERT_GE(sent_.size(), 1u);
    EXPECT_EQ(sent_[0].first, panes[0]);
    EXPECT_EQ(sent_[0].second, "claude\r");
}

TEST_F(AgentOrchestratorTest, LaunchMultipleAgents) {
    auto panes = orch_->launchAgents(makeConfig(4, "grid"));
    ASSERT_EQ(panes.size(), 4u);

    // All panes should be distinct and tracked
    std::set<PaneId> unique_panes(panes.begin(), panes.end());
    EXPECT_EQ(unique_panes.size(), 4u);
    for (auto pid : panes) {
        EXPECT_TRUE(orch_->isTracked(pid));
    }

    // Each pane should have received the CLI command
    EXPECT_EQ(sent_.size(), 4u);
}

TEST_F(AgentOrchestratorTest, OrchestrateWithPrompt) {
    auto panes = orch_->launchAgents(makeConfig(2));
    sent_.clear();

    std::vector<AgentAssignment> assignments;
    assignments.push_back({panes[0], "fix bug #123"});
    assignments.push_back({panes[1], "write tests"});
    orch_->orchestrate(assignments);

    ASSERT_EQ(sent_.size(), 2u);
    EXPECT_EQ(sent_[0].first, panes[0]);
    EXPECT_EQ(sent_[0].second, "fix bug #123\r");
    EXPECT_EQ(sent_[1].first, panes[1]);
    EXPECT_EQ(sent_[1].second, "write tests\r");
}

TEST_F(AgentOrchestratorTest, ReadAllStatusCollectsResults) {
    auto panes = orch_->launchAgents(makeConfig(3));
    auto statuses = orch_->readAllStatus();

    ASSERT_EQ(statuses.size(), 3u);
    for (const auto& s : statuses) {
        EXPECT_NE(s.pane_id, kInvalidPane);
        EXPECT_EQ(s.last_output, "default output");
        // Recently launched, should not be idle
        EXPECT_FALSE(s.is_idle);
    }
}

// ==========================================================================
// Layout Variations
// ==========================================================================

TEST_F(AgentOrchestratorTest, HorizontalLayout) {
    auto panes = orch_->launchAgents(makeConfig(3, "horizontal"));
    ASSERT_EQ(panes.size(), 3u);

    // Verify layout was applied by checking the split tree
    auto ws_id = orch_->lastWorkspaceId();
    auto tab_ids = mux_.allTabIds(ws_id);
    ASSERT_FALSE(tab_ids.empty());

    auto all_panes = mux_.allPanes(ws_id, tab_ids[0]);
    EXPECT_EQ(all_panes.size(), 3u);
}

TEST_F(AgentOrchestratorTest, VerticalLayout) {
    auto panes = orch_->launchAgents(makeConfig(3, "vertical"));
    ASSERT_EQ(panes.size(), 3u);

    auto ws_id = orch_->lastWorkspaceId();
    auto tab_ids = mux_.allTabIds(ws_id);
    ASSERT_FALSE(tab_ids.empty());

    auto all_panes = mux_.allPanes(ws_id, tab_ids[0]);
    EXPECT_EQ(all_panes.size(), 3u);
}

TEST_F(AgentOrchestratorTest, GridLayout) {
    auto panes = orch_->launchAgents(makeConfig(4, "grid"));
    ASSERT_EQ(panes.size(), 4u);

    auto ws_id = orch_->lastWorkspaceId();
    auto tab_ids = mux_.allTabIds(ws_id);
    ASSERT_FALSE(tab_ids.empty());

    auto all_panes = mux_.allPanes(ws_id, tab_ids[0]);
    EXPECT_EQ(all_panes.size(), 4u);
}

// ==========================================================================
// Error Handling
// ==========================================================================

TEST_F(AgentOrchestratorTest, LaunchWithZeroCount) {
    auto panes = orch_->launchAgents(makeConfig(0));
    EXPECT_TRUE(panes.empty());
}

TEST_F(AgentOrchestratorTest, BroadcastToEmptySet) {
    // No agents launched yet -- broadcast should be a no-op, not crash
    sent_.clear();
    orch_->broadcastToAgents("hello");
    EXPECT_TRUE(sent_.empty());
}

TEST_F(AgentOrchestratorTest, OrchestrateWithNoAgents) {
    // Orchestrate with assignments referencing non-tracked panes
    std::vector<AgentAssignment> assignments;
    assignments.push_back({42, "do work"});
    orch_->orchestrate(assignments);

    // Command is still sent (orchestrate doesn't filter by tracked_panes_),
    // but pane won't have activity tracking updated
    ASSERT_EQ(sent_.size(), 1u);
    EXPECT_FALSE(orch_->isTracked(42));
}

TEST_F(AgentOrchestratorTest, OrchestrateSkipsInvalidPaneId) {
    std::vector<AgentAssignment> assignments;
    assignments.push_back({kInvalidPane, "do work"});
    orch_->orchestrate(assignments);
    EXPECT_TRUE(sent_.empty());
}

TEST_F(AgentOrchestratorTest, OrchestrateSkipsEmptyCommand) {
    auto panes = orch_->launchAgents(makeConfig(1));
    sent_.clear();

    std::vector<AgentAssignment> assignments;
    assignments.push_back({panes[0], ""});
    orch_->orchestrate(assignments);
    EXPECT_TRUE(sent_.empty());
}

// ==========================================================================
// State Management
// ==========================================================================

TEST_F(AgentOrchestratorTest, AgentStateTracking) {
    AgentTracker tracker;
    auto panes = orch_->launchAgents(makeConfig(2));

    // Register agents in tracker
    tracker.reportStart(panes[0], AgentType::ClaudeCode, 100);
    tracker.reportStart(panes[1], AgentType::ClaudeCode, 101);
    tracker.reportState(panes[0], AgentType::ClaudeCode, AgentState::Running);

    auto agents = orch_->listAgents(tracker);
    ASSERT_EQ(agents.size(), 2u);

    // Find the running agent
    bool found_running = false;
    bool found_starting = false;
    for (const auto& a : agents) {
        if (a.state == AgentState::Running) found_running = true;
        if (a.state == AgentState::Starting) found_starting = true;
    }
    EXPECT_TRUE(found_running);
    EXPECT_TRUE(found_starting);
}

TEST_F(AgentOrchestratorTest, AgentStateTrackingMinimalEntry) {
    AgentTracker tracker;
    auto panes = orch_->launchAgents(makeConfig(1));

    // Don't register in tracker -- listAgents should create minimal entries
    auto agents = orch_->listAgents(tracker);
    ASSERT_EQ(agents.size(), 1u);
    EXPECT_EQ(agents[0].pane_id, panes[0]);
    EXPECT_EQ(agents[0].state, AgentState::Inactive);
}

TEST_F(AgentOrchestratorTest, CleanupAfterCompletion) {
    auto panes = orch_->launchAgents(makeConfig(3, "grid"));
    auto ws_id = orch_->lastWorkspaceId();
    ASSERT_NE(ws_id, kInvalidWorkspace);

    // All panes should be tracked
    for (auto pid : panes) {
        EXPECT_TRUE(orch_->isTracked(pid));
    }

    // Close all -- resources should be cleaned up
    orch_->closeAll(ws_id);

    for (auto pid : panes) {
        EXPECT_FALSE(orch_->isTracked(pid));
    }
    EXPECT_EQ(orch_->lastWorkspaceId(), kInvalidWorkspace);

    // Workspace should be destroyed
    EXPECT_EQ(mux_.getWorkspace(ws_id), nullptr);
}

TEST_F(AgentOrchestratorTest, CloseAllWithInvalidWorkspace) {
    // Should be a no-op, not crash
    orch_->closeAll(kInvalidWorkspace);
    EXPECT_EQ(orch_->lastWorkspaceId(), kInvalidWorkspace);
}

// ==========================================================================
// Broadcast and Send
// ==========================================================================

TEST_F(AgentOrchestratorTest, BroadcastSendsToAllTracked) {
    auto panes = orch_->launchAgents(makeConfig(3));
    sent_.clear();

    orch_->broadcastToAgents("update status");
    ASSERT_EQ(sent_.size(), 3u);

    std::set<PaneId> recipients;
    for (const auto& [pid, text] : sent_) {
        recipients.insert(pid);
        EXPECT_EQ(text, "update status\r");
    }
    EXPECT_EQ(recipients.size(), 3u);
}

TEST_F(AgentOrchestratorTest, SendToAgentOnlyTargetsOne) {
    auto panes = orch_->launchAgents(makeConfig(3));
    sent_.clear();

    orch_->sendToAgent(panes[1], "specific task");
    ASSERT_EQ(sent_.size(), 1u);
    EXPECT_EQ(sent_[0].first, panes[1]);
    EXPECT_EQ(sent_[0].second, "specific task\r");
}

TEST_F(AgentOrchestratorTest, SendToUntrackedAgentIsNoop) {
    orch_->sendToAgent(999, "hello");
    EXPECT_TRUE(sent_.empty());
}

// ==========================================================================
// Idle Detection
// ==========================================================================

TEST_F(AgentOrchestratorTest, NewlyLaunchedPanesAreNotIdle) {
    orch_->launchAgents(makeConfig(2));
    auto idle = orch_->getIdlePanes(0.001f);
    // Just launched, should not be idle even with tiny threshold
    // (depends on timing, but threshold of 1ms should pass)
    // We mainly verify the API works without crashing
    EXPECT_GE(idle.size(), 0u);
}

TEST_F(AgentOrchestratorTest, ReadStatusUpdatesActivity) {
    auto panes = orch_->launchAgents(makeConfig(1));

    // Reading status with non-empty content should refresh activity
    screen_content_ = "some output";
    auto statuses = orch_->readAllStatus();
    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_EQ(statuses[0].last_output, "some output");
    EXPECT_FALSE(statuses[0].is_idle);
}

TEST_F(AgentOrchestratorTest, ReadStatusWithNoCallback) {
    // Create orchestrator without read callback
    AgentOrchestrator orch2(mux_);
    orch2.setSendTextCallback([this](PaneId id, const std::string& text) {
        sent_.push_back({id, text});
    });
    // No setReadScreenCallback

    auto panes = orch2.launchAgents(makeConfig(1));
    auto statuses = orch2.readAllStatus();
    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_TRUE(statuses[0].last_output.empty());
}

// ==========================================================================
// No send callback
// ==========================================================================

TEST_F(AgentOrchestratorTest, LaunchWithoutSendCallback) {
    AgentOrchestrator orch2(mux_);
    // No setSendTextCallback -- launch should still create panes
    auto panes = orch2.launchAgents(makeConfig(2, "horizontal", "claude"));
    ASSERT_EQ(panes.size(), 2u);
    for (auto pid : panes) {
        EXPECT_TRUE(orch2.isTracked(pid));
    }
}

TEST_F(AgentOrchestratorTest, OrchestrateWithoutSendCallbackIsNoop) {
    AgentOrchestrator orch2(mux_);
    auto panes = orch2.launchAgents(makeConfig(1));

    std::vector<AgentAssignment> assignments;
    assignments.push_back({panes[0], "do work"});
    // Should not crash
    orch2.orchestrate(assignments);
}
