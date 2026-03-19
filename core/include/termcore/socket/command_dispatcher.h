#pragma once

#include "termcore/socket/jsonrpc.h"
#include "termcore/mux.h"
#include "termcore/notification.h"
#include "termcore/agent.h"
#include "termcore/agent_orchestrator.h"

#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace termcore {

/// Callback to write data to a pane's PTY.
/// Returns true on success, false if pane not found.
using PaneWriteCallback = std::function<bool(PaneId pane_id, std::string_view data)>;

/// Callback to read screen lines from a pane.
/// Returns lines of text (visible screen + scrollback). Empty vector if pane not found.
/// pane_id: target pane, lines: number of lines to read (0 = all visible), scrollback: include scrollback
using PaneReadCallback = std::function<std::vector<std::string>(PaneId pane_id, int lines, bool scrollback)>;

/// Callback for WebView commands (method, params).
using WebViewCallback = std::function<void(const std::string& method, const nlohmann::json& params)>;

/// Status pill metadata for a pane (set by agents).
struct PaneStatus {
    std::string key;
    std::string value;
    std::string icon;
};

/// Progress bar metadata for a pane (set by agents).
struct PaneProgress {
    float value = 0.0f;      // 0.0 - 1.0
    std::string label;
};

/// A single log entry from an agent.
struct LogEntry {
    std::string level;        // info, success, warning, error
    std::string message;
    std::chrono::steady_clock::time_point timestamp;
};

/// Routes JSON-RPC requests to the appropriate subsystem handler.
class CommandDispatcher {
public:
    CommandDispatcher(Mux& mux,
                      NotificationStore& notifications,
                      AgentTracker& agent_tracker,
                      PaneWriteCallback write_cb = nullptr,
                      WebViewCallback webview_cb = nullptr);

    /// Dispatch a parsed JSON-RPC request to the appropriate handler.
    rpc::Response dispatch(const rpc::Request& req);

    /// Set the callback for reading screen content from panes.
    void setPaneReadCallback(PaneReadCallback cb) { read_cb_ = std::move(cb); }

    /// Access stored status metadata for a pane.
    std::vector<PaneStatus> getPaneStatuses(PaneId pane_id) const;

    /// Access stored progress metadata for a pane.
    const PaneProgress* getPaneProgress(PaneId pane_id) const;

    /// Access stored log entries (newest first, up to limit).
    std::vector<LogEntry> getLogEntries(size_t limit = 100) const;

private:
    // workspace.*
    rpc::Response handleWorkspaceCreate(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleWorkspaceList(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleWorkspaceSwitch(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleWorkspaceDestroy(std::optional<int64_t> id, const nlohmann::json& p);

    // tab.*
    rpc::Response handleTabCreate(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleTabList(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleTabSwitch(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleTabClose(std::optional<int64_t> id, const nlohmann::json& p);

    // pane.*
    rpc::Response handlePaneSplit(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handlePaneClose(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handlePaneFocus(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handlePaneList(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handlePaneSendText(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handlePaneSendKeys(std::optional<int64_t> id, const nlohmann::json& p);

    // agent.* (pane.readScreen, pane.setStatus, pane.setProgress, pane.log)
    rpc::Response handlePaneReadScreen(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handlePaneSetStatus(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handlePaneSetProgress(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleAgentLog(std::optional<int64_t> id, const nlohmann::json& p);

    // notify.*
    rpc::Response handleNotifySend(std::optional<int64_t> id, const nlohmann::json& p);

    // browser.*
    rpc::Response handleBrowserOpen(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleBrowserNavigate(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleBrowserExecuteJS(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleBrowserSnapshot(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleBrowserShow(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleBrowserHide(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleBrowserClick(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleBrowserFill(std::optional<int64_t> id, const nlohmann::json& p);

    // query.*
    rpc::Response handleQueryActivePane(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleQueryPaneInfo(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleQueryAgentState(std::optional<int64_t> id, const nlohmann::json& p);

    // agent.* (orchestration)
    rpc::Response handleAgentLaunch(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleAgentOrchestrate(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleAgentReadAll(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleAgentGetIdle(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleAgentCloseAll(std::optional<int64_t> id, const nlohmann::json& p);

    Mux& mux_;
    NotificationStore& notifications_;
    AgentTracker& agent_tracker_;
    PaneWriteCallback write_cb_;
    PaneReadCallback read_cb_;
    WebViewCallback webview_cb_;

    // Agent orchestrator
    AgentOrchestrator orchestrator_;

    // Agent metadata storage
    mutable std::mutex agent_meta_mutex_;
    std::unordered_map<PaneId, std::vector<PaneStatus>> pane_statuses_;
    std::unordered_map<PaneId, PaneProgress> pane_progress_;
    std::deque<LogEntry> log_entries_;
    static constexpr size_t kMaxLogEntries = 1000;
};

}  // namespace termcore
