#pragma once

#include "termcore/agent.h"
#include "termcore/mux.h"

#include <chrono>
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

    const std::vector<SubagentNode>& rootAgents(PaneId pane) const;
    const SubagentNode* findAgent(const std::string& agent_id) const;
    size_t activeCount(PaneId pane) const;

    void sweepCompleted(std::chrono::seconds max_age = std::chrono::seconds(300));
    void clearForPane(PaneId pane);

private:
    SubagentNode* findNode(std::vector<SubagentNode>& nodes, const std::string& id);
    const SubagentNode* findNodeConst(const std::vector<SubagentNode>& nodes,
                                       const std::string& id) const;
    size_t countActive(const std::vector<SubagentNode>& nodes) const;
    void sweepNodes(std::vector<SubagentNode>& nodes,
                    std::chrono::steady_clock::time_point cutoff);

    std::unordered_map<PaneId, std::vector<SubagentNode>> pane_trees_;
    static const std::vector<SubagentNode> empty_vec_;
};

} // namespace termcore
