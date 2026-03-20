#include "termcore/socket/command_dispatcher.h"

namespace termcore {

CommandDispatcher::CommandDispatcher(Mux& mux,
                                     NotificationStore& notifications,
                                     AgentTracker& agent_tracker,
                                     PaneWriteCallback write_cb,
                                     WebViewCallback webview_cb,
                                     ScrollbackReadCallback scrollback_cb)
    : mux_(mux)
    , notifications_(notifications)
    , agent_tracker_(agent_tracker)
    , write_cb_(std::move(write_cb))
    , webview_cb_(std::move(webview_cb))
    , scrollback_cb_(std::move(scrollback_cb))
    , orchestrator_(mux)
{
}

rpc::Response CommandDispatcher::dispatch(const rpc::Request& req) {
    const auto& method = req.method;
    auto id = req.id;
    const auto& p = req.params;

    // workspace.*
    if (method == "workspace.create") return handleWorkspaceCreate(id, p);
    if (method == "workspace.list")   return handleWorkspaceList(id, p);
    if (method == "workspace.switch") return handleWorkspaceSwitch(id, p);
    if (method == "workspace.destroy") return handleWorkspaceDestroy(id, p);

    // tab.*
    if (method == "tab.create") return handleTabCreate(id, p);
    if (method == "tab.list")   return handleTabList(id, p);
    if (method == "tab.switch") return handleTabSwitch(id, p);
    if (method == "tab.close")  return handleTabClose(id, p);

    // pane.*
    if (method == "pane.split")       return handlePaneSplit(id, p);
    if (method == "pane.close")       return handlePaneClose(id, p);
    if (method == "pane.focus")       return handlePaneFocus(id, p);
    if (method == "pane.list")        return handlePaneList(id, p);
    if (method == "pane.send-text")   return handlePaneSendText(id, p);
    if (method == "pane.send-keys")   return handlePaneSendKeys(id, p);
    if (method == "pane.read-screen") return handlePaneReadScreen(id, p);
    if (method == "pane.set-status")  return handlePaneSetStatus(id, p);
    if (method == "pane.set-progress") return handlePaneSetProgress(id, p);

    // agent.*
    if (method == "agent.log")         return handleAgentLog(id, p);
    if (method == "agent.launch")      return handleAgentLaunch(id, p);
    if (method == "agent.orchestrate") return handleAgentOrchestrate(id, p);
    if (method == "agent.readAll")     return handleAgentReadAll(id, p);
    if (method == "agent.getIdle")     return handleAgentGetIdle(id, p);
    if (method == "agent.closeAll")    return handleAgentCloseAll(id, p);

    // notify.*
    if (method == "notify.send") return handleNotifySend(id, p);

    // browser.*
    if (method == "browser.open")      return handleBrowserOpen(id, p);
    if (method == "browser.navigate")  return handleBrowserNavigate(id, p);
    if (method == "browser.executeJS") return handleBrowserExecuteJS(id, p);
    if (method == "browser.snapshot")  return handleBrowserSnapshot(id, p);
    if (method == "browser.show")      return handleBrowserShow(id, p);
    if (method == "browser.hide")      return handleBrowserHide(id, p);
    if (method == "browser.click")     return handleBrowserClick(id, p);
    if (method == "browser.fill")      return handleBrowserFill(id, p);

    // query.*
    if (method == "query.active-pane")  return handleQueryActivePane(id, p);
    if (method == "query.pane-info")    return handleQueryPaneInfo(id, p);
    if (method == "query.agent-state")  return handleQueryAgentState(id, p);
    if (method == "query.scrollback")  return handleQueryScrollback(id, p);

    return rpc::makeError(id, rpc::kMethodNotFound,
                          "Method not found: " + method);
}

// --- notify handlers ---

rpc::Response CommandDispatcher::handleNotifySend(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("title") || !p["title"].is_string()) {
        return rpc::makeError(id, rpc::kInvalidParams, "title required");
    }

    PaneId pane_id = 0;
    if (p.contains("pane_id") && p["pane_id"].is_number()) {
        pane_id = p["pane_id"].get<PaneId>();
    }

    auto title = p["title"].get<std::string>();
    auto body = p.value("body", std::string{});

    NotificationUrgency urgency = NotificationUrgency::Normal;
    if (p.contains("urgency") && p["urgency"].is_string()) {
        auto u = p["urgency"].get<std::string>();
        if (u == "low") urgency = NotificationUrgency::Low;
        else if (u == "critical") urgency = NotificationUrgency::Critical;
    }

    NotificationSource source = NotificationSource::Agent;
    if (p.contains("source") && p["source"].is_string()) {
        auto s = p["source"].get<std::string>();
        if (s == "system") source = NotificationSource::System;
    }

