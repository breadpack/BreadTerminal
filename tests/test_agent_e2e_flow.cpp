#include <gtest/gtest.h>
#include "termcore/hook_bridge.h"
#include "termcore/screen.h"
#include "termcore/vt_parser.h"

#include <nlohmann/json.hpp>
#include <string>

using namespace termcore;

// ---------------------------------------------------------------------------
// E2E flow: OSC 7770 -> VtParser -> OscHookCallback -> HookBridge ->
//           AgentTracker / AgentTreeTracker / NotificationStore
// ---------------------------------------------------------------------------

class AgentE2EFlowTest : public ::testing::Test {
protected:
    Screen screen{24, 80};
    VtParser parser{screen};
    AgentTreeTracker tree;
    AgentTracker tracker;
    NotificationStore notifications;
    HookBridge bridge{tree, tracker, notifications};

    void SetUp() override {
        screen.setOscHookCallback([this](const std::string& json_str) {
            auto event = nlohmann::json::parse(json_str, nullptr, false);
            if (!event.is_discarded()) {
                bridge.processHookEvent(event);
            }
        });
    }

    /// Feed a raw OSC 7770 sequence through the VT parser (ST terminator).
    void feedOsc(const std::string& json) {
        std::string seq = "\033]7770;" + json + "\033\\";
        parser.feed(seq.c_str(), seq.size());
    }

    /// Feed a raw OSC 7770 sequence through the VT parser (BEL terminator).
    void feedOscBel(const std::string& json) {
        std::string seq = "\033]7770;" + json + "\007";
        parser.feed(seq.c_str(), seq.size());
    }
};

// ---------------------------------------------------------------------------
// OscToHookBridgeToTracker
// Feed OSC 7770 StateChange JSON -> HookBridge processes ->
// AgentTreeTracker state updated
// ---------------------------------------------------------------------------
TEST_F(AgentE2EFlowTest, OscToHookBridgeToTracker) {
    // First, create an agent so we have something to update state on
    feedOsc(R"({"event":"SubagentStart","agent_id":"agent-1","agent_type":"claude","description":"coding","pane_id":1})");

    // Verify agent exists and is in Starting state
    auto node = tree.findAgent("agent-1");
    ASSERT_TRUE(node.has_value());
    EXPECT_EQ(node->state, AgentState::Starting);

    // Now send a StateChange event through the full OSC pipeline
    feedOsc(R"({"event":"StateChange","agent_id":"agent-1","pane_id":1,"state":"running"})");

    node = tree.findAgent("agent-1");
    ASSERT_TRUE(node.has_value());
    EXPECT_EQ(node->state, AgentState::Running);
}

// ---------------------------------------------------------------------------
// SubagentStartCreatesTreeNode
// OSC 7770 SubagentStart -> HookBridge -> AgentTreeTracker adds child
// ---------------------------------------------------------------------------
TEST_F(AgentE2EFlowTest, SubagentStartCreatesTreeNode) {
    // Create parent agent via OSC 7770
    feedOsc(R"({"event":"SubagentStart","agent_id":"parent-1","agent_type":"claude","description":"main task","pane_id":2})");

    {
        auto roots = tree.rootAgents(2);
        ASSERT_EQ(roots.size(), 1u);
        EXPECT_EQ(roots[0].agent_id, "parent-1");
        EXPECT_EQ(roots[0].agent_type, "claude");
        EXPECT_EQ(roots[0].description, "main task");
    }

    // Create child subagent under parent via OSC 7770
    feedOsc(R"({"event":"SubagentStart","agent_id":"child-1","agent_type":"Explore","description":"search files","pane_id":2,"parent_agent_id":"parent-1"})");

    {
        auto roots = tree.rootAgents(2);
        ASSERT_EQ(roots.size(), 1u);
        ASSERT_EQ(roots[0].children.size(), 1u);
        EXPECT_EQ(roots[0].children[0].agent_id, "child-1");
        EXPECT_EQ(roots[0].children[0].agent_type, "Explore");
        EXPECT_EQ(roots[0].children[0].description, "search files");
    }
}

// ---------------------------------------------------------------------------
// SubagentStopRemovesFromTree
// OSC 7770 SubagentStop -> tree node marked completed
// ---------------------------------------------------------------------------
TEST_F(AgentE2EFlowTest, SubagentStopRemovesFromTree) {
    feedOsc(R"({"event":"SubagentStart","agent_id":"a1","agent_type":"TDD","description":"test","pane_id":1})");

    auto node = tree.findAgent("a1");
    ASSERT_TRUE(node.has_value());
    EXPECT_EQ(node->state, AgentState::Starting);

    // Stop the agent via OSC 7770
    feedOsc(R"({"event":"SubagentStop","agent_id":"a1","pane_id":1,"state":"completed"})");

    node = tree.findAgent("a1");
    ASSERT_TRUE(node.has_value());
    EXPECT_EQ(node->state, AgentState::Exited);

    // Active count should be zero now
    EXPECT_EQ(tree.activeCount(1), 0u);
}

