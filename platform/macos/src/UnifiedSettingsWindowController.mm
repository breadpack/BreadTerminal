#if defined(__APPLE__)

#import "UnifiedSettingsWindowController.h"
#import "UnifiedSettingsSidebar.h"
#import "UnifiedSettingsContent.h"
#import "UnifiedSettingsThemeCards.h"
#import "UnifiedSettingsFontCards.h"

#include "termcore/lua_config.h"
#include "termcore/theme_loader.h"

#include <fstream>
#include <sstream>
#import <CoreText/CoreText.h>

// Layout constants (matching Windows implementation)
static const CGFloat kWindowWidth      = 800.0;
static const CGFloat kWindowHeight     = 600.0;
static const CGFloat kMinWidth         = 640.0;
static const CGFloat kMinHeight        = 480.0;
static const CGFloat kSidebarDefault   = 200.0;
static const CGFloat kSidebarMin       = 140.0;
static const CGFloat kSidebarMax       = 320.0;
static const CGFloat kTopBarHeight     = 40.0;
static const CGFloat kBottomBarHeight  = 24.0;
static const CGFloat kSearchFieldWidth = 280.0;
static const CGFloat kSearchFieldHeight = 28.0;

@implementation UnifiedSettingsWindowController {
    termcore::Config _config;
    termcore::Config _defaultConfig;
    std::unique_ptr<termcore::SettingsModel> _model;
    termcore::FontIndex _fontIndex;
    termcore::ThemeIndex _themeIndex;

    std::string _configPath;
    termcore::IConfigWatcher* _watcher;  // non-owning

    // UI
    NSSplitView* _splitView;
    NSView* _topBar;
    NSView* _bottomBar;
    NSSearchField* _searchField;
    NSButton* _openLuaButton;

    // Section views
    UnifiedSettingsSidebar* _sidebar;
    NSView* _contentContainer;
    NSScrollView* _contentScrollView;

    // Current content view
    NSView* _currentSectionView;
    std::string _selectedCategoryId;
    NSString* _searchQuery;
}

#pragma mark - Properties

- (termcore::Config&)config {
    return _config;
}

- (termcore::SettingsModel*)settingsModel {
    return _model.get();
}

- (termcore::FontIndex&)fontIndex {
    return _fontIndex;
}

- (termcore::ThemeIndex&)themeIndex {
    return _themeIndex;
}

#pragma mark - Initialization

- (instancetype)initWithConfigPath:(const std::string&)path
                     configWatcher:(termcore::IConfigWatcher*)watcher {
    NSWindowStyleMask style = NSWindowStyleMaskTitled
                            | NSWindowStyleMaskClosable
                            | NSWindowStyleMaskMiniaturizable
                            | NSWindowStyleMaskResizable;
    NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, kWindowWidth, kWindowHeight)
                                                   styleMask:style
                                                     backing:NSBackingStoreBuffered
                                                       defer:YES];
    window.title = @"Settings";
    window.minSize = NSMakeSize(kMinWidth, kMinHeight);
    [window center];

    self = [super initWithWindow:window];
    if (self) {
        _configPath = path;
        _watcher = watcher;
        _selectedCategoryId = "general.shell";
        _searchQuery = @"";

        [self loadConfig];
        [self loadIndexFiles];
        [self buildModel];
        [self buildUI];
        [self makeSaveBlockInternal];

        // Select initial category
        [self showContentForCategory:_selectedCategoryId];
    }
    return self;
}

- (void)loadConfig {
    _config = termcore::loadConfig();
    if (!_config.theme.empty()) {
        auto theme = termcore::findTheme(_config.theme);
        if (theme) termcore::applyTheme(_config, *theme);
    }
    _defaultConfig = termcore::Config{};
}