    auto nid = notifications_.add(pane_id, source, urgency, title, body);
    return rpc::makeResult(id, {{"notification_id", nid}});
}

// --- query handlers ---

rpc::Response CommandDispatcher::handleQueryActivePane(
    std::optional<int64_t> id, const nlohmann::json& /*p*/) {
    auto ws_id = mux_.activeWorkspaceId();
    if (ws_id == kInvalidWorkspace) {
        return rpc::makeError(id, rpc::kNotFound, "No active workspace");
    }

    auto* ws = mux_.getWorkspace(ws_id);
    if (!ws || ws->tabs.empty()) {
        return rpc::makeError(id, rpc::kNotFound, "No active tab");
    }

    auto* tab = mux_.activeTab(ws_id);
    if (!tab) {
        return rpc::makeError(id, rpc::kNotFound, "No active tab");
    }

    auto pane_id = mux_.activePaneId(ws_id, tab->id);
    return rpc::makeResult(id, {
        {"workspace_id", ws_id},
        {"tab_id", tab->id},
        {"pane_id", pane_id}
    });
}

rpc::Response CommandDispatcher::handleQueryPaneInfo(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("pane_id") || !p["pane_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "pane_id required");
    }
    auto pane_id = p["pane_id"].get<PaneId>();

    // Basic pane info - we return what we can from the mux layer.
    // Full terminal state (rows, cols, title, cursor) would require access
    // to the Screen object which is not exposed via Mux. Return pane_id confirmation.
    return rpc::makeResult(id, {
        {"pane_id", pane_id},
        {"exists", true}
    });
}

rpc::Response CommandDispatcher::handleQueryAgentState(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (p.contains("pane_id") && p["pane_id"].is_number()) {
        auto pane_id = p["pane_id"].get<uint32_t>();
        const auto* info = agent_tracker_.getAgent(pane_id);
        if (!info) {
            return rpc::makeError(id, rpc::kNotFound, "No agent in pane");
        }

        auto stateStr = [](AgentState s) -> std::string {
            switch (s) {
                case AgentState::Inactive:   return "inactive";
                case AgentState::Starting:   return "starting";
                case AgentState::Idle:       return "idle";
                case AgentState::Running:    return "running";
                case AgentState::NeedsInput: return "needs_input";
                case AgentState::Exited:     return "exited";
            }
            return "unknown";
        };

        return rpc::makeResult(id, {
            {"pane_id", pane_id},
            {"type", static_cast<int>(info->type)},
            {"state", stateStr(info->state)},
            {"name", info->name},
            {"pid", info->pid},
            {"last_message", info->last_message}
        });
    }

    // Return all agents
    auto agents = agent_tracker_.allAgents();
    nlohmann::json arr = nlohmann::json::array();
    for (const auto* info : agents) {
        auto stateStr = [](AgentState s) -> std::string {
            switch (s) {
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
            {"pane_id", info->pane_id},
            {"type", static_cast<int>(info->type)},
            {"state", stateStr(info->state)},
            {"name", info->name},
            {"pid", info->pid},
            {"last_message", info->last_message}
        });
    }
    return rpc::makeResult(id, {{"agents", arr}});
}

rpc::Response CommandDispatcher::handleQueryScrollback(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!scrollback_cb_) {
        return rpc::makeError(id, rpc::kInternalError,
                              "No scrollback callback configured");
    }

    PaneId pane_id = 0;
    if (p.contains("pane_id") && p["pane_id"].is_number()) {
        pane_id = p["pane_id"].get<PaneId>();
    } else {
        // Use active pane if no pane_id specified
        auto ws_id = mux_.activeWorkspaceId();
        if (ws_id != kInvalidWorkspace) {
            auto* tab = mux_.activeTab(ws_id);
            if (tab) {
                pane_id = mux_.activePaneId(ws_id, tab->id);
            }
        }
    }

    if (pane_id == kInvalidPane) {
        return rpc::makeError(id, rpc::kNotFound, "No pane found");
    }

    int line_count = 50;  // default
    if (p.contains("lines") && p["lines"].is_number()) {
        line_count = p["lines"].get<int>();
        if (line_count <= 0) line_count = 50;
        if (line_count > 10000) line_count = 10000;  // cap
    }

    auto lines = scrollback_cb_(pane_id, line_count);

    nlohmann::json lines_arr = nlohmann::json::array();
    for (const auto& line : lines) {
        lines_arr.push_back(line);
    }

    return rpc::makeResult(id, {
        {"pane_id", pane_id},
        {"lines", lines_arr},
        {"count", static_cast<int>(lines.size())}
    });
}

}  // namespace termcore
