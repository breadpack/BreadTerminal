#ifndef BREADTERMINAL_PREFERENCES_WINDOW_CONTROLLER_H
#define BREADTERMINAL_PREFERENCES_WINDOW_CONTROLLER_H

#import <Cocoa/Cocoa.h>
#include "termcore/config.h"
#include "termcore/config_watcher.h"

/// Block signature for preference tab VCs to save config changes.
typedef void (^PrefsSaveBlock)(const termcore::Config& updated);

@interface PreferencesWindowController : NSWindowController <NSToolbarDelegate>

- (instancetype)initWithConfigPath:(const std::string&)path
                     configWatcher:(termcore::IConfigWatcher*)watcher;

/// Show (or bring to front) the preferences window.
- (void)showPreferences;

@end

#endif // BREADTERMINAL_PREFERENCES_WINDOW_CONTROLLER_H
