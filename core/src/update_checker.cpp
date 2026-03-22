#include "termcore/update_checker.h"
#include "termcore/notification.h"
#include "termcore/termcore.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace termcore {

// ---------------------------------------------------------------------------
// Version comparison
// ---------------------------------------------------------------------------

namespace {

/// Split a version string into numeric parts and an optional pre-release tag.
/// "1.2.3-beta" -> parts={1,2,3}, prerelease="beta"
struct ParsedVersion {
    std::vector<int> parts;
    std::string prerelease;
};

ParsedVersion parseVersion(const std::string& v) {
    ParsedVersion pv;
    std::string core_part = v;

    // Strip leading 'v' or 'V'
    if (!core_part.empty() && (core_part[0] == 'v' || core_part[0] == 'V')) {
        core_part = core_part.substr(1);
    }

    // Split on '-' for pre-release
    auto dash = core_part.find('-');
    if (dash != std::string::npos) {
        pv.prerelease = core_part.substr(dash + 1);
        core_part = core_part.substr(0, dash);
    }

    // Split on '.' for numeric parts
    std::istringstream ss(core_part);
    std::string token;
    while (std::getline(ss, token, '.')) {
        try {
            pv.parts.push_back(std::stoi(token));
        } catch (...) {
            pv.parts.push_back(0);
        }
    }

    return pv;
}

} // anonymous namespace

int compareVersions(const std::string& a, const std::string& b) {
    auto va = parseVersion(a);
    auto vb = parseVersion(b);

    // Compare numeric parts
    size_t max_len = std::max(va.parts.size(), vb.parts.size());
    for (size_t i = 0; i < max_len; ++i) {
        int pa = (i < va.parts.size()) ? va.parts[i] : 0;
        int pb = (i < vb.parts.size()) ? vb.parts[i] : 0;
        if (pa < pb) return -1;
        if (pa > pb) return 1;
    }

    // Equal numeric parts — pre-release versions are less than release
    // (e.g., "1.2.3-beta" < "1.2.3")
    bool a_pre = !va.prerelease.empty();
    bool b_pre = !vb.prerelease.empty();

    if (a_pre && !b_pre) return -1;  // a is pre-release, b is release
    if (!a_pre && b_pre) return 1;   // a is release, b is pre-release

    // Both have pre-release or both don't — compare lexicographically
    if (a_pre && b_pre) {
        if (va.prerelease < vb.prerelease) return -1;
        if (va.prerelease > vb.prerelease) return 1;
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Manifest parsing
// ---------------------------------------------------------------------------

bool parseUpdateManifest(const std::string& json_str, UpdateManifest& out) {
    try {
        auto j = nlohmann::json::parse(json_str);

        if (j.contains("version") && j["version"].is_string()) {
            out.version = j["version"].get<std::string>();
        } else {
            return false; // version is required
        }

        if (j.contains("url") && j["url"].is_string()) {
            out.url = j["url"].get<std::string>();
        }

        if (j.contains("notes") && j["notes"].is_string()) {
            out.notes = j["notes"].get<std::string>();
        }

        if (j.contains("sha256") && j["sha256"].is_string()) {
            out.sha256 = j["sha256"].get<std::string>();
        }

        return true;
    } catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------------------
// UpdateChecker
// ---------------------------------------------------------------------------

UpdateChecker::UpdateChecker()
    : manifest_url_(kDefaultManifestUrl) {}

void UpdateChecker::setCheckInterval(int hours) {
    if (hours > 0) {
        check_interval_hours_ = hours;
    }
}

std::string UpdateChecker::currentVersion() {
    return termcore_version();
}

bool UpdateChecker::shouldCheck() const {
    if (config_dir_.empty()) {
        return true;
    }

    auto last = loadLastCheckTime();
    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::hours>(now - last);

    return elapsed.count() >= check_interval_hours_;
}

void UpdateChecker::markChecked() {
    saveLastCheckTime(std::chrono::system_clock::now());
}

void UpdateChecker::setManifestData(const std::string& json) {
    UpdateManifest m;
    if (!parseUpdateManifest(json, m)) {
        update_available_ = false;
        return;
    }

    manifest_ = m;
    std::string current = currentVersion();

    // Update is available if manifest version is newer than current
    update_available_ = (compareVersions(current, m.version) < 0);
}

void UpdateChecker::notifyIfAvailable(NotificationStore* store) {
    if (!store || !update_available_ || already_notified_) {
        return;
    }

    std::string current = currentVersion();
    std::string body = "BreadTerminal v" + manifest_.version +
                       " is available. Current: v" + current;
    if (!manifest_.url.empty()) {
        body += "\n" + manifest_.url;
    }

    store->add(0, NotificationSource::System, NotificationUrgency::Normal,
               "Update Available", body);

    already_notified_ = true;
}

// ---------------------------------------------------------------------------
// Timestamp persistence
// ---------------------------------------------------------------------------

std::string UpdateChecker::timestampFilePath() const {
    if (config_dir_.empty()) return "";
    namespace fs = std::filesystem;
    return (fs::path(config_dir_) / "last_update_check").string();
}

std::chrono::system_clock::time_point UpdateChecker::loadLastCheckTime() const {
    std::string path = timestampFilePath();
    if (path.empty()) {
        return std::chrono::system_clock::time_point{};
    }

    std::ifstream f(path);
    if (!f.is_open()) {
        return std::chrono::system_clock::time_point{};
    }

    int64_t epoch_seconds = 0;
    f >> epoch_seconds;
    if (f.fail()) {
        return std::chrono::system_clock::time_point{};
    }

    return std::chrono::system_clock::time_point{
        std::chrono::seconds(epoch_seconds)};
}

void UpdateChecker::saveLastCheckTime(
    std::chrono::system_clock::time_point tp) const {
    std::string path = timestampFilePath();
    if (path.empty()) return;

    namespace fs = std::filesystem;
    fs::create_directories(config_dir_);

    std::ofstream f(path);
    if (!f.is_open()) return;

    auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
                     tp.time_since_epoch())
                     .count();
    f << epoch;
}

} // namespace termcore
