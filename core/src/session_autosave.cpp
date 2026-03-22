#include "termcore/session_autosave.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace termcore {

// ---------------------------------------------------------------------------
// Recovery file path
// ---------------------------------------------------------------------------

std::string SessionAutoSave::recoveryFilePath(const std::string& dir) {
    if (dir.empty()) return "";
    return dir + "/recovery.json";
}

// ---------------------------------------------------------------------------
// JSON serialization helpers (lightweight, no scrollback compression)
// ---------------------------------------------------------------------------

static json splitNodeToJson(const SplitNodeData* node) {
    if (!node) return nullptr;
    json nj;
    nj["is_leaf"] = node->is_leaf;
    if (node->is_leaf) {
        nj["serial"] = node->leaf_serial;
    } else {
        nj["direction"] = node->direction;
        nj["ratio"] = node->ratio;
        nj["first"] = splitNodeToJson(node->first.get());
        nj["second"] = splitNodeToJson(node->second.get());
    }
    return nj;
}

static json sessionDataToJson(const SessionData& data) {
    json j;
    j["version"] = data.version;
    j["recovery"] = true;

    // Timestamp (seconds since epoch)
    auto now = std::chrono::system_clock::now();
    j["saved_at"] = std::chrono::duration_cast<std::chrono::seconds>(
                        now.time_since_epoch())
                        .count();

    j["window"] = {{"x", data.window.x},
                   {"y", data.window.y},
                   {"width", data.window.width},
                   {"height", data.window.height}};
    j["active_workspace_index"] = data.active_workspace_index;

    // Panes (skip scrollback for speed — recovery focuses on structure)
    json panes_j = json::array();
    for (const auto& p : data.panes) {
        json pj;
        pj["serial"] = p.serial;
        pj["working_dir"] = p.working_dir;
        pj["title"] = p.title;
        pj["rows"] = p.rows;
        pj["cols"] = p.cols;
        pj["is_webview"] = p.is_webview;
        pj["webview_url"] = p.webview_url;
        pj["profile_id"] = p.profile_id;
        panes_j.push_back(std::move(pj));
    }
    j["panes"] = panes_j;

    // Workspaces
    json workspaces_j = json::array();
    for (const auto& ws : data.workspaces) {
        json wj;
        wj["name"] = ws.name;
        wj["active_tab_index"] = ws.active_tab_index;

        json tabs_j = json::array();
        for (const auto& tab : ws.tabs) {
            json tj;
            tj["title"] = tab.title;
            tj["active_pane_serial"] = tab.active_pane_serial;
            tj["root"] = splitNodeToJson(tab.root.get());
            tabs_j.push_back(std::move(tj));
        }
        wj["tabs"] = tabs_j;
        workspaces_j.push_back(std::move(wj));
    }
    j["workspaces"] = workspaces_j;

    return j;
}

// ---------------------------------------------------------------------------
// JSON deserialization helpers
// ---------------------------------------------------------------------------

static std::unique_ptr<SplitNodeData> parseSplitNode(const json& j) {
    if (j.is_null()) return nullptr;
    auto node = std::make_unique<SplitNodeData>();
    node->is_leaf = j.value("is_leaf", true);
    if (node->is_leaf) {
        node->leaf_serial = j.value("serial", 0u);
    } else {
        node->direction = j.value("direction", 0);
        node->ratio = j.value("ratio", 0.5f);
        if (j.contains("first"))
            node->first = parseSplitNode(j["first"]);
        if (j.contains("second"))
            node->second = parseSplitNode(j["second"]);
    }
    return node;
}

static std::optional<SessionData> parseRecoveryJson(const json& j) {
    SessionData data;
    data.version = j.value("version", 0);
    if (data.version != 1 && data.version != 2) return std::nullopt;

    if (j.contains("window")) {
        auto& wj = j["window"];
        data.window.x = wj.value("x", 0);
        data.window.y = wj.value("y", 0);
        data.window.width = wj.value("width", 800);
        data.window.height = wj.value("height", 600);
    }

    data.active_workspace_index = j.value("active_workspace_index", size_t(0));

    if (j.contains("panes") && j["panes"].is_array()) {
        for (auto& pj : j["panes"]) {
            PaneSessionData pd;
            pd.serial = pj.value("serial", 0u);
            pd.working_dir = pj.value("working_dir", "");
            pd.title = pj.value("title", "");
            pd.rows = pj.value("rows", 24);
            pd.cols = pj.value("cols", 80);
            pd.is_webview = pj.value("is_webview", false);
            pd.webview_url = pj.value("webview_url", "");
            pd.profile_id = pj.value("profile_id", "");
            data.panes.push_back(std::move(pd));
        }
    }

    if (j.contains("workspaces") && j["workspaces"].is_array()) {
        for (auto& wj : j["workspaces"]) {
            WorkspaceSessionData ws;
            ws.name = wj.value("name", "");
            ws.active_tab_index = wj.value("active_tab_index", size_t(0));
            if (wj.contains("tabs") && wj["tabs"].is_array()) {
                for (auto& tj : wj["tabs"]) {
                    TabSessionData tab;
                    tab.title = tj.value("title", "");
                    tab.active_pane_serial = tj.value("active_pane_serial", 0u);
                    if (tj.contains("root") && !tj["root"].is_null())
                        tab.root = parseSplitNode(tj["root"]);
                    ws.tabs.push_back(std::move(tab));
                }
            }
            data.workspaces.push_back(std::move(ws));
        }
    }

    return data;
}

