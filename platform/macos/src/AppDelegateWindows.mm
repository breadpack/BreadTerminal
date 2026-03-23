#import "AppDelegatePrivate.h"
#import "TerminalView.h"
#import "TerminalViewImpl.h"
#import "SidebarViewController.h"
#import "PreferencesWindowController.h"
#import "ThemeHubViewController.h"
#import "FontHubViewController.h"
#import "UnifiedSettingsWindowController.h"

#include "termcore/config.h"
#include "termcore/lua_config.h"
#include "termcore/theme_loader.h"
#include "termcore/terminal_controller.h"
#include "termcore/workspace_status.h"

@implementation AppDelegate (Windows)

#pragma mark - Window/Tab creation

- (NSWindow*)createWindowWithFrame:(NSRect)frame
                             style:(NSWindowStyleMask)style
                            config:(const termcore::Config&)config
                            device:(id<MTLDevice>)device {
    NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                   styleMask:style
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    window.title = @"BreadTerminal";
    window.minSize = NSMakeSize(320, 240);
    window.tabbingMode = NSWindowTabbingModePreferred;
    window.tabbingIdentifier = @"BreadTerminalTabs";

    // Background transparency
    if (config.background_opacity < 1.0f || config.background_blur_material != "none") {
        window.opaque = NO;
        window.backgroundColor = [NSColor clearColor];
    }

    // Terminal view
    NSView* contentView = window.contentView;
    TerminalView* termView = [[TerminalView alloc] initWithFrame:contentView.bounds device:device];
    [termView applyConfig:config];
    termView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [contentView addSubview:termView];
    [termView startShell];
    [window makeFirstResponder:termView];

    return window;
}

- (IBAction)newTab:(id)sender {
    (void)sender;
    // Use controller's tab creation if available
    if (_terminalView && [_terminalView controller]) {
        termcore::TerminalController* ctrl = [_terminalView controller];
        if (ctrl->tabs()) {
            ctrl->tabs()->createTab(ctrl->termRows(), ctrl->termCols());
            [_terminalView setNeedsRender];
            return;
        }
    }

    // Fallback: create a new OS-level tabbed window
    NSWindow* keyWindow = [NSApp keyWindow];
    if (!keyWindow) keyWindow = self.mainWindow;

    NSRect frame = keyWindow.frame;
    NSWindowStyleMask style = keyWindow.styleMask;
    NSWindow* newWindow = [self createWindowWithFrame:frame style:style config:_config device:_device];
    [keyWindow addTabbedWindow:newWindow ordered:NSWindowAbove];
    [newWindow makeKeyAndOrderFront:nil];

    for (NSView* subview in newWindow.contentView.subviews) {
        if ([subview isKindOfClass:[TerminalView class]]) {
            [newWindow makeFirstResponder:subview];
            break;
        }
    }
}

- (IBAction)closeTab:(id)sender {
    (void)sender;
    // Use controller's tab closing if available
    if (_terminalView && [_terminalView controller]) {
        termcore::TerminalController* ctrl = [_terminalView controller];
        if (ctrl->tabs() && ctrl->tabCount() > 1) {
            ctrl->tabs()->closeTab();
            [_terminalView setNeedsRender];
            return;
        }
    }

    NSWindow* keyWindow = [NSApp keyWindow];
    if (keyWindow) {
        [keyWindow close];
    }
}

- (IBAction)selectTabByNumber:(id)sender {
    NSInteger index = [sender tag] - 1;  // tag is 1-based
    // Use controller's tab switching if available
    if (_terminalView && [_terminalView controller]) {
        termcore::TerminalController* ctrl = [_terminalView controller];
        if (ctrl->tabs()) {
            ctrl->tabs()->switchToTab(static_cast<int>(index));
            [_terminalView setNeedsRender];
            return;
        }
    }

    NSWindow* keyWindow = [NSApp keyWindow];
    if (!keyWindow) return;
    NSArray<NSWindow*>* tabs = keyWindow.tabbedWindows;
    if (tabs && index >= 0 && index < (NSInteger)tabs.count) {
        [tabs[index] makeKeyAndOrderFront:nil];
    }
}

