#include "termcore/socket/command_dispatcher.h"

#include <unordered_map>

namespace termcore {

// Key translation table for pane.send-keys
static const std::unordered_map<std::string, std::string> kKeyMap = {
    {"enter",     "\r"},
    {"escape",    "\x1b"},
    {"tab",       "\t"},
    {"backspace", "\x7f"},
    {"ctrl+c",    "\x03"},
    {"ctrl+d",    "\x04"},
    {"ctrl+z",    "\x1a"},
    {"ctrl+l",    "\x0c"},
    {"ctrl+a",    "\x01"},
    {"ctrl+e",    "\x05"},
    {"ctrl+k",    "\x0b"},
    {"ctrl+u",    "\x15"},
    {"ctrl+w",    "\x17"},
    {"ctrl+r",    "\x12"},
    {"up",        "\x1b[A"},
    {"down",      "\x1b[B"},
    {"right",     "\x1b[C"},
    {"left",      "\x1b[D"},
    {"home",      "\x1b[H"},
    {"end",       "\x1b[F"},
    {"delete",    "\x1b[3~"},
    {"pageup",    "\x1b[5~"},
    {"pagedown",  "\x1b[6~"},
    {"f1",        "\x1bOP"},
    {"f2",        "\x1bOQ"},
    {"f3",        "\x1bOR"},
    {"f4",        "\x1bOS"},
    {"f5",        "\x1b[15~"},
    {"f6",        "\x1b[17~"},
    {"f7",        "\x1b[18~"},
    {"f8",        "\x1b[19~"},
    {"f9",        "\x1b[20~"},
    {"f10",       "\x1b[21~"},
    {"f11",       "\x1b[23~"},
    {"f12",       "\x1b[24~"},
};

// --- workspace handlers ---

rpc::Response CommandDispatcher::handleWorkspaceCreate(
    std::optional<int64_t> id, const nlohmann::json& p) {
    std::string name;
    if (p.contains("name") && p["name"].is_string()) {
        name = p["name"].get<std::string>();
    }
    auto ws_id = mux_.createWorkspace(name);
    if (ws_id == kInvalidWorkspace) {
        return rpc::makeError(id, rpc::kInternalError, "Failed to create workspace");
    }
    auto* ws = mux_.getWorkspace(ws_id);
    return rpc::makeResult(id, {
        {"workspace_id", ws_id},
        {"name", ws ? ws->name : name}
    });
}

rpc::Response CommandDispatcher::handleWorkspaceList(
    std::optional<int64_t> id, const nlohmann::json& /*p*/) {
    nlohmann::json workspaces = nlohmann::json::array();
    auto active_ws = mux_.activeWorkspaceId();

    // Iterate over all workspaces
    // We try workspace IDs starting from 1 up to a reasonable range
    for (WorkspaceId wid = 1; wid <= 1000; ++wid) {
        auto* ws = mux_.getWorkspace(wid);
        if (!ws) continue;

        std::string ws_ref = "ws:" + std::to_string(ws->id);

        // Build nested tabs with ref IDs
        nlohmann::json tabs_arr = nlohmann::json::array();
        for (const auto& tab : ws->tabs) {
            std::string tab_ref = ws_ref + "/tab:" + std::to_string(tab->id);
            auto all_panes = mux_.allPanes(wid, tab->id);
            auto active_pane = mux_.activePaneId(wid, tab->id);

            // Build nested panes with ref IDs
            nlohmann::json panes_arr = nlohmann::json::array();
            for (auto pid : all_panes) {
                std::string pane_ref = tab_ref + "/pane:" + std::to_string(pid);
                panes_arr.push_back({
                    {"id", pid},
                    {"ref", pane_ref},
                    {"is_active", pid == active_pane}
                });
            }

            tabs_arr.push_back({
                {"id", tab->id},
                {"ref", tab_ref},
                {"title", tab->title},
                {"pane_count", all_panes.size()},
                {"active_pane_id", active_pane},
                {"panes", panes_arr}
            });
        }

        workspaces.push_back({
            {"id", ws->id},
            {"ref", ws_ref},
            {"name", ws->name},
            {"tab_count", ws->tabs.size()},
            {"active", ws->id == active_ws},
            {"tabs", tabs_arr}
        });
    }

    return rpc::makeResult(id, {{"workspaces", workspaces}});
}

rpc::Response CommandDispatcher::handleWorkspaceSwitch(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("workspace_id") || !p["workspace_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "workspace_id required");
    }
    auto ws_id = p["workspace_id"].get<WorkspaceId>();
    auto* ws = mux_.getWorkspace(ws_id);
    if (!ws) {
        return rpc::makeError(id, rpc::kNotFound, "Workspace not found");
    }
    mux_.setActiveWorkspace(ws_id);
    return rpc::makeResult(id, {{"success", true}});
}

rpc::Response CommandDispatcher::handleWorkspaceDestroy(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("workspace_id") || !p["workspace_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "workspace_id required");
    }
    auto ws_id = p["workspace_id"].get<WorkspaceId>();
    auto* ws = mux_.getWorkspace(ws_id);
    if (!ws) {
        return rpc::makeError(id, rpc::kNotFound, "Workspace not found");
    }
    mux_.destroyWorkspace(ws_id);
    return rpc::makeResult(id, {{"success", true}});
}

// --- tab handlers ---

rpc::Response CommandDispatcher::handleTabCreate(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("workspace_id") || !p["workspace_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "workspace_id required");
    }
    auto ws_id = p["workspace_id"].get<WorkspaceId>();
    int rows = p.value("rows", 24);
    int cols = p.value("cols", 80);

    auto tab_id = mux_.createTab(ws_id, rows, cols);
    if (tab_id == kInvalidTab) {
        return rpc::makeError(id, rpc::kNotFound, "Workspace not found or tab creation failed");
    }
    return rpc::makeResult(id, {{"tab_id", tab_id}});
}

rpc::Response CommandDispatcher::handleTabList(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("workspace_id") || !p["workspace_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "workspace_id required");
    }
    auto ws_id = p["workspace_id"].get<WorkspaceId>();
    auto* ws = mux_.getWorkspace(ws_id);
    if (!ws) {
        return rpc::makeError(id, rpc::kNotFound, "Workspace not found");
    }

    std::string ws_ref = "ws:" + std::to_string(ws_id);

    nlohmann::json tabs = nlohmann::json::array();
    for (const auto& tab : ws->tabs) {
        std::string tab_ref = ws_ref + "/tab:" + std::to_string(tab->id);
        auto all_panes = mux_.allPanes(ws_id, tab->id);
        auto active_pane = mux_.activePaneId(ws_id, tab->id);
        tabs.push_back({
            {"id", tab->id},
            {"ref", tab_ref},
            {"title", tab->title},
            {"pane_count", all_panes.size()},
            {"active_pane_id", active_pane}
        });
    }

    return rpc::makeResult(id, {{"tabs", tabs}});
}

rpc::Response CommandDispatcher::handleTabSwitch(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("workspace_id") || !p["workspace_id"].is_number() ||
        !p.contains("tab_id") || !p["tab_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "workspace_id and tab_id required");
    }
    auto ws_id = p["workspace_id"].get<WorkspaceId>();
    auto tab_id = p["tab_id"].get<TabId>();
    mux_.setActiveTab(ws_id, tab_id);
    return rpc::makeResult(id, {{"success", true}});
}

rpc::Response CommandDispatcher::handleTabClose(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("workspace_id") || !p["workspace_id"].is_number() ||
        !p.contains("tab_id") || !p["tab_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "workspace_id and tab_id required");
    }
    auto ws_id = p["workspace_id"].get<WorkspaceId>();
    auto tab_id = p["tab_id"].get<TabId>();
    mux_.destroyTab(ws_id, tab_id);
    return rpc::makeResult(id, {{"success", true}});
}

// --- pane handlers ---

rpc::Response CommandDispatcher::handlePaneSplit(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("workspace_id") || !p["workspace_id"].is_number() ||
        !p.contains("tab_id") || !p["tab_id"].is_number() ||
        !p.contains("pane_id") || !p["pane_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams,
                              "workspace_id, tab_id, pane_id required");
    }

