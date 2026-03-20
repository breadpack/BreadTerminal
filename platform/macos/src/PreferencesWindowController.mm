#import "PreferencesWindowController.h"
#import "PrefsGeneralViewController.h"
#import "PrefsAppearanceViewController.h"
#import "PrefsFontViewController.h"
#import "PrefsKeybindingsViewController.h"
#import "PrefsClipboardViewController.h"

static NSString* const kToolbarIdentifier = @"PreferencesToolbar";
static NSString* const kTabGeneral      = @"General";
static NSString* const kTabAppearance   = @"Appearance";
static NSString* const kTabFont         = @"Font";
static NSString* const kTabKeybindings  = @"Keybindings";
static NSString* const kTabClipboard    = @"Clipboard";

@implementation PreferencesWindowController {
    termcore::Config _liveConfig;
    std::string _configPath;
    termcore::IConfigWatcher* _watcher;  // non-owning
    NSString* _selectedTab;
}

- (instancetype)initWithConfigPath:(const std::string&)path
                     configWatcher:(termcore::IConfigWatcher*)watcher {
    // Create the preferences window programmatically
    NSWindowStyleMask style = NSWindowStyleMaskTitled
                            | NSWindowStyleMaskClosable;
    NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 480, 400)
                                                   styleMask:style
                                                     backing:NSBackingStoreBuffered
                                                       defer:YES];
    window.title = @"Preferences";
    [window center];

    self = [super initWithWindow:window];
    if (self) {
        _configPath = path;
        _watcher = watcher;
        _liveConfig = termcore::parseConfigFile(path);
        if (!_liveConfig.theme.empty()) {
            auto* theme = termcore::getBuiltinTheme(_liveConfig.theme);
            if (theme) termcore::applyTheme(_liveConfig, *theme);
        }
        _selectedTab = kTabGeneral;

        // Setup toolbar
        NSToolbar* toolbar = [[NSToolbar alloc] initWithIdentifier:kToolbarIdentifier];
        toolbar.delegate = self;
        toolbar.displayMode = NSToolbarDisplayModeIconAndLabel;
        toolbar.allowsUserCustomization = NO;
        toolbar.selectedItemIdentifier = kTabGeneral;
        window.toolbar = toolbar;

        // Show initial tab
        [self switchToTab:kTabGeneral];
    }
    return self;
}

- (void)showPreferences {
    // Reload config from disk each time window is shown
    _liveConfig = termcore::parseConfigFile(_configPath);
    if (!_liveConfig.theme.empty()) {
        auto* theme = termcore::getBuiltinTheme(_liveConfig.theme);
        if (theme) termcore::applyTheme(_liveConfig, *theme);
    }
    [self switchToTab:_selectedTab];
    [self.window makeKeyAndOrderFront:nil];
}

#pragma mark - Tab Switching

- (NSArray<NSString*>*)tabIdentifiers {
    return @[kTabGeneral, kTabAppearance, kTabFont, kTabKeybindings, kTabClipboard];
}

- (void)switchToTab:(NSString*)tabId {
    _selectedTab = tabId;
    self.window.toolbar.selectedItemIdentifier = tabId;

    NSViewController* vc = [self viewControllerForTab:tabId];
    NSSize newSize = vc.preferredContentSize;

    // Animate window resize to fit new tab content
    NSRect windowFrame = self.window.frame;
    NSRect contentRect = [self.window contentRectForFrameRect:windowFrame];
    CGFloat deltaH = newSize.height - contentRect.size.height;
    CGFloat deltaW = newSize.width  - contentRect.size.width;

    NSRect newFrame = windowFrame;
    newFrame.size.width  += deltaW;
    newFrame.size.height += deltaH;
    newFrame.origin.y    -= deltaH;  // grow upward

    self.window.contentViewController = vc;
    [self.window setFrame:newFrame display:YES animate:YES];
}

