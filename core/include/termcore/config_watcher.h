#ifndef TERMCORE_CONFIG_WATCHER_H
#define TERMCORE_CONFIG_WATCHER_H

#include "termcore/config.h"
#include "termcore/config_diff.h"
#include <functional>
#include <string>

namespace termcore {

/// Callback invoked when config is reloaded.
/// @param new_config  The newly parsed config
/// @param dirty       Flags indicating which groups changed
/// @param error       Non-empty if parsing failed (new_config may be partial)
using ConfigReloadCallback = std::function<void(
    const Config& new_config, ConfigDirtyFlags dirty, const std::string& error)>;

/// Abstract interface for file-system config watchers.
/// Platform-specific implementations (kqueue, inotify, etc.) derive from this.
class IConfigWatcher {
public:
    virtual ~IConfigWatcher() = default;

    /// Start watching the given config file path.
    virtual void start(const std::string& path, ConfigReloadCallback callback) = 0;

    /// Stop watching. Safe to call if not started.
    virtual void stop() = 0;

    /// Force an immediate reload (e.g. triggered by keybinding).
    virtual void reloadNow() = 0;
};

} // namespace termcore

#endif
