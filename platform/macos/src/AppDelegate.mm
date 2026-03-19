#import "AppDelegate.h"
#import "TerminalView.h"
#import "ConfigWatcherMac.h"
#import "TerminalViewImpl.h"
#import "SidebarViewController.h"
#import "TerminalContentViewController.h"
#import "PreferencesWindowController.h"
#import "QuickTerminalPanel.h"
#import <Metal/Metal.h>

#include "termcore/config.h"
#include "termcore/config_diff.h"
#include "termcore/theme_loader.h"
#include "termcore/socket/socket_server.h"
#include "termcore/socket/command_dispatcher.h"
#include "termcore/socket/socket_transport.h"
#include "termcore/workspace_status.h"

@implementation AppDelegate {
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

    // Preferences
    PreferencesWindowController* _prefsController;

    // Quick Terminal (visor mode)
    QuickTerminalPanel* _quickTerminalPanel;

    // Notification observer token (must be removed on termination)
    id _reloadConfigObserver;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    // --- Load config ---
    std::string configPath = termcore::defaultConfigPath();
    termcore::Config config = termcore::parseConfigFile(configPath);
    if (!config.theme.empty()) {
        // Detect current system appearance for adaptive theme resolution
        BOOL isDark = YES;
        if (@available(macOS 10.14, *)) {
            NSAppearanceName appearance = [NSApp.effectiveAppearance
                bestMatchFromAppearancesWithNames:@[NSAppearanceNameDarkAqua, NSAppearanceNameAqua]];
            isDark = [appearance isEqualToString:NSAppearanceNameDarkAqua];
        }
        std::string resolved = termcore::resolveThemeForAppearance(config.theme, isDark);
        auto theme = termcore::findTheme(resolved);
        if (theme) termcore::applyTheme(config, *theme);
    }

    // --- Metal device ---
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        NSLog(@"BreadTerminal: Metal is not supported on this machine.");
        [NSApp terminate:nil];
        return;
    }

    // --- Window ---
    int winW = config.window_width  > 0 ? config.window_width  : 800;
    int winH = config.window_height > 0 ? config.window_height : 600;
    NSRect frame = NSMakeRect(0, 0, winW, winH);
    NSWindowStyleMask style = NSWindowStyleMaskTitled
                            | NSWindowStyleMaskClosable
                            | NSWindowStyleMaskMiniaturizable
                            | NSWindowStyleMaskResizable;

    self.mainWindow = [[NSWindow alloc] initWithContentRect:frame
                                                  styleMask:style
                                                    backing:NSBackingStoreBuffered
                                                      defer:NO];
    self.mainWindow.title = @"BreadTerminal";
    [self.mainWindow center];
    self.mainWindow.minSize = NSMakeSize(320, 240);

    // --- Background transparency ---
    if (config.background_opacity < 1.0f || config.background_blur > 0) {
        self.mainWindow.opaque = NO;
        self.mainWindow.backgroundColor = [NSColor clearColor];
    }

    // --- Terminal view (as subview of contentView) ---
    NSView* contentView = self.mainWindow.contentView;
    _terminalView = [[TerminalView alloc] initWithFrame:contentView.bounds device:device];
    [_terminalView applyConfig:config];
    _terminalView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [contentView addSubview:_terminalView];

    // --- Show & focus ---
    [NSApp activateIgnoringOtherApps:YES];
    [self.mainWindow makeKeyAndOrderFront:nil];
    [self.mainWindow makeFirstResponder:_terminalView];

    // --- Socket API server ---
    {
        auto socketPath = termcore::resolveSocketPath();
        auto transport = termcore::createSocketTransport(socketPath);

        // PaneWriteCallback: write data to the pane's PTY
        __weak TerminalView* weakTV = _terminalView;
        termcore::PaneWriteCallback writeCb = [weakTV](termcore::PaneId /*pane_id*/,
                                                        std::string_view data) -> bool {
            TerminalView* tv = weakTV;
            if (!tv) return false;
            termcore::Pty* ptyPtr = [tv pty];
            if (!ptyPtr) return false;
            ptyPtr->write(data.data(), data.size());
            return true;
        };

        // WebViewCallback: no-op placeholder
        termcore::WebViewCallback webviewCb = [](const std::string& /*method*/,
                                                  const nlohmann::json& /*params*/) {};

        // ScrollbackReadCallback: read scrollback + visible lines from the pane's Screen
        termcore::ScrollbackReadCallback scrollbackCb =
            [weakTV](termcore::PaneId /*pane_id*/, int line_count) -> std::vector<std::string> {
            TerminalView* tv = weakTV;
            if (!tv || !tv->_impl || !tv->_impl->screen) return {};

            auto* screen = tv->_impl->screen.get();
            int sb_size = static_cast<int>(screen->scrollbackSize());
            int screen_rows = screen->rows();
            int total = sb_size + screen_rows;
            int count = std::min(line_count, total);

            std::vector<std::string> result;
            result.reserve(count);

            // Read from most recent scrollback lines + visible screen
            // line 0 = most recent scrollback line (just above visible area)
            for (int i = 0; i < count; ++i) {
                if (i < sb_size) {
                    result.push_back(screen->getScrollbackLineText(i));
                } else {
                    result.push_back(screen->getLineText(i - sb_size));
                }
            }
            return result;
        };

        auto dispatcher = std::make_shared<termcore::CommandDispatcher>(
            [_terminalView mux],
            [_terminalView notifications],
            [_terminalView agentTracker],
            std::move(writeCb),
            std::move(webviewCb),
            std::move(scrollbackCb));

        _socketServer = std::make_unique<termcore::SocketServer>(
            std::move(transport), std::move(dispatcher));

        if (_socketServer->start()) {
            setenv("BREADTERMINAL_SOCKET", _socketServer->socketPath().c_str(), 1);
            NSLog(@"BreadTerminal: Socket API listening on %s",
                  _socketServer->socketPath().c_str());

            // Drain main-thread dispatch queue at ~60Hz
            __weak AppDelegate* weakDrain = self;
            _socketDrainTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60.0
                                                                repeats:YES
                                                                  block:^(NSTimer* timer) {
                AppDelegate* s = weakDrain;
                if (!s) { [timer invalidate]; return; }
                s->_socketServer->drainMainThreadQueue();
            }];
        } else {
            NSLog(@"BreadTerminal: Failed to start socket API server");
            _socketServer.reset();
        }
    }

    // --- Start shell (after socket env var is set so child inherits it) ---
    [_terminalView startShell];

    // --- Wire WorkspaceStatusProvider to sidebar ---
    _statusProvider = std::make_unique<termcore::WorkspaceStatusProvider>(
        [_terminalView mux], [_terminalView agentTracker],
        [_terminalView notifications]);

    __weak SidebarViewController* weakSidebar = _sidebarVC;
    _statusProvider->setOnChanged(
        [weakSidebar](const std::vector<termcore::WorkspaceStatusSnapshot>& snapshots) {
            auto snapshotsCopy = snapshots;
            dispatch_async(dispatch_get_main_queue(), ^{
                SidebarViewController* sidebar = weakSidebar;
                if (sidebar) {
                    [sidebar updateSnapshots:std::move(snapshotsCopy)];
                }
            });
        });

    // Initial sidebar snapshot
    auto initialSnapshots = _statusProvider->currentSnapshots();
    [_sidebarVC updateSnapshots:std::move(initialSnapshots)];

    // --- Config file watcher ---
    _configPath = configPath;
    _configWatcher = std::make_unique<termcore::ConfigWatcherMac>();

    __weak AppDelegate* weakSelf = self;
    _configWatcher->start(configPath, [weakSelf](
            const termcore::Config& new_config,
            termcore::ConfigDirtyFlags dirty,
            const std::string& error) {
        AppDelegate* strongSelf = weakSelf;
        if (!strongSelf) return;

        if (!error.empty()) {
            NSString* msg = [NSString stringWithUTF8String:error.c_str()];
            NSLog(@"BreadTerminal: config reload error: %@", msg);
            [strongSelf->_terminalView showConfigError:msg];
            return;
        }

        if (dirty != termcore::ConfigDirtyFlags::None) {
            NSLog(@"BreadTerminal: config reloaded (dirty flags: 0x%x)",
                  static_cast<uint32_t>(dirty));
            [strongSelf->_terminalView applyConfigDelta:new_config dirty:dirty];
        }
    });

    // Listen for manual reload requests (from ReloadConfig keybinding)
    _reloadConfigObserver = [[NSNotificationCenter defaultCenter]
        addObserverForName:@"BreadTerminalReloadConfig"
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification* _Nonnull note) {
        AppDelegate* strongSelf = weakSelf;
        if (strongSelf && strongSelf->_configWatcher) {
            strongSelf->_configWatcher->reloadNow();
        }
    }];

    // --- Preferences window controller ---
    _prefsController = [[PreferencesWindowController alloc]
        initWithConfigPath:_configPath
             configWatcher:_configWatcher.get()];

    // --- Quick Terminal (visor mode) ---
    if (!config.quick_terminal_hotkey.empty()) {
        _quickTerminalPanel = [[QuickTerminalPanel alloc] initWithDevice:device];
        [_quickTerminalPanel.terminalView applyConfig:config];
        [_quickTerminalPanel.terminalView startShell];
        NSString* hotkey = [NSString stringWithUTF8String:
                            config.quick_terminal_hotkey.c_str()];
        [_quickTerminalPanel registerGlobalHotkey:hotkey];
    }
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    if (_reloadConfigObserver) {
        [[NSNotificationCenter defaultCenter] removeObserver:_reloadConfigObserver];
        _reloadConfigObserver = nil;
    }

    // Destroy preferences controller first — it holds a raw IConfigWatcher* that
    // will become dangling once _configWatcher is destroyed.
    _prefsController = nil;

    // Tear down quick terminal
    if (_quickTerminalPanel) {
        [_quickTerminalPanel unregisterGlobalHotkey];
        _quickTerminalPanel = nil;
    }

    // Stop socket server
    [_socketDrainTimer invalidate];
    _socketDrainTimer = nil;
    if (_socketServer) {
        _socketServer->stop();
        _socketServer.reset();
    }

    if (_configWatcher) {
        _configWatcher->stop();
        _configWatcher.reset();
    }
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

#pragma mark - SidebarViewControllerDelegate

- (void)sidebarDidSelectWorkspace:(uint32_t)workspaceId {
    [_terminalView mux].setActiveWorkspace(workspaceId);
    if (_statusProvider) {
        _statusProvider->refresh();
    }
}

#pragma mark - Sidebar Toggle (Cmd+Shift+B)

- (IBAction)toggleSidebar:(id)sender {
    (void)sender;
    if (_splitVC.splitViewItems.count > 0) {
        NSSplitViewItem* sidebarItem = _splitVC.splitViewItems[0];
        [sidebarItem setCollapsed:!sidebarItem.isCollapsed];
    }
}

#pragma mark - Preferences

- (IBAction)openPreferences:(id)sender {
    (void)sender;
    [_prefsController showPreferences];
}

@end
