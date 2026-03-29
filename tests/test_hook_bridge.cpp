#include <gtest/gtest.h>
#include "termcore/hook_bridge.h"

class HookBridgeTest : public ::testing::Test {
protected:
    termcore::AgentTreeTracker tree;
    termcore::AgentTracker tracker;
    termcore::NotificationStore notifications;
    termcore::HookBridge bridge{tree, tracker, notifications};
};

TEST_F(HookBridgeTest, SubagentStartCreatesNode) {
    nlohmann::json event = {
        {"event", "SubagentStart"}, {"agent_id", "a1"},
        {"agent_type", "Explore"}, {"description", "Find files"}, {"pane_id", 1}
    };
    bridge.processHookEvent(event);
    ASSERT_EQ(tree.rootAgents(1).size(), 1u);
    ASSERT_EQ(tree.rootAgents(1)[0].agent_type, "Explore");
}

TEST_F(HookBridgeTest, SubagentStopUpdatesState) {
    bridge.processHookEvent({{"event","SubagentStart"},{"agent_id","a1"},
        {"agent_type","TDD"},{"description","d"},{"pane_id",1}});
    bridge.processHookEvent({{"event","SubagentStop"},{"agent_id","a1"},
        {"pane_id",1},{"state","completed"}});
    auto node = tree.findAgent("a1");
    ASSERT_TRUE(node.has_value());
    ASSERT_EQ(node->state, termcore::AgentState::Exited);
}

TEST_F(HookBridgeTest, NotificationAddsEntry) {
    bridge.processHookEvent({{"event","Notification"},{"pane_id",1},
        {"title","Done"},{"body","Task completed"},{"urgency","normal"}});
    ASSERT_EQ(notifications.count(), 1u);
}

TEST_F(HookBridgeTest, UnknownEventIgnored) {
    bridge.processHookEvent({{"event","FutureEvent"},{"data","x"}});
    ASSERT_TRUE(tree.rootAgents(1).empty());
    ASSERT_EQ(notifications.count(), 0u);
}

TEST_F(HookBridgeTest, MissingFieldsGraceful) {
    bridge.processHookEvent({{"event","SubagentStart"}}); // missing agent_id
    ASSERT_TRUE(tree.rootAgents(0).empty());
}
