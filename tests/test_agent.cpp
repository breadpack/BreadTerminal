#include "termcore/agent.h"
#include "termcore/provider_registry.h"

#include <gtest/gtest.h>

using namespace termcore;

TEST(AgentTracker, DefaultPatternsRegistered) {
    AgentTracker tracker;
    EXPECT_GT(tracker.registeredCount(), 0u);
    // Should have at least 8 default patterns
    EXPECT_GE(tracker.registeredCount(), 8u);
}

TEST(AgentTracker, DetectClaudeCode) {
    AgentTracker tracker;
    EXPECT_EQ(tracker.detectAgent("claude"), AgentType::ClaudeCode);
}

TEST(AgentTracker, DetectCodex) {
    AgentTracker tracker;
    EXPECT_EQ(tracker.detectAgent("codex"), AgentType::Codex);
}

TEST(AgentTracker, DetectUnknown) {
    AgentTracker tracker;
    EXPECT_EQ(tracker.detectAgent("unknown_thing"), AgentType::Unknown);
}

TEST(AgentTracker, DetectCaseInsensitive) {
    AgentTracker tracker;
    EXPECT_EQ(tracker.detectAgent("Claude"), AgentType::ClaudeCode);
    EXPECT_EQ(tracker.detectAgent("CLAUDE"), AgentType::ClaudeCode);
    EXPECT_EQ(tracker.detectAgent("CODEX"), AgentType::Codex);
    EXPECT_EQ(tracker.detectAgent("Aider"), AgentType::Aider);
}

TEST(AgentTracker, DetectByEnvMarker) {
    AgentTracker tracker;
    EXPECT_EQ(tracker.detectAgent("some_process", {"CLAUDE_CODE_SESSION=1"}),
              AgentType::ClaudeCode);
    EXPECT_EQ(tracker.detectAgent("some_process", {"CODEX_SESSION=abc"}),
              AgentType::Codex);
}

TEST(AgentTracker, ReportStartCreatesAgent) {
    AgentTracker tracker;
    tracker.reportStart(1, AgentType::ClaudeCode, 12345);

    const AgentInfo* info = tracker.getAgent(1);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->type, AgentType::ClaudeCode);
    EXPECT_EQ(info->state, AgentState::Starting);
    EXPECT_EQ(info->pid, 12345);
    EXPECT_EQ(info->pane_id, 1u);
    EXPECT_EQ(info->name, "Claude Code");
    EXPECT_EQ(info->process_name, "claude");
}

TEST(AgentTracker, ReportStateUpdates) {
    AgentTracker tracker;
    tracker.reportStart(1, AgentType::ClaudeCode, 100);
    tracker.reportState(1, AgentType::ClaudeCode, AgentState::Running,
                        "Processing query");

    const AgentInfo* info = tracker.getAgent(1);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->state, AgentState::Running);
    EXPECT_EQ(info->last_message, "Processing query");
}

TEST(AgentTracker, ReportExitSetsExitedState) {
    AgentTracker tracker;
    tracker.reportStart(1, AgentType::Aider, 200);
    tracker.reportExit(1);

    const AgentInfo* info = tracker.getAgent(1);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->state, AgentState::Exited);
}

TEST(AgentTracker, ReportExitNonexistentPaneIsNoop) {
    AgentTracker tracker;
    // Should not crash
    tracker.reportExit(999);
    EXPECT_EQ(tracker.getAgent(999), nullptr);
}

TEST(AgentTracker, AnyNeedsInputFalseInitially) {
    AgentTracker tracker;
    EXPECT_FALSE(tracker.anyNeedsInput());
}

TEST(AgentTracker, AnyNeedsInputTrueAfterReport) {
    AgentTracker tracker;
    tracker.reportState(1, AgentType::ClaudeCode, AgentState::NeedsInput,
                        "Approve file write?");
    EXPECT_TRUE(tracker.anyNeedsInput());
}

TEST(AgentTracker, AllAgentsReturnTracked) {
    AgentTracker tracker;
    EXPECT_TRUE(tracker.allAgents().empty());

    tracker.reportStart(1, AgentType::ClaudeCode, 100);
    tracker.reportStart(2, AgentType::Aider, 200);

    auto all = tracker.allAgents();
    EXPECT_EQ(all.size(), 2u);
}

TEST(AgentTracker, AgentsInStateFilters) {
    AgentTracker tracker;
    tracker.reportStart(1, AgentType::ClaudeCode, 100);
    tracker.reportStart(2, AgentType::Aider, 200);
    tracker.reportState(1, AgentType::ClaudeCode, AgentState::Running);

    auto starting = tracker.agentsInState(AgentState::Starting);
    EXPECT_EQ(starting.size(), 1u);
    EXPECT_EQ(starting[0]->type, AgentType::Aider);

    auto running = tracker.agentsInState(AgentState::Running);
    EXPECT_EQ(running.size(), 1u);
    EXPECT_EQ(running[0]->type, AgentType::ClaudeCode);

    auto idle = tracker.agentsInState(AgentState::Idle);
    EXPECT_TRUE(idle.empty());
}

