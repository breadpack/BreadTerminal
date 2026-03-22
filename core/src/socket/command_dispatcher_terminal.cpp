#include "termcore/socket/command_dispatcher.h"

#include <algorithm>

namespace termcore {

// ---------------------------------------------------------------------------
// terminal.getScreenContent — read visible terminal content for a row range
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleTerminalGetScreenContent(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("pane_id") || !p["pane_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "pane_id required");
    }
    auto pane_id = p["pane_id"].get<PaneId>();

    if (!read_cb_) {
        return rpc::makeError(id, rpc::kInternalError,
                              "No read callback configured");
    }

    int start_row = p.value("start_row", 0);
    int end_row = p.value("end_row", 0);

    // Read all visible lines, then slice if range specified
    auto all_lines = read_cb_(pane_id, 0, false);
    if (all_lines.empty()) {
        return rpc::makeResult(id, {
            {"pane_id", pane_id},
            {"lines", nlohmann::json::array()},
            {"line_count", 0}
        });
    }

    int total = static_cast<int>(all_lines.size());

    // Clamp range
    if (start_row < 0) start_row = 0;
    if (end_row <= 0 || end_row > total) end_row = total;
    if (start_row >= end_row) start_row = 0;

    nlohmann::json line_array = nlohmann::json::array();
    for (int i = start_row; i < end_row; ++i) {
        line_array.push_back(all_lines[static_cast<size_t>(i)]);
    }

    return rpc::makeResult(id, {
        {"pane_id", pane_id},
        {"lines", line_array},
        {"line_count", static_cast<int>(line_array.size())},
        {"start_row", start_row},
        {"end_row", end_row},
        {"total_rows", total}
    });
}

// ---------------------------------------------------------------------------
// terminal.sendInput — send text input to a pane
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleTerminalSendInput(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("pane_id") || !p["pane_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "pane_id required");
    }
    if (!p.contains("text") || !p["text"].is_string()) {
        return rpc::makeError(id, rpc::kInvalidParams, "text required");
    }

    auto pane_id = p["pane_id"].get<PaneId>();
    auto text = p["text"].get<std::string>();

    if (!write_cb_) {
        return rpc::makeError(id, rpc::kInternalError,
                              "No write callback configured");
    }

    bool ok = write_cb_(pane_id, text);
    if (!ok) {
        return rpc::makeError(id, rpc::kNotFound, "Pane not found");
    }

    return rpc::makeResult(id, {
        {"pane_id", pane_id},
        {"bytes_sent", static_cast<int>(text.size())}
    });
}

// ---------------------------------------------------------------------------
// terminal.getSelection — get currently selected text
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleTerminalGetSelection(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("pane_id") || !p["pane_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "pane_id required");
    }
    auto pane_id = p["pane_id"].get<PaneId>();

    if (!selection_cb_) {
        return rpc::makeError(id, rpc::kInternalError,
                              "No selection callback configured");
    }

    auto text = selection_cb_(pane_id);
    return rpc::makeResult(id, {
        {"pane_id", pane_id},
        {"text", text},
        {"has_selection", !text.empty()}
    });
}

// ---------------------------------------------------------------------------
// terminal.getCursorPosition — get cursor row/col
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleTerminalGetCursorPosition(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("pane_id") || !p["pane_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "pane_id required");
    }
    auto pane_id = p["pane_id"].get<PaneId>();

    if (!cursor_cb_) {
        return rpc::makeError(id, rpc::kInternalError,
                              "No cursor callback configured");
    }

    auto cursor = cursor_cb_(pane_id);
    return rpc::makeResult(id, {
        {"pane_id", pane_id},
        {"row", cursor.row},
        {"col", cursor.col},
        {"visible", cursor.visible}
    });
}

}  // namespace termcore
