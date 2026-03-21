#import "AppDelegatePrivate.h"
#import "TerminalView.h"
#import "ConfigWatcherMac.h"
#import "TerminalViewImpl.h"
#import "SidebarViewController.h"
#import "TerminalContentViewController.h"
#import "PreferencesWindowController.h"
#import "UnifiedSettingsWindowController.h"
#import "QuickTerminalPanel.h"
#import <Metal/Metal.h>

#include "termcore/config.h"
#include "termcore/config_diff.h"
#include "termcore/lua_config.h"
#include "termcore/theme_loader.h"
#include "termcore/terminal_controller.h"
#include "termcore/socket/socket_server.h"
#include "termcore/socket/command_dispatcher.h"
#include "termcore/socket/socket_transport.h"
#include "termcore/workspace_status.h"

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    // --- Load config (Lua first, then legacy) ---
    std::string configPath = termcore::defaultLuaConfigPath();
    if (configPath.empty()) configPath = termcore::defaultConfigPath();
    termcore::Config config = termcore::loadConfig();
    if (!config.theme.empty()) {
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
    _config = config;
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    _device = device;
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

    self.mainWindow = [self createWindowWithFrame:frame style:style config:config device:device];
    [self.mainWindow center];

    // --- Show & focus ---
    [NSApp activateIgnoringOtherApps:YES];
    [self.mainWindow makeKeyAndOrderFront:nil];

    // Get the terminal view from the window (created by createWindowWithFrame)
    for (NSView* subview in self.mainWindow.contentView.subviews) {
        if ([subview isKindOfClass:[TerminalView class]]) {
            _terminalView = (TerminalView*)subview;
            break;
        }
    }

    // --- Socket API server ---
    {
        auto socketPath = termcore::resolveSocketPath();
        auto transport = termcore::createSocketTransport(socketPath);

        // PaneWriteCallback: write data to the pane's PTY via controller
        __weak TerminalView* weakTV = _terminalView;
        termcore::PaneWriteCallback writeCb = [weakTV](termcore::PaneId /*pane_id*/,
                                                        std::string_view data) -> bool {
            TerminalView* tv = weakTV;
            if (!tv) return false;
            termcore::TerminalController* ctrl = [tv controller];
            if (!ctrl || !ctrl->tabs()) return false;
            termcore::Pty* ptyPtr = ctrl->tabs()->activePty();
            if (!ptyPtr) return false;
            ptyPtr->write(data.data(), data.size());
            return true;
        };

        // WebViewCallback: no-op placeholder
        termcore::WebViewCallback webviewCb = [](const std::string& /*method*/,
                                                  const nlohmann::json& /*params*/) {};

        // ScrollbackReadCallback: read scrollback + visible lines from controller's screen
        termcore::ScrollbackReadCallback scrollbackCb =
            [weakTV](termcore::PaneId /*pane_id*/, int line_count) -> std::vector<std::string> {
            TerminalView* tv = weakTV;
            if (!tv) return {};
            termcore::TerminalController* ctrl = [tv controller];
            if (!ctrl) return {};

            auto* screen = ctrl->activeScreen();
            if (!screen) return {};

            int sb_size = static_cast<int>(screen->scrollbackSize());
            int screen_rows = screen->rows();
            int total = sb_size + screen_rows;
            int count = std::min(line_count, total);

            std::vector<std::string> result;
            result.reserve(count);

            for (int i = 0; i < count; ++i) {
                if (i < sb_size) {
                    result.push_back(screen->getScrollbackLineText(i));
                } else {
                    result.push_back(screen->getLineText(i - sb_size));
                }
            }
            return result;
        };

        // Mux is now owned by controller's TabController
        termcore::TerminalController* ctrl = [_terminalView controller];
        if (ctrl && ctrl->tabs()) {
            auto dispatcher = std::make_shared<termcore::CommandDispatcher>(
                *ctrl->tabs()->mux(),
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
    }

    // --- Wire WorkspaceStatusProvider to sidebar ---
    termcore::TerminalController* ctrl = [_terminalView controller];
    if (ctrl && ctrl->tabs()) {
        _statusProvider = std::make_unique<termcore::WorkspaceStatusProvider>(
            *ctrl->tabs()->mux(), [_terminalView agentTracker],
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

        auto initialSnapshots = _statusProvider->currentSnapshots();
        [_sidebarVC updateSnapshots:std::move(initialSnapshots)];
    }

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

    // Listen for OpenSettings / OpenThemeHub / OpenFontHub keybinding actions
    // All three now open the unified settings window, navigating to the appropriate section.
    _openSettingsObserver = [[NSNotificationCenter defaultCenter]
        addObserverForName:@"BreadTerminalOpenSettings"
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification* _Nonnull note) {
        AppDelegate* strongSelf = weakSelf;
        if (strongSelf) [strongSelf openUnifiedSettings:nil];
    }];
    _openThemeHubObserver = [[NSNotificationCenter defaultCenter]
        addObserverForName:@"BreadTerminalOpenThemeHub"
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification* _Nonnull note) {
        AppDelegate* strongSelf = weakSelf;
        if (strongSelf) [strongSelf openUnifiedSettingsAtTheme];
    }];
    _openFontHubObserver = [[NSNotificationCenter defaultCenter]
        addObserverForName:@"BreadTerminalOpenFontHub"
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification* _Nonnull note) {
        AppDelegate* strongSelf = weakSelf;
        if (strongSelf) [strongSelf openUnifiedSettingsAtFont];
    }];

    // --- Unified settings window controller ---
    _unifiedSettingsController = [[UnifiedSettingsWindowController alloc]
        initWithConfigPath:_configPath
             configWatcher:_configWatcher.get()];

    // --- Legacy preferences window controller (kept for backward compatibility) ---
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
    for (id observer in @[_reloadConfigObserver ?: [NSNull null],
                           _openSettingsObserver ?: [NSNull null],
                           _openThemeHubObserver ?: [NSNull null],
                           _openFontHubObserver ?: [NSNull null]]) {
        if (observer != [NSNull null]) {
            [[NSNotificationCenter defaultCenter] removeObserver:observer];
        }
    }
    _reloadConfigObserver = nil;
    _openSettingsObserver = nil;
    _openThemeHubObserver = nil;
    _openFontHubObserver = nil;

    _prefsController = nil;
    _unifiedSettingsController = nil;

    if (_quickTerminalPanel) {
        [_quickTerminalPanel unregisterGlobalHotkey];
        _quickTerminalPanel = nil;
    }

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

@end
