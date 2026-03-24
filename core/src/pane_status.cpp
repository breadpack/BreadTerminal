#include "termcore/pane_status.h"

#include <algorithm>

namespace termcore {

// --- PaneStatusData ---

void PaneStatusData::setProgress(float value, const std::string& label) {
    if (value < 0.0f) {
        progress = -1.0f;
    } else if (value > 1.0f) {
        progress = 1.0f;
    } else {
        progress = value;
    }
    progress_label = label;
}

void PaneStatusData::clearProgress() {
    progress = -1.0f;
    progress_label.clear();
}

void PaneStatusData::setStatus(const std::string& key, const std::string& value,
                               const std::string& icon) {
    for (auto& pill : status_pills) {
        if (pill.key == key) {
            pill.value = value;
            pill.icon = icon;
            return;
        }
    }
    status_pills.push_back({key, value, icon, 0});
}

void PaneStatusData::clearStatus(const std::string& key) {
    status_pills.erase(
        std::remove_if(status_pills.begin(), status_pills.end(),
                        [&](const StatusPill& p) { return p.key == key; }),
        status_pills.end());
}

void PaneStatusData::setStatusPillFromLua(const std::string& key,
                                           const std::string& value,
                                           uint32_t color) {
    for (auto& pill : status_pills) {
        if (pill.key == key) {
            pill.value = value;
            pill.color = color;
            return;
        }
    }
    status_pills.push_back({key, value, /*icon=*/"", color});
}

void PaneStatusData::addLog(LogEntry::Level level, const std::string& message,
                            const std::string& source) {
    LogEntry entry;
    entry.level = level;
    entry.message = message;
    entry.source = source;
    entry.timestamp = std::chrono::steady_clock::now();
    logs.push_back(std::move(entry));

    // Keep a reasonable cap on log entries.
    constexpr size_t kMaxLogs = 500;
    if (logs.size() > kMaxLogs) {
        logs.erase(logs.begin(), logs.begin() + static_cast<ptrdiff_t>(logs.size() - kMaxLogs));
    }
}

// --- PaneStatusStore ---

PaneStatusData& PaneStatusStore::getOrCreate(PaneId pane) {
    auto it = data_.find(pane);
    if (it != data_.end()) {
        return it->second;
    }
    auto& d = data_[pane];
    d.pane_id = pane;
    return d;
}

const PaneStatusData* PaneStatusStore::get(PaneId pane) const {
    auto it = data_.find(pane);
    if (it != data_.end()) {
        return &it->second;
    }
    return nullptr;
}

void PaneStatusStore::remove(PaneId pane) {
    data_.erase(pane);
}

} // namespace termcore
