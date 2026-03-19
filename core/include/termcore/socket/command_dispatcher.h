#pragma once

#include "termcore/socket/jsonrpc.h"
#include "termcore/mux.h"
#include "termcore/notification.h"
#include "termcore/agent.h"

#include <functional>
#include <string_view>

namespace termcore {

/// Callback to write data to a pane's PTY.
/// Returns true on success, false if pane not found.
using PaneWriteCallback = std::function<bool(PaneId pane_id, std::string_view data)>;

/// Callback for WebView commands (method, params).
using WebViewCallback = std::function<void(const std::string& method, const nlohmann::json& params)>;

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

    Mux& mux_;
    NotificationStore& notifications_;
    AgentTracker& agent_tracker_;
    PaneWriteCallback write_cb_;
    WebViewCallback webview_cb_;
};

}  // namespace termcore
