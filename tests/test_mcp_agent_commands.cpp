#include <gtest/gtest.h>
#include "termcore/socket/command_dispatcher.h"
#include "termcore/agent.h"
#include "termcore/provider_registry.h"

using namespace termcore;

// ==========================================================================
// Test fixture with full callback wiring
// ==========================================================================

class McpAgentCommandsTest : public ::testing::Test {
protected:
    void SetUp() override {
        next_pane_ = 1;
        write_calls_.clear();

        mux_.setPaneCallbacks(
            [this](int, int) -> PaneId { return next_pane_++; },
            [this](PaneId) {}
        );

        dispatcher_ = std::make_unique<CommandDispatcher>(
            mux_, notifications_, agent_tracker_,
            [this](PaneId id, std::string_view data) -> bool {
                write_calls_.push_back({id, std::string(data)});
                return true;
            },
            nullptr, // webview
            nullptr  // scrollback
        );

        // Wire up read callback that returns mock screen content
        dispatcher_->setPaneReadCallback(
            [](PaneId, int, bool) -> std::vector<std::string> {
                return {"line0", "line1", "line2", "line3", "line4"};
            });

        // Wire up selection callback
        dispatcher_->setSelectionReadCallback(
            [](PaneId) -> std::string {
                return "selected text";
            });

        // Wire up cursor callback
        dispatcher_->setCursorPositionCallback(
            [](PaneId) -> CursorPositionInfo {
                return {10, 25, true};
            });

        // Wire up attention callback
        dispatcher_->setAttentionCallback(
            [this](PaneId pid, float intensity, uint32_t color) {
                last_attention_pane_ = pid;
                last_attention_intensity_ = intensity;
                last_attention_color_ = color;
            });
    }

    rpc::Response dispatch(const std::string& method, nlohmann::json params = {},
                           int64_t id = 1) {
        rpc::Request req;
        req.method = method;
        req.params = std::move(params);
        req.id = id;
        return dispatcher_->dispatch(req);
    }

    Mux mux_;
    NotificationStore notifications_;
    AgentTracker agent_tracker_;
    std::unique_ptr<CommandDispatcher> dispatcher_;

    uint32_t next_pane_ = 1;
    std::vector<std::pair<PaneId, std::string>> write_calls_;

    PaneId last_attention_pane_ = 0;
    float last_attention_intensity_ = 0.0f;
    uint32_t last_attention_color_ = 0;
};

// ==========================================================================
// AgentState enum enhancements
// ==========================================================================

TEST(AgentStateEnhanced, NewStatesExist) {
    // Verify the new states compile and have distinct values
    EXPECT_NE(static_cast<int>(AgentState::Thinking),
              static_cast<int>(AgentState::Running));
    EXPECT_NE(static_cast<int>(AgentState::ToolUse),
              static_cast<int>(AgentState::Running));
    EXPECT_NE(static_cast<int>(AgentState::Waiting),
              static_cast<int>(AgentState::NeedsInput));
    EXPECT_NE(static_cast<int>(AgentState::Error),
              static_cast<int>(AgentState::Exited));
}

TEST(AgentStateEnhanced, StateToString) {
    EXPECT_EQ(AgentTracker::stateToString(AgentState::Thinking), "thinking");
    EXPECT_EQ(AgentTracker::stateToString(AgentState::ToolUse), "tool_use");
    EXPECT_EQ(AgentTracker::stateToString(AgentState::Waiting), "waiting");
    EXPECT_EQ(AgentTracker::stateToString(AgentState::Error), "error");
    EXPECT_EQ(AgentTracker::stateToString(AgentState::Running), "running");
    EXPECT_EQ(AgentTracker::stateToString(AgentState::Idle), "idle");
}

TEST(AgentStateEnhanced, StringToState) {
    EXPECT_EQ(AgentTracker::stringToState("thinking"), AgentState::Thinking);
    EXPECT_EQ(AgentTracker::stringToState("tool_use"), AgentState::ToolUse);
    EXPECT_EQ(AgentTracker::stringToState("waiting"), AgentState::Waiting);
    EXPECT_EQ(AgentTracker::stringToState("error"), AgentState::Error);
    EXPECT_EQ(AgentTracker::stringToState("unknown_value"), AgentState::Inactive);
}

// ==========================================================================
// Pattern-based state detection
// ==========================================================================