- (NSViewController*)viewControllerForTab:(NSString*)tabId {
    PrefsSaveBlock saveBlock = [self makeSaveBlock];

    if ([tabId isEqualToString:kTabGeneral]) {
        PrefsGeneralViewController* vc = [[PrefsGeneralViewController alloc] init];
        vc.config = _liveConfig;
        vc.saveBlock = saveBlock;
        return vc;
    } else if ([tabId isEqualToString:kTabAppearance]) {
        PrefsAppearanceViewController* vc = [[PrefsAppearanceViewController alloc] init];
        vc.config = _liveConfig;
        vc.saveBlock = saveBlock;
        return vc;
    } else if ([tabId isEqualToString:kTabFont]) {
        PrefsFontViewController* vc = [[PrefsFontViewController alloc] init];
        vc.config = _liveConfig;
        vc.saveBlock = saveBlock;
        return vc;
    } else if ([tabId isEqualToString:kTabKeybindings]) {
        PrefsKeybindingsViewController* vc = [[PrefsKeybindingsViewController alloc] init];
        vc.config = _liveConfig;
        vc.saveBlock = saveBlock;
        return vc;
    } else if ([tabId isEqualToString:kTabClipboard]) {
        PrefsClipboardViewController* vc = [[PrefsClipboardViewController alloc] init];
        vc.config = _liveConfig;
        vc.saveBlock = saveBlock;
        return vc;
    }
    return [[NSViewController alloc] init];
}

- (PrefsSaveBlock)makeSaveBlock {
    __weak PreferencesWindowController* weakSelf = self;
    return ^(const termcore::Config& updated) {
        PreferencesWindowController* strongSelf = weakSelf;
        if (!strongSelf) return;
        strongSelf->_liveConfig = updated;
        termcore::writeConfigFile(strongSelf->_configPath, updated);
        if (strongSelf->_watcher) {
            strongSelf->_watcher->reloadNow();
        }
    };
}

#pragma mark - NSToolbarDelegate

- (NSArray<NSToolbarItemIdentifier>*)toolbarAllowedItemIdentifiers:(NSToolbar*)toolbar {
    (void)toolbar;
    return [self tabIdentifiers];
}

- (NSArray<NSToolbarItemIdentifier>*)toolbarDefaultItemIdentifiers:(NSToolbar*)toolbar {
    (void)toolbar;
    return [self tabIdentifiers];
}

- (NSArray<NSToolbarItemIdentifier>*)toolbarSelectableItemIdentifiers:(NSToolbar*)toolbar {
    (void)toolbar;
    return [self tabIdentifiers];
}

- (NSToolbarItem*)toolbar:(NSToolbar*)toolbar
    itemForItemIdentifier:(NSToolbarItemIdentifier)itemIdentifier
    willBeInsertedIntoToolbar:(BOOL)flag {
    (void)toolbar;
    (void)flag;

    NSToolbarItem* item = [[NSToolbarItem alloc] initWithItemIdentifier:itemIdentifier];
    item.label = itemIdentifier;
    item.target = self;
    item.action = @selector(toolbarTabClicked:);

    // Use SF Symbols for icons (macOS 11+)
    NSString* symbolName = [self sfSymbolForTab:itemIdentifier];
    if (symbolName) {
        if (@available(macOS 11.0, *)) {
            NSImage* img = [NSImage imageWithSystemSymbolName:symbolName
                                    accessibilityDescription:itemIdentifier];
            if (img) {
                item.image = img;
            }
        }
    }

    return item;
}

- (NSString*)sfSymbolForTab:(NSString*)tabId {
    if ([tabId isEqualToString:kTabGeneral])     return @"gear";
    if ([tabId isEqualToString:kTabAppearance])  return @"paintbrush";
    if ([tabId isEqualToString:kTabFont])        return @"textformat";
    if ([tabId isEqualToString:kTabKeybindings]) return @"keyboard";
    if ([tabId isEqualToString:kTabClipboard])   return @"doc.on.clipboard";
    return nil;
}

- (void)toolbarTabClicked:(NSToolbarItem*)sender {
    [self switchToTab:sender.itemIdentifier];
}

@end
