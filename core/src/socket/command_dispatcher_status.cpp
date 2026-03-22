#include "termcore/socket/command_dispatcher.h"

#include <algorithm>

namespace termcore {

// ---------------------------------------------------------------------------
// agent.setStatus — set agent state with custom label and icon
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleAgentSetStatus(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("pane_id") || !p["pane_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "pane_id required");
    }
    auto pane_id = p["pane_id"].get<PaneId>();

    // Update agent state if provided
    if (p.contains("state") && p["state"].is_string()) {
        auto state_str = p["state"].get<std::string>();
        auto state = AgentTracker::stringToState(state_str);
        auto label = p.value("label", std::string{});
        auto icon = p.value("icon", std::string{});

        // Get existing agent info to determine type
        const auto* existing = agent_tracker_.getAgent(pane_id);
        AgentType type = existing ? existing->type : AgentType::Unknown;

        agent_tracker_.reportState(pane_id, type, state, label);

        // Update custom fields
        // We need to const_cast here because reportState doesn't set custom fields
        auto* info = const_cast<AgentInfo*>(agent_tracker_.getAgent(pane_id));
        if (info) {
            if (!label.empty()) info->custom_label = label;
            if (!icon.empty()) info->custom_icon = icon;
        }
    }

    return rpc::makeResult(id, {{"success", true}});
}

// ---------------------------------------------------------------------------
// agent.setProgress — set progress bar (wraps setPaneProgress)
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleAgentSetProgress(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("pane_id") || !p["pane_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "pane_id required");
    }
    auto pane_id = p["pane_id"].get<PaneId>();

    float progress = -1.0f;
    if (p.contains("progress") && p["progress"].is_number()) {
        progress = p["progress"].get<float>();
        progress = std::clamp(progress, -1.0f, 1.0f);
    }

    auto label = p.value("label", std::string{});
    // color field is stored but not used in the progress metadata currently
    // auto color = p.value("color", std::string{});

    {
        std::lock_guard<std::mutex> lock(agent_meta_mutex_);
        if (progress < 0.0f) {
            pane_progress_.erase(pane_id);
        } else {
            pane_progress_[pane_id] = PaneProgress{progress, label};
        }
    }

    return rpc::makeResult(id, {{"success", true}});
}

// ---------------------------------------------------------------------------
// agent.setStatusPills — set multiple status pills
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleAgentSetStatusPills(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("pane_id") || !p["pane_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "pane_id required");
    }
    if (!p.contains("pills") || !p["pills"].is_array()) {
        return rpc::makeError(id, rpc::kInvalidParams, "pills array required");
    }

    auto pane_id = p["pane_id"].get<PaneId>();

    std::vector<PaneStatus> pills;
    for (const auto& pill : p["pills"]) {
        PaneStatus ps;
        ps.key = pill.value("text", std::string{});
        ps.value = pill.value("bg_color", std::string{});
        ps.icon = pill.value("fg_color", std::string{});
        if (!ps.key.empty()) {
            pills.push_back(std::move(ps));
        }
    }

    {
        std::lock_guard<std::mutex> lock(agent_meta_mutex_);
        pane_statuses_[pane_id] = std::move(pills);
    }

    return rpc::makeResult(id, {{"success", true}});
}

// ---------------------------------------------------------------------------
// agent.requestAttention — trigger border glow and notification
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleAgentRequestAttention(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("pane_id") || !p["pane_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "pane_id required");
    }
    auto pane_id = p["pane_id"].get<PaneId>();

    auto message = p.value("message", std::string{"Agent needs attention"});
    auto urgency_str = p.value("urgency", std::string{"normal"});

    // Parse color (default blue accent)
    uint32_t color = 0x007acc;
    if (p.contains("color") && p["color"].is_string()) {
        auto color_str = p["color"].get<std::string>();
        if (!color_str.empty() && color_str[0] == '#' && color_str.size() == 7) {
            try {
                color = static_cast<uint32_t>(std::stoul(color_str.substr(1), nullptr, 16));
            } catch (...) {
                // Keep default color
            }
        }
    }

    // Determine urgency
    NotificationUrgency urgency = NotificationUrgency::Normal;
    float intensity = 0.7f;
    if (urgency_str == "low") {
        urgency = NotificationUrgency::Low;
        intensity = 0.3f;
    } else if (urgency_str == "critical") {
        urgency = NotificationUrgency::Critical;
        intensity = 1.0f;
    }

    // Create a notification in the store
    const auto* agent_info = agent_tracker_.getAgent(pane_id);
    std::string title = agent_info ? agent_info->name : "Agent";
    notifications_.add(pane_id, NotificationSource::Agent, urgency, title, message);

    // Trigger visual border glow via attention callback
    if (attention_cb_) {
        attention_cb_(pane_id, intensity, color);
    }

    return rpc::makeResult(id, {{"success", true}});
}

// ---------------------------------------------------------------------------
// agent.clearStatus — reset all agent UI elements for a pane
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleAgentClearStatus(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("pane_id") || !p["pane_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "pane_id required");
    }
    auto pane_id = p["pane_id"].get<PaneId>();

    {
        std::lock_guard<std::mutex> lock(agent_meta_mutex_);
        pane_statuses_.erase(pane_id);
        pane_progress_.erase(pane_id);
    }

    return rpc::makeResult(id, {{"success", true}});
}

// ---------------------------------------------------------------------------
// agent.addStatePattern — add custom pattern-based state detection
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleAgentAddStatePattern(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("pattern") || !p["pattern"].is_string()) {
        return rpc::makeError(id, rpc::kInvalidParams, "pattern required");
    }
    if (!p.contains("target_state") || !p["target_state"].is_string()) {
        return rpc::makeError(id, rpc::kInvalidParams, "target_state required");
    }

