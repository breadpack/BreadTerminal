#ifndef BREADTERMINAL_UNIFIED_SETTINGS_WINDOW_CONTROLLER_H
#define BREADTERMINAL_UNIFIED_SETTINGS_WINDOW_CONTROLLER_H

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>

#include "termcore/config.h"
#include "termcore/config_watcher.h"
#include "termcore/settings_model.h"
#include "termcore/font_index.h"
#include "termcore/theme_index.h"

/// Block signature for saving config changes from the unified settings window.
typedef void (^UnifiedSettingsSaveBlock)(const termcore::Config& updated);

// ---- Forward declarations for section views ----
@class UnifiedSettingsSidebar;
@class UnifiedSettingsContent;
@class UnifiedSettingsThemeCards;
@class UnifiedSettingsFontCards;

/// Unified settings window controller — VSCode-style sidebar + content layout.
/// Replaces PreferencesWindowController, ThemeHubViewController, FontHubViewController.
@interface UnifiedSettingsWindowController : NSWindowController <NSSplitViewDelegate>

- (instancetype)initWithConfigPath:(const std::string&)path
                     configWatcher:(termcore::IConfigWatcher*)watcher;

/// Show (or bring to front) the unified settings window.
- (void)showSettings;

/// Navigate to a specific category (e.g. "appearance.theme").
- (void)navigateToCategory:(const std::string&)categoryId;

/// Access the current config (for section views).
@property (nonatomic, readonly) termcore::Config& config;
@property (nonatomic, readonly) termcore::SettingsModel* settingsModel;
@property (nonatomic, readonly) termcore::FontIndex& fontIndex;
@property (nonatomic, readonly) termcore::ThemeIndex& themeIndex;
@property (nonatomic, copy) UnifiedSettingsSaveBlock saveBlock;

/// Called by section views when a config value changes.
- (void)configDidChange;

/// Called by sidebar when a category is selected.
- (void)didSelectCategory:(const std::string&)categoryId;

/// Called when search text changes.
- (void)searchTextDidChange:(NSString*)query;

@end

#endif // __APPLE__
#endif // BREADTERMINAL_UNIFIED_SETTINGS_WINDOW_CONTROLLER_H
