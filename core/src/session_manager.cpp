#include "termcore/session_manager.h"
#include "termcore/session.h"
#include "termcore/mux.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace termcore {

// --------------------------------------------------------------------------
// Construction
// --------------------------------------------------------------------------

MultiSessionManager::MultiSessionManager() = default;
MultiSessionManager::~MultiSessionManager() = default;

// --------------------------------------------------------------------------
// Directory helpers
// --------------------------------------------------------------------------

std::string MultiSessionManager::resolveSessionDir(const std::string& dir) {
    if (!dir.empty()) return dir;
    return SessionManager::defaultSessionDir();
}

std::string MultiSessionManager::buildFilePath(const std::string& dir,
                                                const std::string& name) {
    if (dir.empty() || name.empty()) return "";
    return dir + "/" + name + ".json";
}

// --------------------------------------------------------------------------
// Name generation
// --------------------------------------------------------------------------

std::string MultiSessionManager::generateSessionName() {
    auto now  = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);

    std::tm tm_buf = {};
#if defined(_WIN32)
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif

    std::ostringstream oss;
    oss << "session-"
        << std::put_time(&tm_buf, "%Y%m%d-%H%M%S");
    return oss.str();
}

// --------------------------------------------------------------------------
// List sessions
// --------------------------------------------------------------------------

std::vector<SessionInfo> MultiSessionManager::listSessions(
    const std::string& dir) {

    std::string sessionDir = resolveSessionDir(dir);
    if (sessionDir.empty()) return {};

    std::error_code ec;
    if (!fs::exists(sessionDir, ec) || ec) return {};

    std::vector<SessionInfo> results;

    for (const auto& entry : fs::directory_iterator(sessionDir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        if (ec) continue;

        auto path = entry.path();
        if (path.extension() != ".json") continue;

        // Skip the default "last.json" used by SessionManager
        if (path.filename() == "last.json") continue;

        SessionInfo info;
        info.name     = path.stem().string();
        info.filepath = path.string();

        auto ftime    = entry.last_write_time(ec);
        info.timestamp = ec
            ? 0
            : static_cast<uint64_t>(ftime.time_since_epoch().count());

        results.push_back(std::move(info));
    }

    // Sort newest first
    std::sort(results.begin(), results.end(),
              [](const SessionInfo& a, const SessionInfo& b) {
                  return a.timestamp > b.timestamp;
              });

    return results;
}

// --------------------------------------------------------------------------
// Save
// --------------------------------------------------------------------------

bool MultiSessionManager::saveSession(
    const Mux& mux,
    const IPaneStateProvider& provider,
    const std::string& path,
    const WindowGeometry& window) {

    // Determine target file path
    std::string filepath = path;
    if (filepath.empty()) {
        std::string dir = resolveSessionDir("");
        if (dir.empty()) return false;

        std::string name = generateSessionName();
        filepath = buildFilePath(dir, name);
        if (filepath.empty()) return false;
    }

    // Ensure parent directory exists
    {
        std::error_code ec;
        fs::path parent = fs::path(filepath).parent_path();
        if (!parent.empty()) {
            fs::create_directories(parent, ec);
            if (ec) return false;
        }
    }

    // Capture state using the existing SessionManager
    SessionManager sm;
    WindowGeometry win = window;
    SessionData data = sm.capture(mux, provider, win);

    // Serialize to the target directory.
    // SessionManager::save writes to sessionFilePath(dir), so we use a
    // temporary directory trick: save to the parent dir under a controlled
    // name.  However, to keep things simple and reuse the existing atomic-
    // write logic, we replicate the essential write here via the existing
    // save() by passing the parent directory and then renaming.
    //
    // Actually, SessionManager::save always writes "last.json".  Instead,
    // save to a temp location and rename.
    std::string parentDir = fs::path(filepath).parent_path().string();
    if (!sm.save(data, parentDir)) return false;

    // sm.save wrote to <parentDir>/last.json — rename to desired name
    std::string lastJson = SessionManager::sessionFilePath(parentDir);
    if (lastJson == filepath) return true;  // already correct

    std::error_code ec;
    fs::rename(lastJson, filepath, ec);
    return !ec;
}

// --------------------------------------------------------------------------
// Restore
// --------------------------------------------------------------------------

bool MultiSessionManager::restoreSession(const std::string& path,
                                          SessionData& outData) {
    if (path.empty()) return false;

    // SessionManager::load expects a directory and reads "last.json" from it.
    // To load an arbitrary file, temporarily rename/copy is messy.
    // Instead, we copy the file to last.json in the same dir, load, then
    // restore the original last.json if it existed.
    //
    // Better approach: just re-use the parent dir and temporarily swap.
    // Simplest correct approach: copy file to a temp dir as last.json, load.

    std::error_code ec;
    if (!fs::exists(path, ec) || ec) return false;

    // Create a temp directory
    fs::path tempDir = fs::temp_directory_path(ec) / "breadterminal_restore";
    if (ec) return false;
    fs::create_directories(tempDir, ec);
    if (ec) return false;

    fs::path tempFile = tempDir / "last.json";
    fs::copy_file(path, tempFile, fs::copy_options::overwrite_existing, ec);
    if (ec) return false;

    SessionManager sm;
    auto result = sm.load(tempDir.string());

    // Clean up temp
    fs::remove(tempFile, ec);
    fs::remove(tempDir, ec);

    if (!result.has_value()) return false;

    outData = std::move(result.value());
    return true;
}

// --------------------------------------------------------------------------
// Delete
// --------------------------------------------------------------------------

bool MultiSessionManager::deleteSession(const std::string& path) {
    if (path.empty()) return false;
    std::error_code ec;
    return fs::remove(path, ec) && !ec;
}

} // namespace termcore
