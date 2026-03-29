#pragma once

#include "termcore/agent.h"
#include "termcore/mux.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace termcore {

struct SubagentNode {
    std::string agent_id;
    std::string agent_type;
    std::string description;
    AgentState state = AgentState::Inactive;
    std::chrono::steady_clock::time_point started;
    std::chrono::steady_clock::time_point ended;
    std::vector<SubagentNode> children;
};

class AgentTreeTracker {
public:
    AgentTreeTracker() = default;

    void onAgentStart(PaneId pane, const std::string& agent_id,
                      const std::string& type, const std::string& desc,
                      const std::string& parent_id = "");
    void onAgentStop(PaneId pane, const std::string& agent_id,
                     AgentState final_state);
    void onAgentStateChange(PaneId pane, const std::string& agent_id,
                            AgentState state);

    std::vector<SubagentNode> rootAgents(PaneId pane) const;
    std::optional<SubagentNode> findAgent(const std::string& agent_id) const;
    size_t activeCount(PaneId pane) const;

    void sweepCompleted(std::chrono::seconds max_age = std::chrono::seconds(300));
    void clearForPane(PaneId pane);

    /// Generation counter: incremented on every mutation.
    /// Callers can use this for dirty-checking (e.g., skip sidebar rebuild if unchanged).
    uint64_t generation() const;

private:
    SubagentNode* findNode(std::vector<SubagentNode>& nodes, const std::string& id);
    const SubagentNode* findNodeConst(const std::vector<SubagentNode>& nodes,
                                       const std::string& id) const;
    size_t countActive(const std::vector<SubagentNode>& nodes) const;
    void sweepNodes(std::vector<SubagentNode>& nodes,
                    std::chrono::steady_clock::time_point cutoff);

    mutable std::mutex mutex_;
    std::unordered_map<PaneId, std::vector<SubagentNode>> pane_trees_;
    std::atomic<uint64_t> generation_{0};
};

} // namespace termcore
