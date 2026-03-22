#include "termcore/search_history.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace termcore {

void SearchHistory::addQuery(const std::string& query) {
    if (query.empty()) return;

    // Remove duplicate if present
    auto it = std::find(entries_.begin(), entries_.end(), query);
    if (it != entries_.end()) {
        entries_.erase(it);
    }

    // Insert at front
    entries_.insert(entries_.begin(), query);

    // Trim to max capacity
    if (entries_.size() > kMaxEntries) {
        entries_.resize(kMaxEntries);
    }

    // Reset navigation since the list changed
    resetNavigation();
}

bool SearchHistory::navigateUp() {
    if (entries_.empty()) return false;

    if (nav_index_ + 1 < static_cast<int>(entries_.size())) {
        ++nav_index_;
        return true;
    }
    return false;
}

bool SearchHistory::navigateDown() {
    if (nav_index_ <= -1) return false;

    --nav_index_;
    return true;
}

std::string SearchHistory::currentEntry() const {
    if (nav_index_ >= 0 && nav_index_ < static_cast<int>(entries_.size())) {
        return entries_[nav_index_];
    }
    return {};
}

void SearchHistory::resetNavigation() {
    nav_index_ = -1;
}

void SearchHistory::clear() {
    entries_.clear();
    nav_index_ = -1;
}

void SearchHistory::saveToDisk(const std::string& path) const {
    // Ensure parent directory exists
    fs::path p(path);
    if (p.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        // Ignore errors — save will simply fail
    }

    nlohmann::json j;
    j["queries"] = entries_;

    std::ofstream ofs(path);
    if (ofs.is_open()) {
        ofs << j.dump(2);
    }
}

void SearchHistory::loadFromDisk(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return;

    try {
        nlohmann::json j = nlohmann::json::parse(ifs);
        if (j.contains("queries") && j["queries"].is_array()) {
            entries_.clear();
            for (const auto& item : j["queries"]) {
                if (item.is_string() && entries_.size() < kMaxEntries) {
                    entries_.push_back(item.get<std::string>());
                }
            }
        }
    } catch (...) {
        // Corrupted file — ignore
    }

    resetNavigation();
}

std::string SearchHistory::defaultPath() {
#ifdef __APPLE__
    const char* home = std::getenv("HOME");
    if (!home) return {};
    return std::string(home) +
           "/Library/Application Support/BreadTerminal/search_history.json";
#elif defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    if (appdata) {
        return std::string(appdata) + "/BreadTerminal/search_history.json";
    }
    const char* home = std::getenv("USERPROFILE");
    if (!home) return {};
    return std::string(home) + "/AppData/Roaming/BreadTerminal/search_history.json";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') {
        return std::string(xdg) + "/breadterminal/search_history.json";
    }
    const char* home = std::getenv("HOME");
    if (!home) return {};
    return std::string(home) + "/.config/breadterminal/search_history.json";
#endif
}

} // namespace termcore