TEST(AgentStatePatterns, DefaultPatternsEmptyBeforeLoad) {
    AgentTracker tracker;
    // State patterns are now loaded from ProviderRegistry, not hardcoded
    EXPECT_EQ(tracker.statePatterns().size(), 0u);
}

TEST(AgentStatePatterns, LoadStatePatternsFromRegistry) {
    ProviderRegistry registry;
    ProviderInfo info;
    info.id = "claude_code";
    info.agent_type = "ClaudeCode";
    info.detect_process = {"claude"};
    info.state_patterns = {
        {"thinking", "Thinking..."},
        {"tool_use", "Tool:"},
        {"error", "Error:"},
    };
    registry.registerProvider(std::move(info));

    AgentTracker tracker;
    tracker.loadStatePatternsFrom(registry);
    EXPECT_EQ(tracker.statePatterns().size(), 3u);
}

TEST(AgentStatePatterns, EvaluateOutputDetectsThinking) {
    ProviderRegistry registry;
    ProviderInfo pi;
    pi.id = "claude_code";
    pi.agent_type = "ClaudeCode";
    pi.state_patterns = {{"thinking", "Thinking..."}};
    registry.registerProvider(std::move(pi));

    AgentTracker tracker;
    tracker.loadStatePatternsFrom(registry);
    tracker.reportStart(1, AgentType::ClaudeCode, 100);
    tracker.reportState(1, AgentType::ClaudeCode, AgentState::Idle);

    bool changed = tracker.evaluateOutput(1, "Thinking...");
    EXPECT_TRUE(changed);

    const auto* info = tracker.getAgent(1);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->state, AgentState::Thinking);
}

TEST(AgentStatePatterns, EvaluateOutputDetectsToolUse) {
    ProviderRegistry registry;
    ProviderInfo pi;
    pi.id = "claude_code";
    pi.agent_type = "ClaudeCode";
    pi.state_patterns = {{"tool_use", "Tool:"}};
    registry.registerProvider(std::move(pi));

    AgentTracker tracker;
    tracker.loadStatePatternsFrom(registry);
    tracker.reportStart(1, AgentType::ClaudeCode, 100);
    tracker.reportState(1, AgentType::ClaudeCode, AgentState::Thinking);

    bool changed = tracker.evaluateOutput(1, "Tool: Read file.txt");
    EXPECT_TRUE(changed);

    const auto* info = tracker.getAgent(1);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->state, AgentState::ToolUse);
}

TEST(AgentStatePatterns, EvaluateOutputDetectsError) {
    ProviderRegistry registry;
    ProviderInfo pi;
    pi.id = "claude_code";
    pi.agent_type = "ClaudeCode";
    pi.state_patterns = {{"error", "Error:"}};
    registry.registerProvider(std::move(pi));

    AgentTracker tracker;
    tracker.loadStatePatternsFrom(registry);
    tracker.reportStart(1, AgentType::ClaudeCode, 100);
    tracker.reportState(1, AgentType::ClaudeCode, AgentState::Running);

    bool changed = tracker.evaluateOutput(1, "Error: something failed");
    EXPECT_TRUE(changed);

    const auto* info = tracker.getAgent(1);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->state, AgentState::Error);
}

TEST(AgentStatePatterns, NoTransitionOnSameState) {
    ProviderRegistry registry;
    ProviderInfo pi;
    pi.id = "claude_code";
    pi.agent_type = "ClaudeCode";
    pi.state_patterns = {{"thinking", "Thinking..."}};
    registry.registerProvider(std::move(pi));

    AgentTracker tracker;
    tracker.loadStatePatternsFrom(registry);
    tracker.reportStart(1, AgentType::ClaudeCode, 100);
    tracker.reportState(1, AgentType::ClaudeCode, AgentState::Thinking);

    bool changed = tracker.evaluateOutput(1, "Thinking...");
    EXPECT_FALSE(changed); // Already in Thinking state
}

TEST(AgentStatePatterns, CustomPatternRegistration) {
    AgentTracker tracker;
    tracker.reportStart(1, AgentType::ClaudeCode, 100);
    tracker.reportState(1, AgentType::ClaudeCode, AgentState::Idle);

    AgentStatePattern custom;
    custom.agent_type = AgentType::ClaudeCode;
    custom.target_state = AgentState::Running;
    custom.pattern = "CUSTOM_MARKER";
    tracker.addStatePattern(custom);

    bool changed = tracker.evaluateOutput(1, "Saw CUSTOM_MARKER in output");
    EXPECT_TRUE(changed);

    const auto* info = tracker.getAgent(1);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->state, AgentState::Running);
}

