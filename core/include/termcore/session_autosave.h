#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "termcore/session.h"

namespace termcore {

/// Configuration for automatic session persistence.
struct AutoSaveConfig {
    bool enabled = true;
    int interval_seconds = 30;
    std::string save_path;  // Directory for the recovery file
};

/// Summary information extracted from a recovery file without full deserialization.
struct RecoveryInfo {
    int tab_count = 0;
    int pane_count = 0;
    std::chrono::system_clock::time_point saved_at;
};

/// Periodically saves session state to disk for crash recovery.
///
/// Uses atomic writes (write to .tmp then rename) to prevent corruption.
/// On normal exit, clearRecoveryFile() should be called to remove the file.
class SessionAutoSave {
public:
    /// Callback type: invoked on the timer thread to obtain current state.
    using StateProvider = std::function<SessionData()>;

    SessionAutoSave();
    ~SessionAutoSave();

    // Non-copyable, non-movable
    SessionAutoSave(const SessionAutoSave&) = delete;
    SessionAutoSave& operator=(const SessionAutoSave&) = delete;

    /// Begin periodic saving.  `provider` is called every interval to get state.
    void start(const AutoSaveConfig& config, StateProvider provider);

    /// Stop the auto-save timer thread.
    void stop();

    /// Immediately save the given state to the recovery file.
    void saveNow(const SessionData& state);

    /// Load the last saved session from the recovery file.
    static std::optional<SessionData> loadLastSession(const std::string& path);

    /// Check whether a recoverable session file exists.
    static bool hasRecoverableSession(const std::string& path);

    /// Remove the recovery file (call on normal exit).
    static void clearRecoveryFile(const std::string& path);

    /// Extract summary info from a recovery file without full deserialization.
    static std::optional<RecoveryInfo> getRecoveryInfo(const std::string& path);

    /// Full path to the recovery file within the given directory.
    static std::string recoveryFilePath(const std::string& dir);

private:
    void timerLoop();

    std::string save_path_;
    int interval_seconds_ = 30;
    StateProvider provider_;

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
};

}  // namespace termcore
