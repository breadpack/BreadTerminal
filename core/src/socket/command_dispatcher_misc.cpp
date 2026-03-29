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
    initDispatchTable();
}

void CommandDispatcher::initDispatchTable() {
    // workspace.*
    dispatch_table_["workspace.create"]       = &CommandDispatcher::handleWorkspaceCreate;
    dispatch_table_["workspace.list"]         = &CommandDispatcher::handleWorkspaceList;
    dispatch_table_["workspace.switch"]       = &CommandDispatcher::handleWorkspaceSwitch;
    dispatch_table_["workspace.destroy"]      = &CommandDispatcher::handleWorkspaceDestroy;
    dispatch_table_["workspace.listPanes"]    = &CommandDispatcher::handleWorkspaceListPanes;
    dispatch_table_["workspace.getActivePane"]= &CommandDispatcher::handleWorkspaceGetActivePane;
    dispatch_table_["workspace.getPaneInfo"]  = &CommandDispatcher::handleWorkspaceGetPaneInfo;

    // tab.*
    dispatch_table_["tab.create"]  = &CommandDispatcher::handleTabCreate;
    dispatch_table_["tab.list"]    = &CommandDispatcher::handleTabList;
    dispatch_table_["tab.switch"]  = &CommandDispatcher::handleTabSwitch;
    dispatch_table_["tab.close"]   = &CommandDispatcher::handleTabClose;

    // pane.*
    dispatch_table_["pane.split"]        = &CommandDispatcher::handlePaneSplit;
    dispatch_table_["pane.close"]        = &CommandDispatcher::handlePaneClose;
    dispatch_table_["pane.focus"]        = &CommandDispatcher::handlePaneFocus;
    dispatch_table_["pane.list"]         = &CommandDispatcher::handlePaneList;
    dispatch_table_["pane.send-text"]    = &CommandDispatcher::handlePaneSendText;
    dispatch_table_["pane.send-keys"]    = &CommandDispatcher::handlePaneSendKeys;
    dispatch_table_["pane.read-screen"]  = &CommandDispatcher::handlePaneReadScreen;
    dispatch_table_["pane.set-status"]   = &CommandDispatcher::handlePaneSetStatus;
    dispatch_table_["pane.set-progress"] = &CommandDispatcher::handlePaneSetProgress;

    // agent.*
    dispatch_table_["agent.log"]              = &CommandDispatcher::handleAgentLog;
    dispatch_table_["agent.launch"]           = &CommandDispatcher::handleAgentLaunch;
    dispatch_table_["agent.orchestrate"]      = &CommandDispatcher::handleAgentOrchestrate;
    dispatch_table_["agent.readAll"]          = &CommandDispatcher::handleAgentReadAll;
    dispatch_table_["agent.getIdle"]          = &CommandDispatcher::handleAgentGetIdle;
    dispatch_table_["agent.closeAll"]         = &CommandDispatcher::handleAgentCloseAll;
    dispatch_table_["agent.setStatus"]        = &CommandDispatcher::handleAgentSetStatus;
    dispatch_table_["agent.setProgress"]      = &CommandDispatcher::handleAgentSetProgress;
    dispatch_table_["agent.setStatusPills"]   = &CommandDispatcher::handleAgentSetStatusPills;
    dispatch_table_["agent.requestAttention"] = &CommandDispatcher::handleAgentRequestAttention;
    dispatch_table_["agent.clearStatus"]      = &CommandDispatcher::handleAgentClearStatus;
    dispatch_table_["agent.addStatePattern"]  = &CommandDispatcher::handleAgentAddStatePattern;
    dispatch_table_["agent.broadcast"]        = &CommandDispatcher::handleAgentBroadcast;
    dispatch_table_["agent.sendToAgent"]      = &CommandDispatcher::handleAgentSendToAgent;
    dispatch_table_["agent.listAgents"]       = &CommandDispatcher::handleAgentListAgents;

    // notify.*
    dispatch_table_["notify.send"] = &CommandDispatcher::handleNotifySend;

    // browser.*
    dispatch_table_["browser.open"]      = &CommandDispatcher::handleBrowserOpen;
    dispatch_table_["browser.navigate"]  = &CommandDispatcher::handleBrowserNavigate;
    dispatch_table_["browser.executeJS"] = &CommandDispatcher::handleBrowserExecuteJS;
    dispatch_table_["browser.snapshot"]  = &CommandDispatcher::handleBrowserSnapshot;
    dispatch_table_["browser.show"]      = &CommandDispatcher::handleBrowserShow;
    dispatch_table_["browser.hide"]      = &CommandDispatcher::handleBrowserHide;
    dispatch_table_["browser.click"]     = &CommandDispatcher::handleBrowserClick;
    dispatch_table_["browser.fill"]      = &CommandDispatcher::handleBrowserFill;

    // query.*
    dispatch_table_["query.active-pane"]  = &CommandDispatcher::handleQueryActivePane;
    dispatch_table_["query.pane-info"]    = &CommandDispatcher::handleQueryPaneInfo;
    dispatch_table_["query.agent-state"]  = &CommandDispatcher::handleQueryAgentState;
    dispatch_table_["query.scrollback"]   = &CommandDispatcher::handleQueryScrollback;

    // terminal.*
    dispatch_table_["terminal.getScreenContent"]  = &CommandDispatcher::handleTerminalGetScreenContent;
    dispatch_table_["terminal.sendInput"]          = &CommandDispatcher::handleTerminalSendInput;
    dispatch_table_["terminal.getSelection"]       = &CommandDispatcher::handleTerminalGetSelection;
    dispatch_table_["terminal.getCursorPosition"]  = &CommandDispatcher::handleTerminalGetCursorPosition;

    // hook.*
    dispatch_table_["hook.event"] = &CommandDispatcher::handleHookEvent;
}

rpc::Response CommandDispatcher::dispatch(const rpc::Request& req) {
    auto it = dispatch_table_.find(req.method);
    if (it != dispatch_table_.end()) {
        return (this->*(it->second))(req.id, req.params);
    }
    return rpc::makeError(req.id, rpc::kMethodNotFound,
                          "Method not found: " + req.method);
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

        return rpc::makeResult(id, {
            {"pane_id", pane_id},
            {"type", static_cast<int>(info->type)},
            {"state", AgentTracker::stateToString(info->state)},
            {"name", info->name},
            {"pid", info->pid},
            {"last_message", info->last_message}
        });
    }

    // Return all agents
    auto agents = agent_tracker_.allAgents();
    nlohmann::json arr = nlohmann::json::array();
    for (const auto* info : agents) {
        arr.push_back({
            {"pane_id", info->pane_id},
            {"type", static_cast<int>(info->type)},
            {"state", AgentTracker::stateToString(info->state)},
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

// --- hook handlers ---

rpc::Response CommandDispatcher::handleHookEvent(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!hook_bridge_) {
        return rpc::makeError(id, rpc::kInternalError,
                              "HookBridge not configured");
    }
    hook_bridge_->processHookEvent(p);
    return rpc::makeResult(id, {{"ok", true}});
}

}  // namespace termcore