// ---------------------------------------------------------------------------
// Atomic write helper
// ---------------------------------------------------------------------------

static bool atomicWriteFile(const std::string& filepath,
                            const std::string& content) {
    std::string tmp_path = filepath + ".tmp";

#if defined(_WIN32)
    int fd = -1;
    _sopen_s(&fd, tmp_path.c_str(), _O_CREAT | _O_WRONLY | _O_TRUNC,
              _SH_DENYWR, _S_IREAD | _S_IWRITE);
    if (fd < 0) return false;
    size_t total = 0;
    while (total < content.size()) {
        int n = _write(fd, content.data() + total,
                       static_cast<unsigned>(content.size() - total));
        if (n < 0) {
            _close(fd);
            std::error_code ec;
            fs::remove(tmp_path, ec);
            return false;
        }
        total += static_cast<size_t>(n);
    }
    _close(fd);
#else
    int fd = ::open(tmp_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
    if (fd < 0) return false;
    size_t total = 0;
    while (total < content.size()) {
        ssize_t n = ::write(fd, content.data() + total, content.size() - total);
        if (n < 0) {
            ::close(fd);
            ::unlink(tmp_path.c_str());
            return false;
        }
        total += static_cast<size_t>(n);
    }
    ::close(fd);
#endif

    std::error_code ec;
    fs::rename(tmp_path, filepath, ec);
    if (ec) {
        fs::remove(tmp_path, ec);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// SessionAutoSave implementation
// ---------------------------------------------------------------------------

SessionAutoSave::SessionAutoSave() = default;

SessionAutoSave::~SessionAutoSave() {
    stop();
}

void SessionAutoSave::start(const AutoSaveConfig& config,
                            StateProvider provider) {
    stop();  // Ensure any previous timer is stopped

    if (!config.enabled) return;

    save_path_ = config.save_path;
    interval_seconds_ = config.interval_seconds;
    provider_ = std::move(provider);

    if (save_path_.empty() || !provider_) return;

    // Ensure directory exists
    std::error_code ec;
    fs::create_directories(save_path_, ec);
    if (ec) return;

    running_.store(true);
    thread_ = std::thread(&SessionAutoSave::timerLoop, this);
}

void SessionAutoSave::stop() {
    if (!running_.load()) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_.store(false);
    }
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void SessionAutoSave::saveNow(const SessionData& state) {
    if (save_path_.empty()) return;

    std::error_code ec;
    fs::create_directories(save_path_, ec);
    if (ec) return;

    std::string filepath = recoveryFilePath(save_path_);
    json j = sessionDataToJson(state);
    std::string content = j.dump(2);
    atomicWriteFile(filepath, content);
}

std::optional<SessionData> SessionAutoSave::loadLastSession(
    const std::string& path) {
    std::string filepath = recoveryFilePath(path);
    if (filepath.empty()) return std::nullopt;

    std::error_code ec;
    if (!fs::exists(filepath, ec) || ec) return std::nullopt;

    try {
        std::ifstream ifs(filepath, std::ios::binary);
        if (!ifs) return std::nullopt;

        json j = json::parse(ifs);
        return parseRecoveryJson(j);
    } catch (...) {
        return std::nullopt;
    }
}

bool SessionAutoSave::hasRecoverableSession(const std::string& path) {
    std::string filepath = recoveryFilePath(path);
    if (filepath.empty()) return false;
    std::error_code ec;
    return fs::exists(filepath, ec) && !ec;
}

void SessionAutoSave::clearRecoveryFile(const std::string& path) {
    std::string filepath = recoveryFilePath(path);
    if (filepath.empty()) return;
    std::error_code ec;
    fs::remove(filepath, ec);
    // Also remove any leftover .tmp file
    fs::remove(filepath + ".tmp", ec);
}

std::optional<RecoveryInfo> SessionAutoSave::getRecoveryInfo(
    const std::string& path) {
    std::string filepath = recoveryFilePath(path);
    if (filepath.empty()) return std::nullopt;

    std::error_code ec;
    if (!fs::exists(filepath, ec) || ec) return std::nullopt;

    try {
        std::ifstream ifs(filepath, std::ios::binary);
        if (!ifs) return std::nullopt;

        json j = json::parse(ifs);

        RecoveryInfo info;

        // Count tabs across all workspaces
        if (j.contains("workspaces") && j["workspaces"].is_array()) {
            for (auto& wj : j["workspaces"]) {
                if (wj.contains("tabs") && wj["tabs"].is_array()) {
                    info.tab_count +=
                        static_cast<int>(wj["tabs"].size());
                }
            }
        }

        // Count panes
        if (j.contains("panes") && j["panes"].is_array()) {
            info.pane_count = static_cast<int>(j["panes"].size());
        }

        // Timestamp
        if (j.contains("saved_at")) {
            int64_t epoch_secs = j["saved_at"].get<int64_t>();
            info.saved_at = std::chrono::system_clock::time_point(
                std::chrono::seconds(epoch_secs));
        }

        return info;
    } catch (...) {
        return std::nullopt;
    }
}

void SessionAutoSave::timerLoop() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::seconds(interval_seconds_), [this] {
            return !running_.load();
        });

        if (!running_.load()) break;

        // Capture state via provider and save
        if (provider_) {
            try {
                SessionData state = provider_();
                lock.unlock();  // Don't hold lock during I/O
                saveNow(state);
            } catch (...) {
                // Silently ignore errors in auto-save
            }
        }
    }
}

}  // namespace termcore