#pragma mark - SidebarViewControllerDelegate

- (void)sidebarDidSelectWorkspace:(uint32_t)workspaceId {
    if (_terminalView && [_terminalView controller]) {
        termcore::TerminalController* ctrl = [_terminalView controller];
        if (ctrl->tabs() && ctrl->tabs()->mux()) {
            ctrl->tabs()->mux()->setActiveWorkspace(workspaceId);
        }
    }
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
    // Forward to unified settings window
    [self openUnifiedSettings:sender];
}

#pragma mark - Unified Settings

- (IBAction)openUnifiedSettings:(id)sender {
    (void)sender;
    [_unifiedSettingsController showSettings];
}

- (void)openUnifiedSettingsAtTheme {
    [_unifiedSettingsController showSettings];
    [_unifiedSettingsController navigateToCategory:"appearance.theme"];
}

- (void)openUnifiedSettingsAtFont {
    [_unifiedSettingsController showSettings];
    [_unifiedSettingsController navigateToCategory:"font.font_family"];
}

#pragma mark - Theme Hub / Font Hub (standalone windows -- legacy)

- (PrefsSaveBlock)makeSaveBlock {
    __weak AppDelegate* weakSelf = self;
    return ^(const termcore::Config& updated) {
        AppDelegate* strongSelf = weakSelf;
        if (!strongSelf) return;
        strongSelf->_config = updated;
        std::string luaPath = termcore::luaConfigWritePath();
        if (!luaPath.empty()) {
            termcore::writeConfigLua(luaPath, updated);
        }
        if (strongSelf->_configWatcher) {
            strongSelf->_configWatcher->reloadNow();
        }
    };
}

- (IBAction)openThemeHub:(id)sender {
    (void)sender;

    if (_themeHubWindowController.window &&
        _themeHubWindowController.window.isVisible) {
        [_themeHubWindowController.window makeKeyAndOrderFront:nil];
        return;
    }

    termcore::Config config = termcore::loadConfig();
    if (!config.theme.empty()) {
        auto* theme = termcore::getBuiltinTheme(config.theme);
        if (theme) termcore::applyTheme(config, *theme);
    }

    ThemeHubViewController* vc = [[ThemeHubViewController alloc] init];
    vc.config = config;
    vc.saveBlock = [self makeSaveBlock];

    NSWindowStyleMask style = NSWindowStyleMaskTitled
                            | NSWindowStyleMaskClosable
                            | NSWindowStyleMaskResizable;
    NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 720, 520)
                                                   styleMask:style
                                                     backing:NSBackingStoreBuffered
                                                       defer:YES];
    window.title = @"Theme Hub";
    window.contentViewController = vc;
    [window center];

    _themeHubWindowController = [[NSWindowController alloc] initWithWindow:window];
    [_themeHubWindowController showWindow:nil];
}

- (IBAction)openFontHub:(id)sender {
    (void)sender;

    if (_fontHubWindowController.window &&
        _fontHubWindowController.window.isVisible) {
        [_fontHubWindowController.window makeKeyAndOrderFront:nil];
        return;
    }

    termcore::Config config = termcore::loadConfig();
    if (!config.theme.empty()) {
        auto* theme = termcore::getBuiltinTheme(config.theme);
        if (theme) termcore::applyTheme(config, *theme);
    }

    FontHubViewController* vc = [[FontHubViewController alloc] init];
    vc.config = config;
    vc.saveBlock = [self makeSaveBlock];

    NSWindowStyleMask style = NSWindowStyleMaskTitled
                            | NSWindowStyleMaskClosable
                            | NSWindowStyleMaskResizable;
    NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 720, 520)
                                                   styleMask:style
                                                     backing:NSBackingStoreBuffered
                                                       defer:YES];
    window.title = @"Font Hub";
    window.contentViewController = vc;
    [window center];

    _fontHubWindowController = [[NSWindowController alloc] initWithWindow:window];
    [_fontHubWindowController showWindow:nil];
}

@end