- (void)loadIndexFiles {
    // Load theme_index.json from bundle
    NSString* themeIndexPath = [[NSBundle mainBundle] pathForResource:@"theme_index" ofType:@"json"];
    if (themeIndexPath) {
        std::ifstream ifs([themeIndexPath UTF8String]);
        if (ifs.is_open()) {
            std::stringstream ss;
            ss << ifs.rdbuf();
            _themeIndex.loadFromJSON(ss.str());
            _themeIndex.refreshInstallStatus();
        }
    }

    // Load font_index.json from bundle
    NSString* fontIndexPath = [[NSBundle mainBundle] pathForResource:@"font_index" ofType:@"json"];
    if (fontIndexPath) {
        std::ifstream ifs([fontIndexPath UTF8String]);
        if (ifs.is_open()) {
            std::stringstream ss;
            ss << ifs.rdbuf();
            _fontIndex.loadFromJSON(ss.str());

            // Inject CoreText install-check predicate
            _fontIndex.setInstalledPredicate([](const std::string& name) -> bool {
                if (name.empty()) return false;
                CFStringRef cfName = CFStringCreateWithCString(kCFAllocatorDefault,
                                                                name.c_str(),
                                                                kCFStringEncodingUTF8);
                if (!cfName) return false;
                CTFontRef font = CTFontCreateWithName(cfName, 12.0, nullptr);
                CFRelease(cfName);
                if (!font) return false;
                CFRelease(font);
                return true;
            });
            _fontIndex.refreshInstallStatus();

            // Enumerate system fonts and add any not already in the index
            NSArray<NSString*>* families = [[NSFontManager sharedFontManager] availableFontFamilies];
            for (NSString* family in families) {
                _fontIndex.addSystemFont(std::string([family UTF8String]));
            }
        }
    }
}

- (void)buildModel {
    _model = std::make_unique<termcore::SettingsModel>(_config, _defaultConfig);
}

- (void)makeSaveBlockInternal {
    __weak UnifiedSettingsWindowController* weakSelf = self;
    self.saveBlock = ^(const termcore::Config& updated) {
        UnifiedSettingsWindowController* strongSelf = weakSelf;
        if (!strongSelf) return;
        strongSelf->_config = updated;
        std::string luaPath = termcore::luaConfigWritePath();
        if (!luaPath.empty()) {
            termcore::writeConfigLua(luaPath, updated);
        }
        if (strongSelf->_watcher) {
            strongSelf->_watcher->reloadNow();
        }
    };
}

#pragma mark - UI Construction