TEST(AgentStatePatterns, UnknownPaneReturnsNop) {
    AgentTracker tracker;
    EXPECT_FALSE(tracker.evaluateOutput(999, "Thinking..."));
}

// ==========================================================================
// terminal.* commands
// ==========================================================================

TEST_F(McpAgentCommandsTest, TerminalGetScreenContent) {
    auto resp = dispatch("terminal.getScreenContent", {{"pane_id", 1}});
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_EQ(resp.result["line_count"], 5);
    EXPECT_EQ(resp.result["lines"].size(), 5u);
    EXPECT_EQ(resp.result["lines"][0], "line0");
}

TEST_F(McpAgentCommandsTest, TerminalGetScreenContentWithRange) {
    auto resp = dispatch("terminal.getScreenContent",
                         {{"pane_id", 1}, {"start_row", 1}, {"end_row", 3}});
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_EQ(resp.result["line_count"], 2);
    EXPECT_EQ(resp.result["lines"][0], "line1");
    EXPECT_EQ(resp.result["lines"][1], "line2");
    EXPECT_EQ(resp.result["start_row"], 1);
    EXPECT_EQ(resp.result["end_row"], 3);
}

TEST_F(McpAgentCommandsTest, TerminalGetScreenContentMissingPaneId) {
    auto resp = dispatch("terminal.getScreenContent", {});
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, rpc::kInvalidParams);
}

TEST_F(McpAgentCommandsTest, TerminalSendInput) {
    auto resp = dispatch("terminal.sendInput",
                         {{"pane_id", 1}, {"text", "hello world"}});
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_EQ(resp.result["bytes_sent"], 11);
    ASSERT_EQ(write_calls_.size(), 1u);
    EXPECT_EQ(write_calls_[0].second, "hello world");
}

TEST_F(McpAgentCommandsTest, TerminalSendInputMissingText) {
    auto resp = dispatch("terminal.sendInput", {{"pane_id", 1}});
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, rpc::kInvalidParams);
}

TEST_F(McpAgentCommandsTest, TerminalGetSelection) {
    auto resp = dispatch("terminal.getSelection", {{"pane_id", 1}});
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_EQ(resp.result["text"], "selected text");
    EXPECT_TRUE(resp.result["has_selection"].get<bool>());
}

TEST_F(McpAgentCommandsTest, TerminalGetCursorPosition) {
    auto resp = dispatch("terminal.getCursorPosition", {{"pane_id", 1}});
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_EQ(resp.result["row"], 10);
    EXPECT_EQ(resp.result["col"], 25);
    EXPECT_TRUE(resp.result["visible"].get<bool>());
}

// ==========================================================================
// agent.setStatus
// ==========================================================================

TEST_F(McpAgentCommandsTest, AgentSetStatus) {
    agent_tracker_.reportStart(1, AgentType::ClaudeCode, 100);

    auto resp = dispatch("agent.setStatus", {
        {"pane_id", 1},
        {"state", "thinking"},
        {"label", "Processing query..."},
        {"icon", "brain"}
    });
    ASSERT_FALSE(resp.error.has_value());

    const auto* info = agent_tracker_.getAgent(1);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->state, AgentState::Thinking);
    EXPECT_EQ(info->custom_label, "Processing query...");
    EXPECT_EQ(info->custom_icon, "brain");
}

TEST_F(McpAgentCommandsTest, AgentSetStatusMissingPaneId) {
    auto resp = dispatch("agent.setStatus", {{"state", "idle"}});
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, rpc::kInvalidParams);
}

// ==========================================================================
// agent.setProgress
// ==========================================================================

TEST_F(McpAgentCommandsTest, AgentSetProgress) {
    auto resp = dispatch("agent.setProgress", {
        {"pane_id", 1}, {"progress", 0.5}, {"label", "Building..."}
    });
    ASSERT_FALSE(resp.error.has_value());

    const auto* progress = dispatcher_->getPaneProgress(1);
    ASSERT_NE(progress, nullptr);
    EXPECT_FLOAT_EQ(progress->value, 0.5f);
    EXPECT_EQ(progress->label, "Building...");
}