TEST(AgentTracker, RegisterCustomAgent) {
    AgentTracker tracker;
    size_t before = tracker.registeredCount();
    tracker.registerAgent(AgentType::Custom, "MyAgent", "myagent",
                          {"MY_AGENT_SESSION"});
    EXPECT_EQ(tracker.registeredCount(), before + 1);

    EXPECT_EQ(tracker.detectAgent("myagent"), AgentType::Custom);
    EXPECT_EQ(tracker.detectAgent("other", {"MY_AGENT_SESSION=1"}),
              AgentType::Custom);
}

TEST(AgentTracker, StateCallbackFires) {
    AgentTracker tracker;
    uint32_t cb_pane = 0;
    AgentState cb_state = AgentState::Inactive;
    int cb_count = 0;

    tracker.setStateCallback(
        [&](uint32_t pane_id, const AgentInfo& info) {
            cb_pane = pane_id;
            cb_state = info.state;
            cb_count++;
        });

    tracker.reportStart(5, AgentType::Codex, 500);
    EXPECT_EQ(cb_count, 1);
    EXPECT_EQ(cb_pane, 5u);
    EXPECT_EQ(cb_state, AgentState::Starting);

    tracker.reportState(5, AgentType::Codex, AgentState::Running, "working");
    EXPECT_EQ(cb_count, 2);
    EXPECT_EQ(cb_state, AgentState::Running);

    tracker.reportExit(5);
    EXPECT_EQ(cb_count, 3);
    EXPECT_EQ(cb_state, AgentState::Exited);
}

TEST(AgentTracker, MultiplePanesIndependent) {
    AgentTracker tracker;
    tracker.reportStart(1, AgentType::ClaudeCode, 100);
    tracker.reportStart(2, AgentType::Aider, 200);

    tracker.reportState(1, AgentType::ClaudeCode, AgentState::Running);

    const AgentInfo* a1 = tracker.getAgent(1);
    const AgentInfo* a2 = tracker.getAgent(2);

    ASSERT_NE(a1, nullptr);
    ASSERT_NE(a2, nullptr);
    EXPECT_EQ(a1->state, AgentState::Running);
    EXPECT_EQ(a2->state, AgentState::Starting);

    tracker.reportExit(1);
    EXPECT_EQ(tracker.getAgent(1)->state, AgentState::Exited);
    EXPECT_EQ(tracker.getAgent(2)->state, AgentState::Starting);
}

TEST(AgentTracker, GetAgentReturnsNullForUnknownPane) {
    AgentTracker tracker;
    EXPECT_EQ(tracker.getAgent(42), nullptr);
}

TEST(AgentTracker, DetectSubstringMatch) {
    AgentTracker tracker;
    // Process name containing the pattern as a substring
    EXPECT_EQ(tracker.detectAgent("/usr/bin/claude"), AgentType::ClaudeCode);
    EXPECT_EQ(tracker.detectAgent("python3 -m aider"), AgentType::Aider);
}

TEST(AgentTracker, DetectAllDefaultAgents) {
    AgentTracker tracker;
    EXPECT_EQ(tracker.detectAgent("claude"), AgentType::ClaudeCode);
    EXPECT_EQ(tracker.detectAgent("codex"), AgentType::Codex);
    EXPECT_EQ(tracker.detectAgent("gemini"), AgentType::GeminiCli);
    EXPECT_EQ(tracker.detectAgent("aider"), AgentType::Aider);
    EXPECT_EQ(tracker.detectAgent("opencode"), AgentType::OpenCode);
    EXPECT_EQ(tracker.detectAgent("goose"), AgentType::Goose);
    EXPECT_EQ(tracker.detectAgent("amp"), AgentType::Amp);
    EXPECT_EQ(tracker.detectAgent("cline"), AgentType::Cline);
}

// ==========================================================================
// State patterns loaded from ProviderRegistry
// ==========================================================================

TEST(AgentTracker, LoadStatePatternsFromRegistry) {
    ProviderRegistry registry;
    ProviderInfo pi;
    pi.id = "claude_code";
    pi.agent_type = "ClaudeCode";
    pi.state_patterns = {
        {"thinking", "Thinking..."},
        {"tool_use", "Tool:"},
        {"tool_use", "Running"},
        {"waiting", "Allow?"},
        {"error", "Error:"},
        {"idle", "> "},
    };
    registry.registerProvider(std::move(pi));

    AgentTracker tracker;
    EXPECT_EQ(tracker.statePatterns().size(), 0u);  // empty before load

    tracker.loadStatePatternsFrom(registry);
    EXPECT_EQ(tracker.statePatterns().size(), 6u);
}