    auto ws_id = p["workspace_id"].get<WorkspaceId>();
    auto tab_id = p["tab_id"].get<TabId>();
    auto pane_id = p["pane_id"].get<PaneId>();
    int rows = p.value("rows", 24);
    int cols = p.value("cols", 80);

    SplitDirection dir = SplitDirection::Horizontal;
    if (p.contains("direction") && p["direction"].is_string()) {
        auto d = p["direction"].get<std::string>();
        if (d == "vertical") {
            dir = SplitDirection::Vertical;
        }
    }

    auto new_pane = mux_.splitPane(ws_id, tab_id, pane_id, dir, rows, cols);
    if (new_pane == kInvalidPane) {
        return rpc::makeError(id, rpc::kNotFound, "Pane not found or split failed");
    }
    return rpc::makeResult(id, {{"pane_id", new_pane}});
}

rpc::Response CommandDispatcher::handlePaneClose(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("workspace_id") || !p["workspace_id"].is_number() ||
        !p.contains("tab_id") || !p["tab_id"].is_number() ||
        !p.contains("pane_id") || !p["pane_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams,
                              "workspace_id, tab_id, pane_id required");
    }
    auto ws_id = p["workspace_id"].get<WorkspaceId>();
    auto tab_id = p["tab_id"].get<TabId>();
    auto pane_id = p["pane_id"].get<PaneId>();
    mux_.closePane(ws_id, tab_id, pane_id);
    return rpc::makeResult(id, {{"success", true}});
}