TEST_F(McpAgentCommandsTest, AgentSetProgressClear) {
    // Set progress first
    dispatch("agent.setProgress", {{"pane_id", 1}, {"progress", 0.5}});
    ASSERT_NE(dispatcher_->getPaneProgress(1), nullptr);

    // Clear by setting negative
    dispatch("agent.setProgress", {{"pane_id", 1}, {"progress", -1.0}});
    EXPECT_EQ(dispatcher_->getPaneProgress(1), nullptr);
}

// ==========================================================================
// agent.setStatusPills
// ==========================================================================

TEST_F(McpAgentCommandsTest, AgentSetStatusPills) {
    auto resp = dispatch("agent.setStatusPills", {
        {"pane_id", 1},
        {"pills", {{{"text", "Idle"}, {"bg_color", "#00ff00"}, {"fg_color", "#000000"}},
                    {{"text", "v1.2"}, {"bg_color", "#0000ff"}}}}
    });
    ASSERT_FALSE(resp.error.has_value());

    auto pills = dispatcher_->getPaneStatuses(1);
    EXPECT_EQ(pills.size(), 2u);
    EXPECT_EQ(pills[0].key, "Idle");
    EXPECT_EQ(pills[0].value, "#00ff00");
}

// ==========================================================================
// agent.requestAttention
// ==========================================================================

TEST_F(McpAgentCommandsTest, AgentRequestAttention) {
    agent_tracker_.reportStart(1, AgentType::ClaudeCode, 100);

    auto resp = dispatch("agent.requestAttention", {
        {"pane_id", 1},
        {"urgency", "critical"},
        {"message", "Approval needed"},
        {"color", "#ff0000"}
    });
    ASSERT_FALSE(resp.error.has_value());

    // Check notification was created
    EXPECT_EQ(notifications_.count(), 1u);
    auto all = notifications_.all();
    EXPECT_EQ(all[0].body, "Approval needed");
    EXPECT_EQ(all[0].urgency, NotificationUrgency::Critical);

    // Check attention callback was fired
    EXPECT_EQ(last_attention_pane_, 1u);
    EXPECT_FLOAT_EQ(last_attention_intensity_, 1.0f); // critical = 1.0
    EXPECT_EQ(last_attention_color_, 0xff0000u);
}

TEST_F(McpAgentCommandsTest, AgentRequestAttentionDefaultColor) {
    auto resp = dispatch("agent.requestAttention", {
        {"pane_id", 1}, {"message", "Hello"}
    });
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_EQ(last_attention_color_, 0x007accu); // default blue
}

// ==========================================================================
// agent.clearStatus
// ==========================================================================

TEST_F(McpAgentCommandsTest, AgentClearStatus) {
    // Set some status first
    dispatch("agent.setProgress", {{"pane_id", 1}, {"progress", 0.5}});
    dispatch("agent.setStatusPills", {
        {"pane_id", 1}, {"pills", {{{"text", "Test"}}}}
    });

    ASSERT_NE(dispatcher_->getPaneProgress(1), nullptr);
    ASSERT_FALSE(dispatcher_->getPaneStatuses(1).empty());

    auto resp = dispatch("agent.clearStatus", {{"pane_id", 1}});
    ASSERT_FALSE(resp.error.has_value());

    EXPECT_EQ(dispatcher_->getPaneProgress(1), nullptr);
    EXPECT_TRUE(dispatcher_->getPaneStatuses(1).empty());
}

// ==========================================================================
// agent.addStatePattern
// ==========================================================================

TEST_F(McpAgentCommandsTest, AgentAddStatePattern) {
    auto before = agent_tracker_.statePatterns().size();

    auto resp = dispatch("agent.addStatePattern", {
        {"pattern", "CUSTOM_DONE"},
        {"target_state", "idle"},
        {"is_regex", false}
    });
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_EQ(agent_tracker_.statePatterns().size(), before + 1);
}

TEST_F(McpAgentCommandsTest, AgentAddStatePatternMissingFields) {
    auto resp = dispatch("agent.addStatePattern", {{"pattern", "test"}});
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, rpc::kInvalidParams);
}

// ==========================================================================
// workspace.listPanes
// ==========================================================================

