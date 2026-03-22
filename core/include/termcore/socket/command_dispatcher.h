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

/// Callback to read scrollback lines from a pane.
/// Returns the requested lines as a vector of strings, or empty on failure.
using ScrollbackReadCallback = std::function<std::vector<std::string>(PaneId pane_id, int line_count)>;

/// Callback to get currently selected text from a pane.
/// Returns the selected text, or empty string if no selection.
using SelectionReadCallback = std::function<std::string(PaneId pane_id)>;

/// Cursor position info returned by the cursor callback.
struct CursorPositionInfo {
    int row = 0;
    int col = 0;
    bool visible = true;
};

/// Callback to get cursor position from a pane.
using CursorPositionCallback = std::function<CursorPositionInfo(PaneId pane_id)>;

/// Callback to trigger visual attention (border glow) on a pane.
/// Parameters: pane_id, intensity (0.0-1.0), color (RGB hex)
using AttentionCallback = std::function<void(PaneId pane_id, float intensity, uint32_t color)>;

/// Routes JSON-RPC requests to the appropriate subsystem handler.
class CommandDispatcher {
public:
    CommandDispatcher(Mux& mux,
                      NotificationStore& notifications,
                      AgentTracker& agent_tracker,
                      PaneWriteCallback write_cb = nullptr,
                      WebViewCallback webview_cb = nullptr,
                      ScrollbackReadCallback scrollback_cb = nullptr);

    /// Dispatch a parsed JSON-RPC request to the appropriate handler.
    rpc::Response dispatch(const rpc::Request& req);

    /// Set the callback for reading screen content from panes.
    void setPaneReadCallback(PaneReadCallback cb) { read_cb_ = std::move(cb); }

    /// Set the callback for reading selected text from panes.
    void setSelectionReadCallback(SelectionReadCallback cb) { selection_cb_ = std::move(cb); }

    /// Set the callback for reading cursor position from panes.
    void setCursorPositionCallback(CursorPositionCallback cb) { cursor_cb_ = std::move(cb); }

    /// Set the callback for triggering visual attention on panes.
    void setAttentionCallback(AttentionCallback cb) { attention_cb_ = std::move(cb); }

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
    rpc::Response handleQueryScrollback(std::optional<int64_t> id, const nlohmann::json& p);

    // agent.* (orchestration)
    rpc::Response handleAgentLaunch(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleAgentOrchestrate(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleAgentReadAll(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleAgentGetIdle(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleAgentCloseAll(std::optional<int64_t> id, const nlohmann::json& p);

    // terminal.* (terminal control)
    rpc::Response handleTerminalGetScreenContent(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleTerminalSendInput(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleTerminalGetSelection(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleTerminalGetCursorPosition(std::optional<int64_t> id, const nlohmann::json& p);

    // agent.* (status display)
    rpc::Response handleAgentSetStatus(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleAgentSetProgress(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleAgentSetStatusPills(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleAgentRequestAttention(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleAgentClearStatus(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleAgentAddStatePattern(std::optional<int64_t> id, const nlohmann::json& p);

    // workspace.* (awareness)
    rpc::Response handleWorkspaceListPanes(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleWorkspaceGetActivePane(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleWorkspaceGetPaneInfo(std::optional<int64_t> id, const nlohmann::json& p);

    // agent.* (orchestration enhanced)
    rpc::Response handleAgentBroadcast(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleAgentSendToAgent(std::optional<int64_t> id, const nlohmann::json& p);
    rpc::Response handleAgentListAgents(std::optional<int64_t> id, const nlohmann::json& p);

    Mux& mux_;
    NotificationStore& notifications_;
    AgentTracker& agent_tracker_;
    PaneWriteCallback write_cb_;
    PaneReadCallback read_cb_;
    WebViewCallback webview_cb_;
    ScrollbackReadCallback scrollback_cb_;
    SelectionReadCallback selection_cb_;
    CursorPositionCallback cursor_cb_;
    AttentionCallback attention_cb_;

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
