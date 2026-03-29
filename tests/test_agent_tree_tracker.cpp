#include "termcore/agent_tree_tracker.h"

#include <gtest/gtest.h>

using namespace termcore;

TEST(AgentTreeTracker, StartsEmpty) {
    AgentTreeTracker tracker;
    EXPECT_TRUE(tracker.rootAgents(1).empty());
    EXPECT_EQ(tracker.activeCount(1), 0u);
}

TEST(AgentTreeTracker, OnAgentStartAddsRoot) {
    AgentTreeTracker tracker;
    tracker.onAgentStart(1, "agent-1", "claude", "doing stuff");

    auto roots = tracker.rootAgents(1);
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0].agent_id, "agent-1");
    EXPECT_EQ(roots[0].agent_type, "claude");
    EXPECT_EQ(roots[0].description, "doing stuff");
    EXPECT_EQ(roots[0].state, AgentState::Starting);
}

TEST(AgentTreeTracker, OnAgentStartAddsChild) {
    AgentTreeTracker tracker;
    tracker.onAgentStart(1, "parent-1", "claude", "parent task");
    tracker.onAgentStart(1, "child-1", "claude", "child task", "parent-1");

    auto roots = tracker.rootAgents(1);
    ASSERT_EQ(roots.size(), 1u);
    ASSERT_EQ(roots[0].children.size(), 1u);
    EXPECT_EQ(roots[0].children[0].agent_id, "child-1");
    EXPECT_EQ(roots[0].children[0].description, "child task");
}

TEST(AgentTreeTracker, OnAgentStopUpdatesState) {
    AgentTreeTracker tracker;
    tracker.onAgentStart(1, "agent-1", "claude", "task");
    tracker.onAgentStop(1, "agent-1", AgentState::Exited);

    auto agent = tracker.findAgent("agent-1");
    ASSERT_TRUE(agent.has_value());
    EXPECT_EQ(agent->state, AgentState::Exited);
}

TEST(AgentTreeTracker, ActiveCountIgnoresExited) {
    AgentTreeTracker tracker;
    tracker.onAgentStart(1, "agent-1", "claude", "task1");
    tracker.onAgentStart(1, "agent-2", "claude", "task2");
    EXPECT_EQ(tracker.activeCount(1), 2u);

    tracker.onAgentStop(1, "agent-1", AgentState::Exited);
    EXPECT_EQ(tracker.activeCount(1), 1u);

    tracker.onAgentStop(1, "agent-2", AgentState::Exited);
    EXPECT_EQ(tracker.activeCount(1), 0u);
}

TEST(AgentTreeTracker, ClearForPane) {
    AgentTreeTracker tracker;
    tracker.onAgentStart(1, "agent-1", "claude", "task1");
    tracker.onAgentStart(2, "agent-2", "claude", "task2");

    tracker.clearForPane(1);
    EXPECT_TRUE(tracker.rootAgents(1).empty());
    EXPECT_EQ(tracker.rootAgents(2).size(), 1u);
}

TEST(AgentTreeTracker, OnAgentStateChange) {
    AgentTreeTracker tracker;
    tracker.onAgentStart(1, "agent-1", "claude", "task");
    tracker.onAgentStateChange(1, "agent-1", AgentState::Running);

    auto agent = tracker.findAgent("agent-1");
    ASSERT_TRUE(agent.has_value());
    EXPECT_EQ(agent->state, AgentState::Running);
}

TEST(AgentTreeTracker, FindAgentAcrossPanes) {
    AgentTreeTracker tracker;
    tracker.onAgentStart(1, "agent-1", "claude", "task1");
    tracker.onAgentStart(2, "agent-2", "codex", "task2");

    auto a1 = tracker.findAgent("agent-1");
    auto a2 = tracker.findAgent("agent-2");
    auto a3 = tracker.findAgent("nonexistent");

    ASSERT_TRUE(a1.has_value());
    EXPECT_EQ(a1->agent_type, "claude");
    ASSERT_TRUE(a2.has_value());
    EXPECT_EQ(a2->agent_type, "codex");
    EXPECT_FALSE(a3.has_value());
}

TEST(AgentTreeTracker, ChildWithMissingParentBecomesRoot) {
    AgentTreeTracker tracker;
    tracker.onAgentStart(1, "child-1", "claude", "orphan", "nonexistent-parent");

    auto roots = tracker.rootAgents(1);
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0].agent_id, "child-1");
}