TEST_F(McpAgentCommandsTest, WorkspaceListPanes) {
    // Create a workspace with a tab
    auto ws_resp = dispatch("workspace.create", {{"name", "ws1"}});
    auto ws_id = ws_resp.result["workspace_id"].get<WorkspaceId>();
    dispatch("tab.create", {{"workspace_id", ws_id}});

    auto resp = dispatch("workspace.listPanes");
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_TRUE(resp.result.contains("panes"));
    EXPECT_GE(resp.result["panes"].size(), 1u);

    auto& first_pane = resp.result["panes"][0];
    EXPECT_TRUE(first_pane.contains("pane_id"));
    EXPECT_TRUE(first_pane.contains("workspace_id"));
    EXPECT_TRUE(first_pane.contains("tab_id"));
}

TEST_F(McpAgentCommandsTest, WorkspaceListPanesWithAgentInfo) {
    auto ws_resp = dispatch("workspace.create", {{"name", "ws1"}});
    auto ws_id = ws_resp.result["workspace_id"].get<WorkspaceId>();
    auto tab_resp = dispatch("tab.create", {{"workspace_id", ws_id}});

    // Get the pane ID from the tab
    auto panes_resp = dispatch("pane.list",
        {{"workspace_id", ws_id},
         {"tab_id", tab_resp.result["tab_id"].get<TabId>()}});
    auto pane_id = panes_resp.result["panes"][0]["id"].get<PaneId>();

    // Register an agent on that pane
    agent_tracker_.reportStart(pane_id, AgentType::ClaudeCode, 100);

    auto resp = dispatch("workspace.listPanes");
    ASSERT_FALSE(resp.error.has_value());

    bool found = false;
    for (const auto& pane : resp.result["panes"]) {
        if (pane["pane_id"] == pane_id && pane.contains("agent")) {
            found = true;
            EXPECT_EQ(pane["agent"]["name"], "Claude Code");
            break;
        }
    }
    EXPECT_TRUE(found);
}

// ==========================================================================
// workspace.getActivePane
// ==========================================================================

TEST_F(McpAgentCommandsTest, WorkspaceGetActivePane) {
    auto ws_resp = dispatch("workspace.create", {{"name", "ws1"}});
    auto ws_id = ws_resp.result["workspace_id"].get<WorkspaceId>();
    dispatch("workspace.switch", {{"workspace_id", ws_id}});
    dispatch("tab.create", {{"workspace_id", ws_id}});

    auto resp = dispatch("workspace.getActivePane");
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_TRUE(resp.result.contains("pane_id"));
    EXPECT_TRUE(resp.result.contains("workspace_id"));
    EXPECT_TRUE(resp.result.contains("tab_id"));
}

TEST_F(McpAgentCommandsTest, WorkspaceGetActivePaneNoWorkspace) {
    auto resp = dispatch("workspace.getActivePane");
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, rpc::kNotFound);
}

// ==========================================================================
// workspace.getPaneInfo
// ==========================================================================

TEST_F(McpAgentCommandsTest, WorkspaceGetPaneInfo) {
    auto ws_resp = dispatch("workspace.create", {{"name", "test"}});
    auto ws_id = ws_resp.result["workspace_id"].get<WorkspaceId>();
    auto tab_resp = dispatch("tab.create", {{"workspace_id", ws_id}});
    auto tab_id = tab_resp.result["tab_id"].get<TabId>();

    auto panes_resp = dispatch("pane.list",
        {{"workspace_id", ws_id}, {"tab_id", tab_id}});
    auto pane_id = panes_resp.result["panes"][0]["id"].get<PaneId>();

    auto resp = dispatch("workspace.getPaneInfo", {{"pane_id", pane_id}});
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_EQ(resp.result["pane_id"], pane_id);
    EXPECT_EQ(resp.result["workspace_id"], ws_id);
    EXPECT_EQ(resp.result["tab_id"], tab_id);
    EXPECT_EQ(resp.result["workspace_name"], "test");
}

TEST_F(McpAgentCommandsTest, WorkspaceGetPaneInfoNotFound) {
    auto resp = dispatch("workspace.getPaneInfo", {{"pane_id", 9999}});
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, rpc::kNotFound);
}

// ==========================================================================
// agent.broadcast / agent.sendToAgent / agent.listAgents
// ==========================================================================

TEST_F(McpAgentCommandsTest, AgentListAgentsEmpty) {
    auto resp = dispatch("agent.listAgents");
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_EQ(resp.result["count"], 0);
    EXPECT_TRUE(resp.result["agents"].is_array());
}

