#import "AppDelegate.h"
#import "TerminalView.h"
#import "ConfigWatcherMac.h"
#import "TerminalViewImpl.h"
#import "SidebarViewController.h"
#import "TerminalContentViewController.h"
#import "PreferencesWindowController.h"
#import <Metal/Metal.h>

#include "termcore/config.h"
#include "termcore/config_diff.h"
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
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    // --- Load config ---
    std::string configPath = termcore::defaultConfigPath();
    termcore::Config config = termcore::parseConfigFile(configPath);
    if (!config.theme.empty()) {
        auto* theme = termcore::getBuiltinTheme(config.theme);
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

    // --- Sidebar ---
    _sidebarVC = [[SidebarViewController alloc] init];
    _sidebarVC.delegate = self;

    // --- Terminal content (wrapped in a view controller) ---
    _contentVC = [[TerminalContentViewController alloc] initWithDevice:device];
    [_contentVC applyConfig:config];
    _terminalView = _contentVC.terminalView;

    // --- NSSplitViewController ---
    _splitVC = [[NSSplitViewController alloc] init];

    NSSplitViewItem* sidebarItem =
        [NSSplitViewItem sidebarWithViewController:_sidebarVC];
    sidebarItem.minimumThickness = 180;
    sidebarItem.maximumThickness = 320;
    sidebarItem.canCollapse = YES;
    sidebarItem.collapsed = YES;  // Hidden by default, toggle with Cmd+Shift+B
    sidebarItem.holdingPriority = NSLayoutPriorityDefaultLow + 1;

    NSSplitViewItem* contentItem =
        [NSSplitViewItem contentListWithViewController:_contentVC];
    contentItem.minimumThickness = 300;

    [_splitVC addSplitViewItem:sidebarItem];
    [_splitVC addSplitViewItem:contentItem];

    self.mainWindow.contentViewController = _splitVC;

    // --- Show & focus ---
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

        auto dispatcher = std::make_shared<termcore::CommandDispatcher>(
            [_terminalView mux],
            [_terminalView notifications],
            [_terminalView agentTracker],
            std::move(writeCb),
            std::move(webviewCb));

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
    [[NSNotificationCenter defaultCenter]
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
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    [[NSNotificationCenter defaultCenter] removeObserver:self];

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
