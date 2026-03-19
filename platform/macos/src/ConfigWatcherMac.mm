#import "ConfigWatcherMac.h"

#include "termcore/config.h"
#include "termcore/config_diff.h"

#include <fcntl.h>
#include <unistd.h>
#include <dispatch/dispatch.h>

namespace termcore {

ConfigWatcherMac::ConfigWatcherMac() = default;

ConfigWatcherMac::~ConfigWatcherMac() {
    stop();
}

void ConfigWatcherMac::start(const std::string& path, ConfigReloadCallback callback) {
    stop();
    path_ = path;
    callback_ = std::move(callback);
    running_ = true;

    // Parse initial config as baseline for diffing
    try {
        last_good_config_ = parseConfigFile(path_);
        if (!last_good_config_.theme.empty()) {
            auto* theme = getBuiltinTheme(last_good_config_.theme);
            if (theme) applyTheme(last_good_config_, *theme);
        }
    } catch (...) {
        // If initial parse fails, start with default config
        last_good_config_ = Config{};
    }

    openAndWatch();
}

void ConfigWatcherMac::stop() {
    running_ = false;

    if (debounce_timer_) {
        dispatch_source_cancel(debounce_timer_);
        debounce_timer_ = nullptr;
    }

    if (source_) {
        dispatch_source_cancel(source_);
        source_ = nullptr;
    }

    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }

    callback_ = nullptr;
}

void ConfigWatcherMac::reloadNow() {
    if (!running_ || !callback_) return;
    doReload();
}

void ConfigWatcherMac::openAndWatch() {
    if (!running_) return;

    fd_ = open(path_.c_str(), O_EVTONLY);
    if (fd_ < 0) {
        // File may not exist yet; retry a few times
        for (int attempt = 0; attempt < 5 && fd_ < 0; ++attempt) {
            usleep(50000); // 50ms
            fd_ = open(path_.c_str(), O_EVTONLY);
        }
        if (fd_ < 0) {
            if (callback_) {
                callback_(last_good_config_, ConfigDirtyFlags::None,
                          "Failed to open config file: " + path_);
            }
            return;
        }
    }

    unsigned long mask = DISPATCH_VNODE_WRITE | DISPATCH_VNODE_DELETE | DISPATCH_VNODE_RENAME;
    source_ = dispatch_source_create(DISPATCH_SOURCE_TYPE_VNODE,
                                     static_cast<uintptr_t>(fd_),
                                     mask,
                                     dispatch_get_main_queue());
    if (!source_) {
        close(fd_);
        fd_ = -1;
        return;
    }

    // Prevent captures from preventing destruction
    __block ConfigWatcherMac* watcher = this;

    dispatch_source_set_event_handler(source_, ^{
        if (!watcher->running_) return;

        unsigned long flags = dispatch_source_get_data(watcher->source_);

        // Handle file deletion or rename: re-open the file
        if (flags & (DISPATCH_VNODE_DELETE | DISPATCH_VNODE_RENAME)) {
            // Cancel old source and close fd
            if (watcher->source_) {
                dispatch_source_cancel(watcher->source_);
                watcher->source_ = nullptr;
            }
            if (watcher->fd_ >= 0) {
                close(watcher->fd_);
                watcher->fd_ = -1;
            }
            // Re-open with retry (editor may atomically replace files)
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC),
                           dispatch_get_main_queue(), ^{
                if (watcher->running_) {
                    watcher->openAndWatch();
                    // Also trigger a reload since the file was replaced
                    watcher->doReload();
                }
            });
            return;
        }

        // Debounce writes: cancel existing timer, schedule new one at 150ms
        if (watcher->debounce_timer_) {
            dispatch_source_cancel(watcher->debounce_timer_);
            watcher->debounce_timer_ = nullptr;
        }

        watcher->debounce_timer_ = dispatch_source_create(
            DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
        if (watcher->debounce_timer_) {
            dispatch_source_set_timer(watcher->debounce_timer_,
                                      dispatch_time(DISPATCH_TIME_NOW, 150 * NSEC_PER_MSEC),
                                      DISPATCH_TIME_FOREVER, 10 * NSEC_PER_MSEC);
            dispatch_source_set_event_handler(watcher->debounce_timer_, ^{
                if (watcher->running_) {
                    watcher->doReload();
                }
                if (watcher->debounce_timer_) {
                    dispatch_source_cancel(watcher->debounce_timer_);
                    watcher->debounce_timer_ = nullptr;
                }
            });
            dispatch_resume(watcher->debounce_timer_);
        }
    });

    dispatch_source_set_cancel_handler(source_, ^{
        // Cleanup handled in stop()
    });

    dispatch_resume(source_);
}

void ConfigWatcherMac::doReload() {
    if (!callback_) return;

    try {
        Config new_config = parseConfigFile(path_);
        if (!new_config.theme.empty()) {
            auto* theme = getBuiltinTheme(new_config.theme);
            if (theme) applyTheme(new_config, *theme);
        }

        ConfigDirtyFlags dirty = diffConfig(last_good_config_, new_config);
        if (dirty != ConfigDirtyFlags::None) {
            last_good_config_ = new_config;
            callback_(new_config, dirty, "");
        }
    } catch (const std::exception& e) {
        // On parse error, report error with last good config
        callback_(last_good_config_, ConfigDirtyFlags::None, e.what());
    } catch (...) {
        callback_(last_good_config_, ConfigDirtyFlags::None,
                  "Unknown error parsing config file");
    }
}

} // namespace termcore
