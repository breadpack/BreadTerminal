#include "termcore/agent_tree_tracker.h"

#include <algorithm>

namespace termcore {

void AgentTreeTracker::onAgentStart(PaneId pane, const std::string& agent_id,
                                     const std::string& type, const std::string& desc,
                                     const std::string& parent_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    SubagentNode node;
    node.agent_id = agent_id;
    node.agent_type = type;
    node.description = desc;
    node.state = AgentState::Starting;
    node.started = std::chrono::steady_clock::now();

    auto& roots = pane_trees_[pane];

    if (!parent_id.empty()) {
        SubagentNode* parent = findNode(roots, parent_id);
        if (parent) {
            parent->children.push_back(std::move(node));
            ++generation_;
            return;
        }
    }

    // No parent_id or parent not found: add as root
    roots.push_back(std::move(node));
    ++generation_;
}

void AgentTreeTracker::onAgentStop(PaneId pane, const std::string& agent_id,
                                    AgentState final_state) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = pane_trees_.find(pane);
    if (it == pane_trees_.end()) return;

    SubagentNode* node = findNode(it->second, agent_id);
    if (node) {
        node->state = final_state;
        node->ended = std::chrono::steady_clock::now();
        ++generation_;
    }
}

void AgentTreeTracker::onAgentStateChange(PaneId pane, const std::string& agent_id,
                                           AgentState state) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = pane_trees_.find(pane);
    if (it == pane_trees_.end()) return;

    SubagentNode* node = findNode(it->second, agent_id);
    if (node) {
        node->state = state;
        ++generation_;
    }
}

std::vector<SubagentNode> AgentTreeTracker::rootAgents(PaneId pane) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = pane_trees_.find(pane);
    if (it == pane_trees_.end()) return {};
    return it->second;  // return by value (copy)
}

std::optional<SubagentNode> AgentTreeTracker::findAgent(const std::string& agent_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& [pane, roots] : pane_trees_) {
        const SubagentNode* found = findNodeConst(roots, agent_id);
        if (found) return *found;  // return copy — safe after lock release
    }
    return std::nullopt;
}

size_t AgentTreeTracker::activeCount(PaneId pane) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = pane_trees_.find(pane);
    if (it == pane_trees_.end()) return 0;
    return countActive(it->second);
}

void AgentTreeTracker::sweepCompleted(std::chrono::seconds max_age) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto cutoff = std::chrono::steady_clock::now() - max_age;
    for (auto& [pane, roots] : pane_trees_) {
        sweepNodes(roots, cutoff);
    }
    ++generation_;
}

void AgentTreeTracker::clearForPane(PaneId pane) {
    std::lock_guard<std::mutex> lock(mutex_);

    pane_trees_.erase(pane);
    ++generation_;
}

uint64_t AgentTreeTracker::generation() const {
    return generation_.load(std::memory_order_acquire);
}

SubagentNode* AgentTreeTracker::findNode(std::vector<SubagentNode>& nodes,
                                          const std::string& id) {
    for (auto& node : nodes) {
        if (node.agent_id == id) return &node;
        SubagentNode* child = findNode(node.children, id);
        if (child) return child;
    }
    return nullptr;
}

const SubagentNode* AgentTreeTracker::findNodeConst(const std::vector<SubagentNode>& nodes,
                                                     const std::string& id) const {
    for (const auto& node : nodes) {
        if (node.agent_id == id) return &node;
        const SubagentNode* child = findNodeConst(node.children, id);
        if (child) return child;
    }
    return nullptr;
}

size_t AgentTreeTracker::countActive(const std::vector<SubagentNode>& nodes) const {
    size_t count = 0;
    for (const auto& node : nodes) {
        if (node.state != AgentState::Exited && node.state != AgentState::Inactive) {
            ++count;
        }
        count += countActive(node.children);
    }
    return count;
}

void AgentTreeTracker::sweepNodes(std::vector<SubagentNode>& nodes,
                                   std::chrono::steady_clock::time_point cutoff) {
    // Sweep children first (depth-first)
    for (auto& node : nodes) {
        sweepNodes(node.children, cutoff);
    }

    // Remove exited leaf nodes older than cutoff
    nodes.erase(
        std::remove_if(nodes.begin(), nodes.end(),
                        [&cutoff](const SubagentNode& node) {
                            return node.state == AgentState::Exited &&
                                   node.children.empty() &&
                                   node.ended <= cutoff;
                        }),
        nodes.end());
}

} // namespace termcore