TEST_F(McpAgentCommandsTest, AgentListAgentsWithAgents) {
    agent_tracker_.reportStart(1, AgentType::ClaudeCode, 100);
    agent_tracker_.reportStart(2, AgentType::Aider, 200);

    auto resp = dispatch("agent.listAgents");
    ASSERT_FALSE(resp.error.has_value());
    EXPECT_EQ(resp.result["count"], 2);
}

TEST_F(McpAgentCommandsTest, AgentBroadcastMissingMessage) {
    auto resp = dispatch("agent.broadcast", {});
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, rpc::kInvalidParams);
}

TEST_F(McpAgentCommandsTest, AgentSendToAgentDirectWrite) {
    // Even if pane is not tracked by orchestrator, should fall back to direct write
    auto resp = dispatch("agent.sendToAgent", {
        {"pane_id", 1}, {"message", "do something"}
    });
    ASSERT_FALSE(resp.error.has_value());
    ASSERT_EQ(write_calls_.size(), 1u);
    EXPECT_EQ(write_calls_[0].second, "do something");
}

// ==========================================================================
// Orchestrator enhanced methods
// ==========================================================================

TEST(AgentOrchestratorEnhanced, BroadcastToAgents) {
    Mux mux;
    std::vector<std::pair<PaneId, std::string>> sent;

    mux.setPaneCallbacks(
        [n = PaneId{1}](int, int) mutable -> PaneId { return n++; },
        [](PaneId) {}
    );

    AgentOrchestrator orch(mux);
    orch.setSendTextCallback([&](PaneId id, const std::string& text) {
        sent.push_back({id, text});
    });

    // Launch 3 agents
    AgentLaunchConfig config;
    config.cli_command = "test";
    config.count = 3;
    config.layout = "grid";
    auto panes = orch.launchAgents(config);
    ASSERT_EQ(panes.size(), 3u);

    sent.clear(); // Clear launch commands

    orch.broadcastToAgents("hello all");
    EXPECT_EQ(sent.size(), 3u);
    for (const auto& [pid, text] : sent) {
        EXPECT_EQ(text, "hello all\r");
    }
}

TEST(AgentOrchestratorEnhanced, SendToSpecificAgent) {
    Mux mux;
    std::vector<std::pair<PaneId, std::string>> sent;

    mux.setPaneCallbacks(
        [n = PaneId{1}](int, int) mutable -> PaneId { return n++; },
        [](PaneId) {}
    );

    AgentOrchestrator orch(mux);
    orch.setSendTextCallback([&](PaneId id, const std::string& text) {
        sent.push_back({id, text});
    });

    AgentLaunchConfig config;
    config.cli_command = "test";
    config.count = 2;
    auto panes = orch.launchAgents(config);
    sent.clear();

    orch.sendToAgent(panes[0], "task for agent 1");
    EXPECT_EQ(sent.size(), 1u);
    EXPECT_EQ(sent[0].first, panes[0]);
}

TEST(AgentOrchestratorEnhanced, IsTracked) {
    Mux mux;
    mux.setPaneCallbacks(
        [n = PaneId{1}](int, int) mutable -> PaneId { return n++; },
        [](PaneId) {}
    );

    AgentOrchestrator orch(mux);
    orch.setSendTextCallback([](PaneId, const std::string&) {});

    EXPECT_FALSE(orch.isTracked(1));

    AgentLaunchConfig config;
    config.cli_command = "test";
    config.count = 1;
    auto panes = orch.launchAgents(config);
    EXPECT_TRUE(orch.isTracked(panes[0]));
    EXPECT_FALSE(orch.isTracked(999));
}

TEST(AgentOrchestratorEnhanced, ListAgents) {
    Mux mux;
    AgentTracker tracker;

    mux.setPaneCallbacks(
        [n = PaneId{1}](int, int) mutable -> PaneId { return n++; },
        [](PaneId) {}
    );

    AgentOrchestrator orch(mux);
    orch.setSendTextCallback([](PaneId, const std::string&) {});

    AgentLaunchConfig config;
    config.cli_command = "claude";
    config.count = 2;
    auto panes = orch.launchAgents(config);

    // Register agents in the tracker
    tracker.reportStart(panes[0], AgentType::ClaudeCode, 100);
    tracker.reportStart(panes[1], AgentType::ClaudeCode, 101);

    auto agents = orch.listAgents(tracker);
    EXPECT_EQ(agents.size(), 2u);
    for (const auto& a : agents) {
        EXPECT_EQ(a.type, AgentType::ClaudeCode);
    }
}
