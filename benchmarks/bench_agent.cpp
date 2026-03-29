#include "bench_agent.h"
#include "termcore/agent.h"
#include "termcore/agent_tree_tracker.h"
#include "termcore/hook_bridge.h"
#include "termcore/notification.h"

#include <nlohmann/json.hpp>
#include <string>

namespace bench {

void runAgentBenchmarks(BenchmarkRunner& runner) {
    // --- agent_state_transition ---
    // reportState() throughput with 100 agents (ops/sec)
    {
        runner.run("agent_state_transition", "ops/sec", []() -> double {
            termcore::AgentTracker tracker;

            // Create 100 agents
            constexpr int num_agents = 100;
            for (int i = 0; i < num_agents; ++i) {
                tracker.reportStart(static_cast<uint32_t>(i),
                                    termcore::AgentType::ClaudeCode, 1000 + i);
            }

            // Measure state transitions
            constexpr int transitions = 10000;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < transitions; ++i) {
                uint32_t pane = static_cast<uint32_t>(i % num_agents);
                auto state = static_cast<termcore::AgentState>(
                    2 + (i % 6)); // Cycle through Idle..Error
                tracker.reportState(pane, termcore::AgentType::ClaudeCode,
                                    state, "message");
            }
            double sec = t.elapsedSec();
            return transitions / sec;
        });
    }

    // --- agent_pattern_matching ---
    // evaluateOutput() with 50 patterns against 1KB output (ops/sec)
    {
        runner.run("agent_pattern_matching", "ops/sec", []() -> double {
            termcore::AgentTracker tracker;

            // Register additional patterns to get ~50 total
            for (int i = 0; i < 42; ++i) {
                termcore::AgentStatePattern pat;
                pat.agent_type = termcore::AgentType::Unknown; // matches all
                pat.target_state = termcore::AgentState::Running;
                pat.pattern = "pattern_match_" + std::to_string(i);
                pat.is_regex = false;
                tracker.addStatePattern(pat);
            }

            // Create an agent in pane 0 so evaluateOutput has something to check
            tracker.reportStart(0, termcore::AgentType::ClaudeCode, 9999);

            // Build 1KB output string that does NOT match any pattern
            // (worst case: must check all patterns)
            std::string output(1024, 'x');

            constexpr int evaluations = 5000;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < evaluations; ++i) {
                tracker.evaluateOutput(0, output);
            }
            double sec = t.elapsedSec();
            return evaluations / sec;
        });
    }

    // --- agent_tree_traversal ---
    // findAgent() in tree with 1000 nodes (ops/sec)
    {
        runner.run("agent_tree_traversal", "ops/sec", []() -> double {
            termcore::AgentTreeTracker tree;

            // Build a tree with 1000 nodes across 10 panes, with hierarchy
            constexpr int total_nodes = 1000;
            constexpr int panes = 10;
            constexpr int roots_per_pane = 10;
            constexpr int children_per_root = (total_nodes / panes / roots_per_pane) - 1;

            for (int p = 0; p < panes; ++p) {
                for (int r = 0; r < roots_per_pane; ++r) {
                    std::string root_id = "p" + std::to_string(p) +
                                          "_r" + std::to_string(r);
                    tree.onAgentStart(static_cast<uint32_t>(p), root_id,
                                      "claude", "root task");
                    for (int c = 0; c < children_per_root; ++c) {
                        std::string child_id = root_id + "_c" + std::to_string(c);
                        tree.onAgentStart(static_cast<uint32_t>(p), child_id,
                                          "explore", "child task", root_id);
                    }
                }
            }

            // Search for agents spread across the tree (including last node = worst case)
            std::vector<std::string> search_ids = {
                "p0_r0",              // first root
                "p5_r5_c4",           // middle child
                "p9_r9_c" + std::to_string(children_per_root - 1), // last child
                "nonexistent_agent",  // miss
            };

            constexpr int lookups = 10000;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < lookups; ++i) {
                const auto& id = search_ids[i % search_ids.size()];
                auto result = tree.findAgent(id);
                (void)result; // prevent optimization
            }
            double sec = t.elapsedSec();
            return lookups / sec;
        });
    }

    // --- hook_event_processing ---
    // processHookEvent() JSON parse + state update throughput (ops/sec)
    {
        runner.run("hook_event_processing", "ops/sec", []() -> double {
            termcore::AgentTreeTracker tree;
            termcore::AgentTracker tracker;
            termcore::NotificationStore notifications;
            termcore::HookBridge bridge(tree, tracker, notifications);

            // Pre-create agents that we will update
            constexpr int num_agents = 50;
            for (int i = 0; i < num_agents; ++i) {
                nlohmann::json start = {
                    {"event", "SubagentStart"},
                    {"agent_id", "agent-" + std::to_string(i)},
                    {"agent_type", "claude"},
                    {"description", "task " + std::to_string(i)},
                    {"pane_id", static_cast<uint32_t>(i % 10)}
                };
                bridge.processHookEvent(start);
            }

            // Build a set of pre-serialized JSON strings to parse + process
            std::vector<std::string> json_strings;
            json_strings.reserve(num_agents * 3);
            for (int i = 0; i < num_agents; ++i) {
                // StateChange events
                nlohmann::json ev = {
                    {"event", "StateChange"},
                    {"agent_id", "agent-" + std::to_string(i)},
                    {"pane_id", static_cast<uint32_t>(i % 10)},
                    {"state", "running"}
                };
                json_strings.push_back(ev.dump());
            }
            for (int i = 0; i < num_agents; ++i) {
                // Notification events
                nlohmann::json ev = {
                    {"event", "Notification"},
                    {"pane_id", static_cast<uint32_t>(i % 10)},
                    {"title", "Title " + std::to_string(i)},
                    {"body", "Body text for notification"},
                    {"urgency", "normal"}
                };
                json_strings.push_back(ev.dump());
            }

            constexpr int iterations = 5000;
            BenchmarkTimer t;
            t.start();
            for (int i = 0; i < iterations; ++i) {
                const auto& raw = json_strings[i % json_strings.size()];
                auto parsed = nlohmann::json::parse(raw, nullptr, false);
                if (!parsed.is_discarded()) {
                    bridge.processHookEvent(parsed);
                }
            }
            double sec = t.elapsedSec();
            return iterations / sec;
        });
    }
}

} // namespace bench
