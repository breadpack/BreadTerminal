#include "termcore/socket/command_dispatcher.h"

#include <algorithm>

namespace termcore {

// --- pane.read-screen ---

rpc::Response CommandDispatcher::handlePaneReadScreen(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("pane_id") || !p["pane_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "pane_id required");
    }
    auto pane_id = p["pane_id"].get<PaneId>();
    int lines = p.value("lines", 0);  // 0 = all visible lines
    bool scrollback = p.value("scrollback", false);

    if (!read_cb_) {
        return rpc::makeError(id, rpc::kInternalError, "No read callback configured");
    }

    auto content = read_cb_(pane_id, lines, scrollback);
    if (content.empty()) {
        // Could be an empty screen or pane not found -- return empty
        return rpc::makeResult(id, {
            {"pane_id", pane_id},
            {"lines", nlohmann::json::array()},
            {"line_count", 0}
        });
    }

    nlohmann::json line_array = nlohmann::json::array();
    for (const auto& line : content) {
        line_array.push_back(line);
    }

    return rpc::makeResult(id, {
        {"pane_id", pane_id},
        {"lines", line_array},
        {"line_count", static_cast<int>(content.size())}
    });
}

// --- pane.set-status ---

rpc::Response CommandDispatcher::handlePaneSetStatus(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("pane_id") || !p["pane_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "pane_id required");
    }
    if (!p.contains("key") || !p["key"].is_string()) {
        return rpc::makeError(id, rpc::kInvalidParams, "key required");
    }
    if (!p.contains("value") || !p["value"].is_string()) {
        return rpc::makeError(id, rpc::kInvalidParams, "value required");
    }

    auto pane_id = p["pane_id"].get<PaneId>();
    auto key = p["key"].get<std::string>();
    auto value = p["value"].get<std::string>();
    auto icon = p.value("icon", std::string{});

    {
        std::lock_guard<std::mutex> lock(agent_meta_mutex_);
        auto& statuses = pane_statuses_[pane_id];

        // Update existing key or add new one
        bool found = false;
        for (auto& s : statuses) {
            if (s.key == key) {
                s.value = value;
                s.icon = icon;
                found = true;
                break;
            }
        }
        if (!found) {
            statuses.push_back({key, value, icon});
        }
    }

    return rpc::makeResult(id, {{"success", true}});
}

// --- pane.set-progress ---

rpc::Response CommandDispatcher::handlePaneSetProgress(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("pane_id") || !p["pane_id"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "pane_id required");
    }
    if (!p.contains("value") || !p["value"].is_number()) {
        return rpc::makeError(id, rpc::kInvalidParams, "value (float 0.0-1.0) required");
    }

    auto pane_id = p["pane_id"].get<PaneId>();
    float value = p["value"].get<float>();
    value = std::clamp(value, 0.0f, 1.0f);
    auto label = p.value("label", std::string{});

    {
        std::lock_guard<std::mutex> lock(agent_meta_mutex_);
        pane_progress_[pane_id] = PaneProgress{value, label};
    }

    return rpc::makeResult(id, {{"success", true}});
}

// --- agent.log ---

rpc::Response CommandDispatcher::handleAgentLog(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("level") || !p["level"].is_string()) {
        return rpc::makeError(id, rpc::kInvalidParams, "level required (info|success|warning|error)");
    }
    if (!p.contains("message") || !p["message"].is_string()) {
        return rpc::makeError(id, rpc::kInvalidParams, "message required");
    }

    auto level = p["level"].get<std::string>();
    if (level != "info" && level != "success" && level != "warning" && level != "error") {
        return rpc::makeError(id, rpc::kInvalidParams,
                              "level must be one of: info, success, warning, error");
    }
    auto message = p["message"].get<std::string>();

    {
        std::lock_guard<std::mutex> lock(agent_meta_mutex_);
        log_entries_.push_front({level, message, std::chrono::steady_clock::now()});
        while (log_entries_.size() > kMaxLogEntries) {
            log_entries_.pop_back();
        }
    }

    return rpc::makeResult(id, {{"success", true}});
}

// --- Accessor implementations ---

std::vector<PaneStatus> CommandDispatcher::getPaneStatuses(PaneId pane_id) const {
    std::lock_guard<std::mutex> lock(agent_meta_mutex_);
    auto it = pane_statuses_.find(pane_id);
    if (it == pane_statuses_.end()) return {};
    return it->second;
}

const PaneProgress* CommandDispatcher::getPaneProgress(PaneId pane_id) const {
    std::lock_guard<std::mutex> lock(agent_meta_mutex_);
    auto it = pane_progress_.find(pane_id);
    if (it == pane_progress_.end()) return nullptr;
    return &it->second;
}

std::vector<LogEntry> CommandDispatcher::getLogEntries(size_t limit) const {
    std::lock_guard<std::mutex> lock(agent_meta_mutex_);
    std::vector<LogEntry> result;
    size_t count = std::min(limit, log_entries_.size());
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        result.push_back(log_entries_[i]);
    }
    return result;
}

}  // namespace termcore
