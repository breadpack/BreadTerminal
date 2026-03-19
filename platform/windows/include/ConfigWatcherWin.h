#pragma once
#if defined(_WIN32)

#include "termcore/config_watcher.h"
#include <windows.h>
#include <thread>
#include <atomic>
#include <string>

namespace termcore {

class ConfigWatcherWin : public IConfigWatcher {
public:
    ConfigWatcherWin() = default;
    ~ConfigWatcherWin() override;

    void start(const std::string& path, ConfigReloadCallback callback) override;
    void stop() override;
    void reloadNow() override;

private:
    void watchThread();

    std::string config_path_;
    std::string watch_dir_;
    std::string watch_filename_;
    ConfigReloadCallback callback_;

    std::thread watch_thread_;
    std::atomic<bool> running_{false};
    HANDLE stop_event_ = INVALID_HANDLE_VALUE;

    Config last_config_;
};

} // namespace termcore

#endif // _WIN32