- (void)buildUI {
    NSView* contentView = self.window.contentView;
    contentView.wantsLayer = YES;

    // --- Top bar ---
    _topBar = [[NSView alloc] initWithFrame:NSZeroRect];
    _topBar.wantsLayer = YES;
    _topBar.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:_topBar];

    // Search field
    _searchField = [[NSSearchField alloc] initWithFrame:NSZeroRect];
    _searchField.placeholderString = @"Search settings...";
    _searchField.translatesAutoresizingMaskIntoConstraints = NO;
    _searchField.target = self;
    _searchField.action = @selector(searchAction:);
    [_topBar addSubview:_searchField];

    // Open Lua button
    _openLuaButton = [NSButton buttonWithTitle:@"Open Lua" target:self action:@selector(openLuaConfig:)];
    _openLuaButton.bezelStyle = NSBezelStyleRounded;
    _openLuaButton.translatesAutoresizingMaskIntoConstraints = NO;
    [_topBar addSubview:_openLuaButton];

    // --- Bottom bar ---
    _bottomBar = [[NSView alloc] initWithFrame:NSZeroRect];
    _bottomBar.wantsLayer = YES;
    _bottomBar.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:_bottomBar];

    NSTextField* versionLabel = [NSTextField labelWithString:@"BreadTerminal v0.1"];
    versionLabel.font = [NSFont systemFontOfSize:11];
    versionLabel.textColor = [NSColor secondaryLabelColor];
    versionLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [_bottomBar addSubview:versionLabel];

    // --- Split view (sidebar + content) ---
    _splitView = [[NSSplitView alloc] initWithFrame:NSZeroRect];
    _splitView.dividerStyle = NSSplitViewDividerStyleThin;
    _splitView.vertical = YES;
    _splitView.delegate = self;
    _splitView.translatesAutoresizingMaskIntoConstraints = NO;
    [contentView addSubview:_splitView];

    // Sidebar
    _sidebar = [[UnifiedSettingsSidebar alloc] initWithController:self];
    _sidebar.translatesAutoresizingMaskIntoConstraints = NO;

    // Content container (right side)
    _contentContainer = [[NSView alloc] initWithFrame:NSZeroRect];
    _contentContainer.translatesAutoresizingMaskIntoConstraints = NO;

    // Content scroll view
    _contentScrollView = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    _contentScrollView.hasVerticalScroller = YES;
    _contentScrollView.hasHorizontalScroller = NO;
    _contentScrollView.drawsBackground = NO;
    _contentScrollView.autohidesScrollers = YES;
    _contentScrollView.translatesAutoresizingMaskIntoConstraints = NO;
    [_contentContainer addSubview:_contentScrollView];

    [NSLayoutConstraint activateConstraints:@[
        [_contentScrollView.topAnchor constraintEqualToAnchor:_contentContainer.topAnchor],
        [_contentScrollView.leadingAnchor constraintEqualToAnchor:_contentContainer.leadingAnchor],
        [_contentScrollView.trailingAnchor constraintEqualToAnchor:_contentContainer.trailingAnchor],
        [_contentScrollView.bottomAnchor constraintEqualToAnchor:_contentContainer.bottomAnchor],
    ]];

    [_splitView addSubview:_sidebar];
    [_splitView addSubview:_contentContainer];
    [_splitView setPosition:kSidebarDefault ofDividerAtIndex:0];

    // --- Layout constraints ---
    [NSLayoutConstraint activateConstraints:@[
        // Top bar
        [_topBar.topAnchor constraintEqualToAnchor:contentView.topAnchor],
        [_topBar.leadingAnchor constraintEqualToAnchor:contentView.leadingAnchor],
        [_topBar.trailingAnchor constraintEqualToAnchor:contentView.trailingAnchor],
        [_topBar.heightAnchor constraintEqualToConstant:kTopBarHeight],

        // Search field in top bar
        [_searchField.centerYAnchor constraintEqualToAnchor:_topBar.centerYAnchor],
        [_searchField.leadingAnchor constraintEqualToAnchor:_topBar.leadingAnchor constant:12],
        [_searchField.widthAnchor constraintEqualToConstant:kSearchFieldWidth],
        [_searchField.heightAnchor constraintEqualToConstant:kSearchFieldHeight],

        // Open Lua button in top bar
        [_openLuaButton.centerYAnchor constraintEqualToAnchor:_topBar.centerYAnchor],
        [_openLuaButton.trailingAnchor constraintEqualToAnchor:_topBar.trailingAnchor constant:-12],

        // Split view (between top bar and bottom bar)
        [_splitView.topAnchor constraintEqualToAnchor:_topBar.bottomAnchor],
        [_splitView.leadingAnchor constraintEqualToAnchor:contentView.leadingAnchor],
        [_splitView.trailingAnchor constraintEqualToAnchor:contentView.trailingAnchor],
        [_splitView.bottomAnchor constraintEqualToAnchor:_bottomBar.topAnchor],

        // Bottom bar
        [_bottomBar.leadingAnchor constraintEqualToAnchor:contentView.leadingAnchor],
        [_bottomBar.trailingAnchor constraintEqualToAnchor:contentView.trailingAnchor],
        [_bottomBar.bottomAnchor constraintEqualToAnchor:contentView.bottomAnchor],
        [_bottomBar.heightAnchor constraintEqualToConstant:kBottomBarHeight],

        // Version label in bottom bar
        [versionLabel.centerYAnchor constraintEqualToAnchor:_bottomBar.centerYAnchor],
        [versionLabel.leadingAnchor constraintEqualToAnchor:_bottomBar.leadingAnchor constant:12],
    ]];

    // Set up search field delegate behavior via notification
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(searchFieldTextDidChange:)
                                                 name:NSControlTextDidChangeNotification
                                               object:_searchField];
}

#pragma mark - Show / Navigate

- (void)showSettings {
    // Reload config each time
    [self loadConfig];
    [self buildModel];
    [_sidebar reloadData];
    [self showContentForCategory:_selectedCategoryId];
    [self.window makeKeyAndOrderFront:nil];
}

- (void)navigateToCategory:(const std::string&)categoryId {
    _selectedCategoryId = categoryId;
    [_sidebar selectCategory:categoryId];
    [self showContentForCategory:categoryId];
}

#pragma mark - Category Selection

- (void)didSelectCategory:(const std::string&)categoryId {
    _selectedCategoryId = categoryId;
    [self showContentForCategory:categoryId];
}

