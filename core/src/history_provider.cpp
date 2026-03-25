#include "termcore/history_provider.h"
#include <algorithm>

namespace termcore {

void HistoryProvider::addEntry(const std::string& command) {
    if (command.empty()) return;
    bool allSpace = true;
    for (char c : command) {
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            allSpace = false;
            break;
        }
    }
    if (allSpace) return;

    auto it = std::find(history_.begin(), history_.end(), command);
    if (it != history_.end()) {
        history_.erase(it);
    }

    history_.insert(history_.begin(), command);

    if (history_.size() > kMaxHistory) {
        history_.pop_back();
    }
}

std::string HistoryProvider::suggest(const std::string& prefix) const {
    if (prefix.empty()) return "";
    for (const auto& entry : history_) {
        if (entry.size() > prefix.size() &&
            entry.compare(0, prefix.size(), prefix) == 0) {
            return entry;
        }
    }
    return "";
}

} // namespace termcore