rpc::Response CommandDispatcher::handlePaneFocus(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("workspace_id") || !p["workspace_id"].is_number() ||
        !p.contains("tab_id") || !p["tab_id"].is_number() ||
        !p.contains("pane_id") || !p["pane_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams,
                              "workspace_id, tab_id, pane_id required");
    }
    auto ws_id = p["workspace_id"].get<WorkspaceId>();
    auto tab_id = p["tab_id"].get<TabId>();
    auto pane_id = p["pane_id"].get<PaneId>();
    mux_.setActivePane(ws_id, tab_id, pane_id);
    return rpc::makeResult(id, {{"success", true}});
}

rpc::Response CommandDispatcher::handlePaneList(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("workspace_id") || !p["workspace_id"].is_number() ||
        !p.contains("tab_id") || !p["tab_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams,
                              "workspace_id and tab_id required");
    }
    auto ws_id = p["workspace_id"].get<WorkspaceId>();
    auto tab_id = p["tab_id"].get<TabId>();
    auto panes = mux_.allPanes(ws_id, tab_id);
    auto active = mux_.activePaneId(ws_id, tab_id);

    std::string base_ref = "ws:" + std::to_string(ws_id)
                         + "/tab:" + std::to_string(tab_id);

    nlohmann::json pane_list = nlohmann::json::array();
    for (auto pid : panes) {
        std::string pane_ref = base_ref + "/pane:" + std::to_string(pid);
        pane_list.push_back({
            {"id", pid},
            {"ref", pane_ref},
            {"is_active", pid == active}
        });
    }
    return rpc::makeResult(id, {{"panes", pane_list}});
}

rpc::Response CommandDispatcher::handlePaneSendText(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("pane_id") || !p["pane_id"].is_number() ||
        !p.contains("text") || !p["text"].is_string()) {
        return rpc::makeError(id, rpc::kInvalidParams, "pane_id and text required");
    }
    auto pane_id = p["pane_id"].get<PaneId>();
    auto text = p["text"].get<std::string>();

    if (!write_cb_) {
        return rpc::makeError(id, rpc::kInternalError, "No write callback configured");
    }
    bool ok = write_cb_(pane_id, text);
    if (!ok) {
        return rpc::makeError(id, rpc::kNotFound, "Pane not found");
    }
    return rpc::makeResult(id, {{"bytes_written", static_cast<int>(text.size())}});
}

rpc::Response CommandDispatcher::handlePaneSendKeys(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("pane_id") || !p["pane_id"].is_number() ||
        !p.contains("keys") || !p["keys"].is_array()) {
        return rpc::makeError(id, rpc::kInvalidParams, "pane_id and keys array required");
    }
    auto pane_id = p["pane_id"].get<PaneId>();

    if (!write_cb_) {
        return rpc::makeError(id, rpc::kInternalError, "No write callback configured");
    }

    std::string data;
    for (const auto& key : p["keys"]) {
        if (!key.is_string()) continue;
        auto key_str = key.get<std::string>();
        auto it = kKeyMap.find(key_str);
        if (it != kKeyMap.end()) {
            data += it->second;
        } else {
            // Unknown key name: send as literal text
            data += key_str;
        }
    }

    bool ok = write_cb_(pane_id, data);
    if (!ok) {
        return rpc::makeError(id, rpc::kNotFound, "Pane not found");
    }
    return rpc::makeResult(id, {{"success", true}});
}

}  // namespace termcore
