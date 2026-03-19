#include "termcore/socket/command_dispatcher.h"

namespace termcore {

rpc::Response CommandDispatcher::handleAgentLaunch(
    std::optional<int64_t> id, const nlohmann::json& p) {

    // Parse config
    AgentLaunchConfig config;
    if (p.contains("cli") && p["cli"].is_string()) {
        config.cli_command = p["cli"].get<std::string>();
    }
    config.count = p.value("count", 1);
    if (config.count <= 0 || config.count > 100) {
        return rpc::makeError(id, rpc::kInvalidParams,
                              "count must be between 1 and 100");
    }
    config.layout = p.value("layout", std::string("grid"));

    int rows = p.value("rows", 24);
    int cols = p.value("cols", 80);

    // Wire up the send callback from the existing write_cb_
    if (write_cb_) {
        orchestrator_.setSendTextCallback(
            [this](PaneId pane_id, const std::string& text) {
                write_cb_(pane_id, text);
            });
    }

    // Wire up read callback if available
    if (read_cb_) {
        orchestrator_.setReadScreenCallback(
            [this](PaneId pane_id, int lines) -> std::string {
                auto result = read_cb_(pane_id, lines, false);
                std::string output;
                for (const auto& line : result) {
                    if (!output.empty()) output += '\n';
                    output += line;
                }
                return output;
            });
    }

    auto pane_ids = orchestrator_.launchAgents(config, rows, cols);
    if (pane_ids.empty()) {
        return rpc::makeError(id, rpc::kInternalError,
                              "Failed to launch agents");
    }

    nlohmann::json pane_arr = nlohmann::json::array();
    for (auto pid : pane_ids) {
        pane_arr.push_back(pid);
    }

    return rpc::makeResult(id, {
        {"workspace_id", orchestrator_.lastWorkspaceId()},
        {"pane_ids", pane_arr},
        {"count", static_cast<int>(pane_ids.size())}
    });
}

rpc::Response CommandDispatcher::handleAgentOrchestrate(
    std::optional<int64_t> id, const nlohmann::json& p) {

    if (!p.contains("assignments") || !p["assignments"].is_array()) {
        return rpc::makeError(id, rpc::kInvalidParams,
                              "assignments array required");
    }

    std::vector<AgentAssignment> assignments;
    for (const auto& item : p["assignments"]) {
        AgentAssignment a;
        if (!item.contains("pane_id") || !item["pane_id"].is_number()) {
            return rpc::makeError(id, rpc::kInvalidParams,
                                  "Each assignment requires pane_id");
        }
        a.pane_id = item["pane_id"].get<PaneId>();
        a.command = item.value("command", std::string{});
        assignments.push_back(std::move(a));
    }

    orchestrator_.orchestrate(assignments);

    return rpc::makeResult(id, {
        {"success", true},
        {"count", static_cast<int>(assignments.size())}
    });
}

rpc::Response CommandDispatcher::handleAgentReadAll(
    std::optional<int64_t> id, const nlohmann::json& /*p*/) {

    auto statuses = orchestrator_.readAllStatus();

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : statuses) {
        auto stateStr = [](AgentState st) -> std::string {
            switch (st) {
                case AgentState::Inactive:   return "inactive";
                case AgentState::Starting:   return "starting";
                case AgentState::Idle:       return "idle";
                case AgentState::Running:    return "running";
                case AgentState::NeedsInput: return "needs_input";
                case AgentState::Exited:     return "exited";
            }
            return "unknown";
        };

        arr.push_back({
            {"pane_id", s.pane_id},
            {"last_output", s.last_output},
            {"is_idle", s.is_idle},
            {"agent_state", stateStr(s.agent_state)}
        });
    }

    return rpc::makeResult(id, {{"panes", arr}});
}

rpc::Response CommandDispatcher::handleAgentGetIdle(
    std::optional<int64_t> id, const nlohmann::json& p) {

    float threshold = p.value("threshold_seconds", 5.0f);
    auto idle_panes = orchestrator_.getIdlePanes(threshold);

    nlohmann::json arr = nlohmann::json::array();
    for (auto pid : idle_panes) {
        arr.push_back(pid);
    }

    return rpc::makeResult(id, {{"idle_panes", arr}});
}

rpc::Response CommandDispatcher::handleAgentCloseAll(
    std::optional<int64_t> id, const nlohmann::json& p) {

    WorkspaceId ws_id = kInvalidWorkspace;
    if (p.contains("workspace_id") && p["workspace_id"].is_number()) {
        ws_id = p["workspace_id"].get<WorkspaceId>();
    } else {
        // Default: close the last launched workspace
        ws_id = orchestrator_.lastWorkspaceId();
    }

    if (ws_id == kInvalidWorkspace) {
        return rpc::makeError(id, rpc::kNotFound,
                              "No agent workspace to close");
    }

    orchestrator_.closeAll(ws_id);

    return rpc::makeResult(id, {{"success", true}});
}

}  // namespace termcore