// ---------------------------------------------------------------------------
// NotificationFromOsc
// OSC 7770 Notification -> HookBridge creates notification
// ---------------------------------------------------------------------------
TEST_F(AgentE2EFlowTest, NotificationFromOsc) {
    EXPECT_EQ(notifications.count(), 0u);

    feedOsc(R"({"event":"Notification","pane_id":1,"title":"Build Complete","body":"All tests passed","urgency":"normal"})");

    EXPECT_EQ(notifications.count(), 1u);
    const auto& all = notifications.all();
    EXPECT_EQ(all[0].title, "Build Complete");
    EXPECT_EQ(all[0].body, "All tests passed");
    EXPECT_EQ(all[0].source, NotificationSource::Agent);
    EXPECT_EQ(all[0].urgency, NotificationUrgency::Normal);
}

// ---------------------------------------------------------------------------
// NotificationFromOscCritical
// OSC 7770 Notification with critical urgency
// ---------------------------------------------------------------------------
TEST_F(AgentE2EFlowTest, NotificationFromOscCritical) {
    feedOsc(R"({"event":"Notification","pane_id":3,"title":"Error","body":"Build failed","urgency":"critical"})");

    ASSERT_EQ(notifications.count(), 1u);
    EXPECT_EQ(notifications.all()[0].urgency, NotificationUrgency::Critical);
}

// ---------------------------------------------------------------------------
// MultiPaneAgentTracking
// Multiple panes with different agents, state tracked independently
// ---------------------------------------------------------------------------
TEST_F(AgentE2EFlowTest, MultiPaneAgentTracking) {
    // Start agents in different panes
    feedOsc(R"({"event":"SubagentStart","agent_id":"pane1-agent","agent_type":"claude","description":"pane 1 task","pane_id":1})");
    feedOsc(R"({"event":"SubagentStart","agent_id":"pane2-agent","agent_type":"codex","description":"pane 2 task","pane_id":2})");
    feedOsc(R"({"event":"SubagentStart","agent_id":"pane3-agent","agent_type":"aider","description":"pane 3 task","pane_id":3})");

    // Verify each pane has its own root agent
    EXPECT_EQ(tree.rootAgents(1).size(), 1u);
    EXPECT_EQ(tree.rootAgents(2).size(), 1u);
    EXPECT_EQ(tree.rootAgents(3).size(), 1u);

    // Change state of pane 1 agent only
    feedOsc(R"({"event":"StateChange","agent_id":"pane1-agent","pane_id":1,"state":"running"})");

    EXPECT_EQ(tree.findAgent("pane1-agent")->state, AgentState::Running);
    EXPECT_EQ(tree.findAgent("pane2-agent")->state, AgentState::Starting);
    EXPECT_EQ(tree.findAgent("pane3-agent")->state, AgentState::Starting);

    // Stop pane 2 agent - others unaffected
    feedOsc(R"({"event":"SubagentStop","agent_id":"pane2-agent","pane_id":2,"state":"completed"})");

    EXPECT_EQ(tree.findAgent("pane1-agent")->state, AgentState::Running);
    EXPECT_EQ(tree.findAgent("pane2-agent")->state, AgentState::Exited);
    EXPECT_EQ(tree.findAgent("pane3-agent")->state, AgentState::Starting);

    EXPECT_EQ(tree.activeCount(1), 1u);
    EXPECT_EQ(tree.activeCount(2), 0u);
    EXPECT_EQ(tree.activeCount(3), 1u);
}

// ---------------------------------------------------------------------------
// RapidStateChanges
// Fast sequence of state changes, final state correct
// ---------------------------------------------------------------------------
TEST_F(AgentE2EFlowTest, RapidStateChanges) {
    feedOsc(R"({"event":"SubagentStart","agent_id":"rapid","agent_type":"claude","description":"rapid test","pane_id":1})");

    // Rapid-fire state changes through the full pipeline
    const char* states[] = {"running", "thinking", "tool_use", "thinking",
                            "running", "waiting", "running", "idle"};
    for (const char* s : states) {
        std::string json = R"({"event":"StateChange","agent_id":"rapid","pane_id":1,"state":")" +
                           std::string(s) + R"("})";
        feedOsc(json);
    }

    // Final state should be idle
    auto node = tree.findAgent("rapid");
    ASSERT_TRUE(node.has_value());
    EXPECT_EQ(node->state, AgentState::Idle);
}

