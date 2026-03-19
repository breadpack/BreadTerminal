#ifndef BREADTERMINAL_CONFIG_WATCHER_MAC_H
#define BREADTERMINAL_CONFIG_WATCHER_MAC_H

#include "termcore/config_watcher.h"
#include <dispatch/dispatch.h>

namespace termcore {

class ConfigWatcherMac : public IConfigWatcher {
public:
    ConfigWatcherMac();
    ~ConfigWatcherMac() override;

    void start(const std::string& path, ConfigReloadCallback callback) override;
    void stop() override;
    void reloadNow() override;

private:
    void openAndWatch();
    void doReload();

    std::string path_;
    ConfigReloadCallback callback_;
    Config last_good_config_;
    int fd_ = -1;
    dispatch_source_t source_ = nullptr;
    dispatch_source_t debounce_timer_ = nullptr;
    bool running_ = false;
};

} // namespace termcore

#endif // BREADTERMINAL_CONFIG_WATCHER_MAC_H