    AgentStatePattern sp;
    sp.pattern = p["pattern"].get<std::string>();
    sp.target_state = AgentTracker::stringToState(
        p["target_state"].get<std::string>());
    sp.is_regex = p.value("is_regex", false);

    // Optional: agent_type filter
    if (p.contains("agent_type") && p["agent_type"].is_number()) {
        sp.agent_type = static_cast<AgentType>(p["agent_type"].get<int>());
    }

    agent_tracker_.addStatePattern(sp);

    return rpc::makeResult(id, {
        {"success", true},
        {"pattern_count", static_cast<int>(agent_tracker_.statePatterns().size())}
    });
}

// ---------------------------------------------------------------------------
// workspace.listPanes — list all panes with their agent status
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleWorkspaceListPanes(
    std::optional<int64_t> id, const nlohmann::json& /*p*/) {
    nlohmann::json panes_arr = nlohmann::json::array();

    auto ws_ids = mux_.allWorkspaceIds();
    for (auto ws_id : ws_ids) {
        auto tab_ids = mux_.allTabIds(ws_id);
        for (auto tab_id : tab_ids) {
            auto pane_ids = mux_.allPanes(ws_id, tab_id);
            auto active_pane = mux_.activePaneId(ws_id, tab_id);

            for (auto pid : pane_ids) {
                nlohmann::json pane_obj = {
                    {"pane_id", pid},
                    {"workspace_id", ws_id},
                    {"tab_id", tab_id},
                    {"is_active", pid == active_pane}
                };

                // Attach agent info if available
                const auto* agent = agent_tracker_.getAgent(pid);
                if (agent) {
                    pane_obj["agent"] = {
                        {"type", static_cast<int>(agent->type)},
                        {"state", AgentTracker::stateToString(agent->state)},
                        {"name", agent->name}
                    };
                }

                panes_arr.push_back(std::move(pane_obj));
            }
        }
    }

    return rpc::makeResult(id, {{"panes", panes_arr}});
}

// ---------------------------------------------------------------------------
// workspace.getActivePane — get the active pane ID
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleWorkspaceGetActivePane(
    std::optional<int64_t> id, const nlohmann::json& /*p*/) {
    auto ws_id = mux_.activeWorkspaceId();
    if (ws_id == kInvalidWorkspace) {
        return rpc::makeError(id, rpc::kNotFound, "No active workspace");
    }

    auto* tab = mux_.activeTab(ws_id);
    if (!tab) {
        return rpc::makeError(id, rpc::kNotFound, "No active tab");
    }

    auto pane_id = mux_.activePaneId(ws_id, tab->id);

    nlohmann::json result = {
        {"pane_id", pane_id},
        {"workspace_id", ws_id},
        {"tab_id", tab->id}
    };

    // Attach agent info if available
    const auto* agent = agent_tracker_.getAgent(pane_id);
    if (agent) {
        result["agent"] = {
            {"type", static_cast<int>(agent->type)},
            {"state", AgentTracker::stateToString(agent->state)},
            {"name", agent->name}
        };
    }

    return rpc::makeResult(id, result);
}