TEST(AgentTracker, LoadStatePatternsFromMultipleProviders) {
    ProviderRegistry registry;

    ProviderInfo claude;
    claude.id = "claude_code";
    claude.agent_type = "ClaudeCode";
    claude.state_patterns = {{"thinking", "Thinking..."}};
    registry.registerProvider(std::move(claude));

    ProviderInfo aider;
    aider.id = "aider";
    aider.agent_type = "Aider";
    aider.state_patterns = {{"thinking", "Thinking..."}, {"error", "Error"}};
    registry.registerProvider(std::move(aider));

    AgentTracker tracker;
    tracker.loadStatePatternsFrom(registry);
    EXPECT_EQ(tracker.statePatterns().size(), 3u);
}

TEST(AgentTracker, LoadStatePatternsGenericUnknownType) {
    ProviderRegistry registry;
    ProviderInfo generic;
    generic.id = "_generic";
    generic.agent_type = "Unknown";
    generic.state_patterns = {{"error", "FATAL"}};
    registry.registerProvider(std::move(generic));

    AgentTracker tracker;
    tracker.loadStatePatternsFrom(registry);
    EXPECT_EQ(tracker.statePatterns().size(), 1u);

    // Generic pattern (Unknown type) should match any agent
    tracker.reportStart(1, AgentType::ClaudeCode, 100);
    tracker.reportState(1, AgentType::ClaudeCode, AgentState::Idle);
    bool changed = tracker.evaluateOutput(1, "FATAL crash");
    EXPECT_TRUE(changed);
    EXPECT_EQ(tracker.getAgent(1)->state, AgentState::Error);
}

TEST(AgentTracker, EvaluateOutputAfterLoadFromRegistry) {
    ProviderRegistry registry;
    ProviderInfo pi;
    pi.id = "claude_code";
    pi.agent_type = "ClaudeCode";
    pi.state_patterns = {
        {"thinking", "Thinking..."},
        {"tool_use", "Tool:"},
        {"error", "Error:"},
    };
    registry.registerProvider(std::move(pi));

    AgentTracker tracker;
    tracker.loadStatePatternsFrom(registry);
    tracker.reportStart(1, AgentType::ClaudeCode, 100);
    tracker.reportState(1, AgentType::ClaudeCode, AgentState::Idle);

    EXPECT_TRUE(tracker.evaluateOutput(1, "Thinking..."));
    EXPECT_EQ(tracker.getAgent(1)->state, AgentState::Thinking);

    EXPECT_TRUE(tracker.evaluateOutput(1, "Tool: read file"));
    EXPECT_EQ(tracker.getAgent(1)->state, AgentState::ToolUse);

    EXPECT_TRUE(tracker.evaluateOutput(1, "Error: crash"));
    EXPECT_EQ(tracker.getAgent(1)->state, AgentState::Error);
}

// ==========================================================================
// Configurable stale timeout
// ==========================================================================

TEST(AgentTracker, DefaultStaleTimeout) {
    AgentTracker tracker;
    EXPECT_EQ(tracker.staleTimeout(), 60);
}

TEST(AgentTracker, SetStaleTimeout) {
    AgentTracker tracker;
    tracker.setStaleTimeout(120);
    EXPECT_EQ(tracker.staleTimeout(), 120);
}

TEST(AgentTracker, StaleTimeoutFromRegistry) {
    ProviderRegistry registry;
    registry.setStaleTimeout(180);
    EXPECT_EQ(registry.staleTimeout(), 180);

    AgentTracker tracker;
    tracker.loadStatePatternsFrom(registry);
    EXPECT_EQ(tracker.staleTimeout(), 180);
}

TEST(ProviderRegistryTest, StaleTimeoutDefault) {
    ProviderRegistry registry;
    EXPECT_EQ(registry.staleTimeout(), 60);
}

TEST(ProviderRegistryTest, SetStaleTimeout) {
    ProviderRegistry registry;
    registry.setStaleTimeout(90);
    EXPECT_EQ(registry.staleTimeout(), 90);
}

TEST(ProviderRegistryTest, StatePatternStorage) {
    ProviderRegistry registry;
    ProviderInfo info;
    info.id = "test_provider";
    info.agent_type = "Custom";
    info.state_patterns = {
        {"thinking", "processing..."},
        {"error", "failed!"},
    };
    registry.registerProvider(std::move(info));

    const auto* found = registry.findById("test_provider");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->state_patterns.size(), 2u);
    EXPECT_EQ(found->state_patterns[0].state, "thinking");
    EXPECT_EQ(found->state_patterns[0].pattern, "processing...");
    EXPECT_EQ(found->state_patterns[1].state, "error");
    EXPECT_EQ(found->state_patterns[1].pattern, "failed!");
}