// ---------------------------------------------------------------------------
// MalformedOscGracefullyIgnored
// Invalid JSON in OSC 7770, no crash, no state corruption
// ---------------------------------------------------------------------------
TEST_F(AgentE2EFlowTest, MalformedOscGracefullyIgnored) {
    // Set up a valid agent first
    feedOsc(R"({"event":"SubagentStart","agent_id":"safe","agent_type":"claude","description":"test","pane_id":1})");

    auto node = tree.findAgent("safe");
    ASSERT_TRUE(node.has_value());
    EXPECT_EQ(node->state, AgentState::Starting);

    // Now send various malformed payloads through OSC 7770
    feedOsc("not json at all");
    feedOsc("{broken json");
    feedOsc("");
    feedOsc("{}");
    feedOsc(R"({"event":12345})");
    feedOsc(R"({"event":"StateChange"})");  // missing agent_id
    feedOsc(R"({"no_event_field":true})");

    // Original agent should be completely unaffected
    node = tree.findAgent("safe");
    ASSERT_TRUE(node.has_value());
    EXPECT_EQ(node->state, AgentState::Starting);

    // No spurious tree entries or notifications
    EXPECT_EQ(tree.activeCount(1), 1u);
    EXPECT_EQ(notifications.count(), 0u);
}

// ---------------------------------------------------------------------------
// BellTerminatorAlsoWorks
// OSC 7770 with BEL terminator (\007) goes through same pipeline
// ---------------------------------------------------------------------------
TEST_F(AgentE2EFlowTest, BellTerminatorAlsoWorks) {
    feedOscBel(R"({"event":"SubagentStart","agent_id":"bel-agent","agent_type":"claude","description":"bel test","pane_id":5})");

    auto node = tree.findAgent("bel-agent");
    ASSERT_TRUE(node.has_value());
    EXPECT_EQ(node->agent_type, "claude");
    EXPECT_EQ(node->state, AgentState::Starting);
}

// ---------------------------------------------------------------------------
// FullLifecycleE2E
// Complete agent lifecycle: start -> running -> thinking -> tool_use -> idle -> exit
// ---------------------------------------------------------------------------
TEST_F(AgentE2EFlowTest, FullLifecycleE2E) {
    // Start
    feedOsc(R"({"event":"SubagentStart","agent_id":"lc-1","agent_type":"claude","description":"full lifecycle","pane_id":1})");
    EXPECT_EQ(tree.findAgent("lc-1")->state, AgentState::Starting);
    EXPECT_EQ(tree.activeCount(1), 1u);

    // Running
    feedOsc(R"({"event":"StateChange","agent_id":"lc-1","pane_id":1,"state":"running"})");
    EXPECT_EQ(tree.findAgent("lc-1")->state, AgentState::Running);

    // Thinking
    feedOsc(R"({"event":"StateChange","agent_id":"lc-1","pane_id":1,"state":"thinking"})");
    EXPECT_EQ(tree.findAgent("lc-1")->state, AgentState::Thinking);

    // Tool use
    feedOsc(R"({"event":"StateChange","agent_id":"lc-1","pane_id":1,"state":"tool_use"})");
    EXPECT_EQ(tree.findAgent("lc-1")->state, AgentState::ToolUse);

    // Idle
    feedOsc(R"({"event":"StateChange","agent_id":"lc-1","pane_id":1,"state":"idle"})");
    EXPECT_EQ(tree.findAgent("lc-1")->state, AgentState::Idle);

    // Notification during lifecycle
    feedOsc(R"({"event":"Notification","pane_id":1,"title":"Task Done","body":"Complete","urgency":"low"})");
    EXPECT_EQ(notifications.count(), 1u);
    EXPECT_EQ(notifications.all()[0].urgency, NotificationUrgency::Low);

    // Exit
    feedOsc(R"({"event":"SubagentStop","agent_id":"lc-1","pane_id":1,"state":"completed"})");
    EXPECT_EQ(tree.findAgent("lc-1")->state, AgentState::Exited);
    EXPECT_EQ(tree.activeCount(1), 0u);
}

// ---------------------------------------------------------------------------
// MixedEventsAndNotifications
// Interleaved agent events and notifications across panes
// ---------------------------------------------------------------------------
TEST_F(AgentE2EFlowTest, MixedEventsAndNotifications) {
    feedOsc(R"({"event":"SubagentStart","agent_id":"mix-1","agent_type":"claude","description":"t1","pane_id":1})");
    feedOsc(R"({"event":"Notification","pane_id":1,"title":"N1","body":"b1","urgency":"normal"})");
    feedOsc(R"({"event":"SubagentStart","agent_id":"mix-2","agent_type":"codex","description":"t2","pane_id":2})");
    feedOsc(R"({"event":"StateChange","agent_id":"mix-1","pane_id":1,"state":"running"})");
    feedOsc(R"({"event":"Notification","pane_id":2,"title":"N2","body":"b2","urgency":"critical"})");
    feedOsc(R"({"event":"SubagentStop","agent_id":"mix-2","pane_id":2,"state":"completed"})");

    EXPECT_EQ(tree.findAgent("mix-1")->state, AgentState::Running);
    EXPECT_EQ(tree.findAgent("mix-2")->state, AgentState::Exited);
    EXPECT_EQ(notifications.count(), 2u);
    EXPECT_EQ(tree.activeCount(1), 1u);
    EXPECT_EQ(tree.activeCount(2), 0u);
}