// ---------------------------------------------------------------------------
// workspace.getPaneInfo — get pane details
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleWorkspaceGetPaneInfo(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("pane_id") || !p["pane_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "pane_id required");
    }
    auto pane_id = p["pane_id"].get<PaneId>();

    // Find which workspace/tab this pane belongs to
    WorkspaceId found_ws = kInvalidWorkspace;
    TabId found_tab = kInvalidTab;
    bool is_active = false;

    auto ws_ids = mux_.allWorkspaceIds();
    for (auto ws_id : ws_ids) {
        auto tab_ids = mux_.allTabIds(ws_id);
        for (auto tab_id : tab_ids) {
            auto pane_ids = mux_.allPanes(ws_id, tab_id);
            for (auto pid : pane_ids) {
                if (pid == pane_id) {
                    found_ws = ws_id;
                    found_tab = tab_id;
                    is_active = (pid == mux_.activePaneId(ws_id, tab_id));
                    break;
                }
            }
            if (found_ws != kInvalidWorkspace) break;
        }
        if (found_ws != kInvalidWorkspace) break;
    }

    if (found_ws == kInvalidWorkspace) {
        return rpc::makeError(id, rpc::kNotFound, "Pane not found");
    }

    nlohmann::json result = {
        {"pane_id", pane_id},
        {"workspace_id", found_ws},
        {"tab_id", found_tab},
        {"is_active", is_active}
    };

    // Add workspace name if available
    auto* ws = mux_.getWorkspace(found_ws);
    if (ws) {
        result["workspace_name"] = ws->name;
    }

    // Attach agent info if available
    const auto* agent = agent_tracker_.getAgent(pane_id);
    if (agent) {
        result["agent"] = {
            {"type", static_cast<int>(agent->type)},
            {"state", AgentTracker::stateToString(agent->state)},
            {"name", agent->name},
            {"pid", agent->pid},
            {"last_message", agent->last_message}
        };
    }

    // Attach status pills and progress if available
    {
        std::lock_guard<std::mutex> lock(agent_meta_mutex_);

        auto status_it = pane_statuses_.find(pane_id);
        if (status_it != pane_statuses_.end()) {
            nlohmann::json pills = nlohmann::json::array();
            for (const auto& ps : status_it->second) {
                pills.push_back({{"key", ps.key}, {"value", ps.value}, {"icon", ps.icon}});
            }
            result["status_pills"] = pills;
        }

        auto progress_it = pane_progress_.find(pane_id);
        if (progress_it != pane_progress_.end()) {
            result["progress"] = {
                {"value", progress_it->second.value},
                {"label", progress_it->second.label}
            };
        }
    }

    return rpc::makeResult(id, result);
}

// ---------------------------------------------------------------------------
// agent.broadcast — send message to all agent panes
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleAgentBroadcast(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("message") || !p["message"].is_string()) {
        return rpc::makeError(id, rpc::kInvalidParams, "message required");
    }

    auto message = p["message"].get<std::string>();

    // Wire up send callback if needed
    if (write_cb_) {
        orchestrator_.setSendTextCallback(
            [this](PaneId pane_id, const std::string& text) {
                write_cb_(pane_id, text);
            });
    }

    orchestrator_.broadcastToAgents(message);

    return rpc::makeResult(id, {{"success", true}});
}

// ---------------------------------------------------------------------------
// agent.sendToAgent — send message to a specific agent pane
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleAgentSendToAgent(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("pane_id") || !p["pane_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "pane_id required");
    }
    if (!p.contains("message") || !p["message"].is_string()) {
        return rpc::makeError(id, rpc::kInvalidParams, "message required");
    }

    auto pane_id = p["pane_id"].get<PaneId>();
    auto message = p["message"].get<std::string>();

    // Wire up send callback if needed
    if (write_cb_) {
        orchestrator_.setSendTextCallback(
            [this](PaneId pid, const std::string& text) {
                write_cb_(pid, text);
            });
    }

    if (!orchestrator_.isTracked(pane_id)) {
        // Fall back to direct write for non-orchestrated panes
        if (write_cb_) {
            write_cb_(pane_id, message);
            return rpc::makeResult(id, {{"success", true}});
        }
        return rpc::makeError(id, rpc::kNotFound, "Pane not tracked");
    }

    orchestrator_.sendToAgent(pane_id, message);
    return rpc::makeResult(id, {{"success", true}});
}

// ---------------------------------------------------------------------------
// agent.listAgents — list all active agents with their states
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleAgentListAgents(
    std::optional<int64_t> id, const nlohmann::json& /*p*/) {
    auto agents = agent_tracker_.allAgents();

    nlohmann::json arr = nlohmann::json::array();
    for (const auto* info : agents) {
        nlohmann::json agent_obj = {
            {"pane_id", info->pane_id},
            {"type", static_cast<int>(info->type)},
            {"state", AgentTracker::stateToString(info->state)},
            {"name", info->name},
            {"pid", info->pid},
            {"last_message", info->last_message}
        };

        if (!info->custom_label.empty()) {
            agent_obj["custom_label"] = info->custom_label;
        }
        if (!info->custom_icon.empty()) {
            agent_obj["custom_icon"] = info->custom_icon;
        }

        arr.push_back(std::move(agent_obj));
    }

    return rpc::makeResult(id, {
        {"agents", arr},
        {"count", static_cast<int>(agents.size())}
    });
}

}  // namespace termcore