- (void)showContentForCategory:(const std::string&)categoryId {
    // Remove previous content
    _contentScrollView.documentView = nil;
    _currentSectionView = nil;

    if (!_model) return;

    const termcore::SettingsCategory* cat = _model->category(categoryId);
    if (!cat) return;

    NSView* sectionView = nil;

    switch (cat->sectionType) {
        case termcore::SectionType::Settings: {
            UnifiedSettingsContent* content = [[UnifiedSettingsContent alloc]
                initWithController:self category:cat];
            sectionView = content;
            break;
        }
        case termcore::SectionType::CardGrid: {
            // Determine if this is theme or font
            if (categoryId.find("theme") != std::string::npos) {
                UnifiedSettingsThemeCards* cards = [[UnifiedSettingsThemeCards alloc]
                    initWithController:self];
                sectionView = cards;
            } else if (categoryId.find("font_family") != std::string::npos ||
                       categoryId.find("font.font") != std::string::npos) {
                UnifiedSettingsFontCards* cards = [[UnifiedSettingsFontCards alloc]
                    initWithController:self];
                sectionView = cards;
            }
            break;
        }
        case termcore::SectionType::KeybindingList: {
            // TODO: Keybinding list section (separate task)
            NSTextField* placeholder = [NSTextField labelWithString:@"Keybinding editor coming soon..."];
            placeholder.font = [NSFont systemFontOfSize:14];
            placeholder.textColor = [NSColor secondaryLabelColor];
            placeholder.translatesAutoresizingMaskIntoConstraints = NO;

            NSView* wrapper = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 400, 200)];
            [wrapper addSubview:placeholder];
            [NSLayoutConstraint activateConstraints:@[
                [placeholder.centerXAnchor constraintEqualToAnchor:wrapper.centerXAnchor],
                [placeholder.centerYAnchor constraintEqualToAnchor:wrapper.centerYAnchor],
            ]];
            sectionView = wrapper;
            break;
        }
    }

    if (sectionView) {
        _currentSectionView = sectionView;
        _contentScrollView.documentView = sectionView;
    }
}

#pragma mark - Config Changes

- (void)configDidChange {
    if (_saveBlock) {
        _saveBlock(_config);
    }
    // Refresh model modified indicators
    if (_model) {
        _model->refreshModified(_config);
    }
}

#pragma mark - Search

- (void)searchAction:(id)sender {
    (void)sender;
    NSString* query = _searchField.stringValue;
    [self searchTextDidChange:query];
}

- (void)searchFieldTextDidChange:(NSNotification*)notification {
    (void)notification;
    NSString* query = _searchField.stringValue;
    [self searchTextDidChange:query];
}

- (void)searchTextDidChange:(NSString*)query {
    _searchQuery = query;
    [_sidebar filterWithSearchQuery:query];
}

#pragma mark - Open Lua Config

- (void)openLuaConfig:(id)sender {
    (void)sender;
    std::string luaPath = termcore::luaConfigWritePath();
    if (luaPath.empty()) return;

    // Create default config.lua if it does not exist
    NSString* nsPath = [NSString stringWithUTF8String:luaPath.c_str()];
    if (![[NSFileManager defaultManager] fileExistsAtPath:nsPath]) {
        termcore::writeDefaultLuaConfig(luaPath);
    }

    NSURL* url = [NSURL fileURLWithPath:nsPath];
    [[NSWorkspace sharedWorkspace] openURL:url];
}

#pragma mark - NSSplitViewDelegate

- (CGFloat)splitView:(NSSplitView*)splitView constrainMinCoordinate:(CGFloat)proposedMin
         ofSubviewAt:(NSInteger)dividerIndex {
    (void)splitView;
    (void)proposedMin;
    (void)dividerIndex;
    return kSidebarMin;
}

- (CGFloat)splitView:(NSSplitView*)splitView constrainMaxCoordinate:(CGFloat)proposedMax
         ofSubviewAt:(NSInteger)dividerIndex {
    (void)splitView;
    (void)proposedMax;
    (void)dividerIndex;
    return kSidebarMax;
}

- (BOOL)splitView:(NSSplitView*)splitView canCollapseSubview:(NSView*)subview {
    (void)splitView;
    (void)subview;
    return NO;
}

#pragma mark - Cleanup

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

@end

#endif // __APPLE__
