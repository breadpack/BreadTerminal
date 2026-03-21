#ifndef BREADTERMINAL_APP_DELEGATE_PRIVATE_H
#define BREADTERMINAL_APP_DELEGATE_PRIVATE_H

// Private class extension for AppDelegate, shared between AppDelegate.mm
// and AppDelegateWindows.mm.

#import "AppDelegate.h"
#import <Metal/Metal.h>

#include "termcore/config.h"
#include "termcore/workspace_status.h"
#include "termcore/socket/socket_server.h"

@class TerminalView;
@class ConfigWatcherMac;
@class SidebarViewController;
@class TerminalContentViewController;
@class PreferencesWindowController;
@class UnifiedSettingsWindowController;
@class QuickTerminalPanel;

namespace termcore { class ConfigWatcherMac; }

// Block type for preferences save callback
typedef void(^PrefsSaveBlock)(const termcore::Config&);

@interface AppDelegate () {
@public
    TerminalView* _terminalView;
    std::unique_ptr<termcore::ConfigWatcherMac> _configWatcher;
    std::string _configPath;
    std::unique_ptr<termcore::SocketServer> _socketServer;
    NSTimer* _socketDrainTimer;

    // Sidebar / split view
    NSSplitViewController* _splitVC;
    SidebarViewController* _sidebarVC;
    TerminalContentViewController* _contentVC;
    std::unique_ptr<termcore::WorkspaceStatusProvider> _statusProvider;

    // Preferences (legacy)
    PreferencesWindowController* _prefsController;

    // Unified settings window
    UnifiedSettingsWindowController* _unifiedSettingsController;

    // Theme Hub / Font Hub standalone windows (legacy)
    NSWindowController* _themeHubWindowController;
    NSWindowController* _fontHubWindowController;

    // Quick Terminal (visor mode)
    QuickTerminalPanel* _quickTerminalPanel;

    // Notification observer tokens
    id _reloadConfigObserver;
    id _openSettingsObserver;
    id _openThemeHubObserver;
    id _openFontHubObserver;

    // Saved for creating new tabs
    id<MTLDevice> _device;
    termcore::Config _config;
}

// Window creation helper
- (NSWindow*)createWindowWithFrame:(NSRect)frame
                             style:(NSWindowStyleMask)style
                            config:(const termcore::Config&)config
                            device:(id<MTLDevice>)device;

// Settings navigation helpers
- (void)openUnifiedSettingsAtTheme;
- (void)openUnifiedSettingsAtFont;

// Save block factory
- (PrefsSaveBlock)makeSaveBlock;

// Tab management
- (IBAction)selectTabByNumber:(id)sender;

@end

#endif // BREADTERMINAL_APP_DELEGATE_PRIVATE_H
