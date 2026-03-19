#ifndef TERMCORE_PANE_STATUS_H
#define TERMCORE_PANE_STATUS_H

#include "termcore/mux.h"

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace termcore {

struct StatusPill {
    std::string key;
    std::string value;
    std::string icon;      // icon name (for future use)
    uint32_t color = 0;    // optional custom color
};

struct LogEntry {
    enum Level { Info, Success, Warning, Error };
    Level level;
    std::string message;
    std::string source;
    std::chrono::steady_clock::time_point timestamp;
};

struct PaneStatusData {
    PaneId pane_id = kInvalidPane;
    float progress = -1.0f;        // -1 = no progress bar, 0.0-1.0 = percentage
    std::string progress_label;
    std::vector<StatusPill> status_pills;
    std::vector<LogEntry> logs;

    void setProgress(float value, const std::string& label = "");
    void clearProgress();
    void setStatus(const std::string& key, const std::string& value,
                   const std::string& icon = "");
    void clearStatus(const std::string& key);
    void addLog(LogEntry::Level level, const std::string& message,
                const std::string& source = "");
};

class PaneStatusStore {
public:
    PaneStatusData& getOrCreate(PaneId pane);
    const PaneStatusData* get(PaneId pane) const;
    void remove(PaneId pane);

private:
    std::unordered_map<PaneId, PaneStatusData> data_;
};

} // namespace termcore

#endif // TERMCORE_PANE_STATUS_H
