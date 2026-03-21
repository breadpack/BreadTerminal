#if defined(__APPLE__)

#import "UnifiedSettingsThemeCards.h"
#import "UnifiedSettingsWindowController.h"

#include "termcore/theme_index.h"
#include "termcore/theme_loader.h"
#include "termcore/config.h"

#import "ThemeDownloader.h"

static const CGFloat kCardWidth    = 190.0;
static const CGFloat kCardHeight   = 134.0;
static const CGFloat kCardGap      = 12.0;
static const CGFloat kGridPadding  = 24.0;
static const CGFloat kFilterBarH   = 40.0;
static const CGFloat kSwatchSize   = 16.0;
static const CGFloat kSwatchGap    = 4.0;

enum ThemeFilterType : NSInteger {
    ThemeFilterAll       = 0,
    ThemeFilterDark      = 1,
    ThemeFilterLight     = 2,
    ThemeFilterInstalled = 3,
};

@implementation UnifiedSettingsThemeCards {
    __weak UnifiedSettingsWindowController* _controller;

    // Filter bar
    NSSegmentedControl* _filterSegment;
    NSSearchField* _localSearchField;
    ThemeFilterType _activeFilter;
    NSString* _localSearchQuery;

    // Grid container
    NSView* _gridContainer;
    NSTimer* _searchDebounce;
}

- (instancetype)initWithController:(UnifiedSettingsWindowController*)controller {
    self = [super initWithFrame:NSMakeRect(0, 0, 500, 600)];
    if (self) {
        _controller = controller;
        _activeFilter = ThemeFilterAll;
        _localSearchQuery = @"";

        [self buildFilterBar];
        [self reloadCards];
    }
    return self;
}

- (BOOL)isFlipped {
    return YES;
}

- (void)buildFilterBar {
    // Filter segment control
    _filterSegment = [[NSSegmentedControl alloc] init];
    [_filterSegment setSegmentCount:4];
    [_filterSegment setLabel:@"All" forSegment:0];
    [_filterSegment setLabel:@"Dark" forSegment:1];
    [_filterSegment setLabel:@"Light" forSegment:2];
    [_filterSegment setLabel:@"Installed" forSegment:3];
    _filterSegment.segmentStyle = NSSegmentStyleAutomatic;
    _filterSegment.selectedSegment = 0;
    _filterSegment.target = self;
    _filterSegment.action = @selector(filterChanged:);
    _filterSegment.frame = NSMakeRect(kGridPadding, 8, 300, 24);
    [self addSubview:_filterSegment];

    // Local search field
    _localSearchField = [[NSSearchField alloc] initWithFrame:NSZeroRect];
    _localSearchField.placeholderString = @"Search themes...";
    _localSearchField.frame = NSMakeRect(self.frame.size.width - kGridPadding - 200, 8, 200, 24);
    _localSearchField.autoresizingMask = NSViewMinXMargin;
    _localSearchField.target = self;
    _localSearchField.action = @selector(localSearchAction:);
    [self addSubview:_localSearchField];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(localSearchTextDidChange:)
                                                 name:NSControlTextDidChangeNotification
                                               object:_localSearchField];
}

- (void)reloadCards {
    // Remove old grid
    [_gridContainer removeFromSuperview];
    _gridContainer = [[NSView alloc] initWithFrame:NSZeroRect];
    [self addSubview:_gridContainer];

    UnifiedSettingsWindowController* ctrl = _controller;
    if (!ctrl) return;

    // Get filtered themes
    std::vector<const termcore::ThemeMetadata*> themes;

    if (_localSearchQuery.length > 0) {
        themes = ctrl.themeIndex.search(std::string([_localSearchQuery UTF8String]));
    } else {
        switch (_activeFilter) {
            case ThemeFilterAll:
                for (const auto& t : ctrl.themeIndex.all()) themes.push_back(&t);
                break;
            case ThemeFilterDark:
                themes = ctrl.themeIndex.filterByCategory(true, false, false);
                break;
            case ThemeFilterLight:
                themes = ctrl.themeIndex.filterByCategory(false, true, false);
                break;
            case ThemeFilterInstalled:
                themes = ctrl.themeIndex.filterByCategory(false, false, true);
                break;
        }
    }

    std::string activeTheme = ctrl.config.theme;

    // Calculate grid layout
    CGFloat availWidth = self.frame.size.width;
    if (availWidth < kCardWidth + 2 * kGridPadding) availWidth = 500;
    NSInteger cols = (NSInteger)((availWidth - 2 * kGridPadding + kCardGap) / (kCardWidth + kCardGap));
    if (cols < 1) cols = 1;

    CGFloat totalGridWidth = cols * kCardWidth + (cols - 1) * kCardGap;
    CGFloat offsetX = (availWidth - totalGridWidth) / 2.0;

    NSInteger rows = ((NSInteger)themes.size() + cols - 1) / cols;
    CGFloat gridHeight = rows * kCardHeight + (rows > 0 ? (rows - 1) * kCardGap : 0) + kGridPadding;

    _gridContainer.frame = NSMakeRect(0, kFilterBarH, availWidth, gridHeight);

    __weak UnifiedSettingsThemeCards* weakSelf = self;

    for (NSInteger i = 0; i < (NSInteger)themes.size(); i++) {
        const termcore::ThemeMetadata* meta = themes[i];

        NSInteger col = i % cols;
        NSInteger row = i / cols;

        CGFloat x = offsetX + col * (kCardWidth + kCardGap);
        CGFloat y = row * (kCardHeight + kCardGap);

        NSView* card = [self createCardForTheme:meta
                                       isActive:(meta->name == activeTheme)
                                          frame:NSMakeRect(x, y, kCardWidth, kCardHeight)];
        [_gridContainer addSubview:card];
    }

    // Show placeholder if empty
    if (themes.empty()) {
        NSTextField* emptyLabel = [NSTextField labelWithString:@"No themes found"];
        emptyLabel.font = [NSFont systemFontOfSize:14];
        emptyLabel.textColor = [NSColor secondaryLabelColor];
        emptyLabel.alignment = NSTextAlignmentCenter;
        emptyLabel.frame = NSMakeRect(0, 40, availWidth, 24);
        [_gridContainer addSubview:emptyLabel];
    }

    // Update total height
    CGFloat totalHeight = kFilterBarH + gridHeight + kGridPadding;
    self.frame = NSMakeRect(0, 0, availWidth, totalHeight);
}

#pragma mark - Card Creation

- (NSView*)createCardForTheme:(const termcore::ThemeMetadata*)meta
                     isActive:(BOOL)isActive
                        frame:(NSRect)frame {
    NSView* card = [[NSView alloc] initWithFrame:frame];
    card.wantsLayer = YES;
    card.layer.cornerRadius = 8.0;
    card.layer.borderWidth = isActive ? 2.0 : 1.0;

    // Background color from theme
    CGFloat bgR = ((meta->background >> 16) & 0xFF) / 255.0;
    CGFloat bgG = ((meta->background >> 8) & 0xFF) / 255.0;
    CGFloat bgB = (meta->background & 0xFF) / 255.0;
    card.layer.backgroundColor = [NSColor colorWithCalibratedRed:bgR green:bgG blue:bgB alpha:1.0].CGColor;

    if (isActive) {
        card.layer.borderColor = [NSColor controlAccentColor].CGColor;
    } else {
        card.layer.borderColor = [NSColor separatorColor].CGColor;
    }

    // Theme name label
    NSString* themeName = [NSString stringWithUTF8String:meta->name.c_str()];
    NSTextField* nameLabel = [NSTextField labelWithString:themeName];
    nameLabel.font = [NSFont systemFontOfSize:12 weight:NSFontWeightMedium];
    CGFloat fgR = ((meta->foreground >> 16) & 0xFF) / 255.0;
    CGFloat fgG = ((meta->foreground >> 8) & 0xFF) / 255.0;
    CGFloat fgB = (meta->foreground & 0xFF) / 255.0;
    nameLabel.textColor = [NSColor colorWithCalibratedRed:fgR green:fgG blue:fgB alpha:1.0];
    nameLabel.frame = NSMakeRect(10, 10, frame.size.width - 20, 18);
    nameLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    [card addSubview:nameLabel];

    // Palette swatches (6 colors from the 16-color palette)
    int swatchIndices[] = {1, 2, 3, 4, 5, 6};  // red, green, yellow, blue, magenta, cyan
    CGFloat swatchY = 34;
    for (int s = 0; s < 6; s++) {
        uint32_t color = meta->palette[swatchIndices[s]];
        CGFloat r = ((color >> 16) & 0xFF) / 255.0;
        CGFloat g = ((color >> 8) & 0xFF) / 255.0;
        CGFloat b = (color & 0xFF) / 255.0;

        NSView* swatch = [[NSView alloc] initWithFrame:NSMakeRect(10 + s * (kSwatchSize + kSwatchGap),
                                                                    swatchY,
                                                                    kSwatchSize, kSwatchSize)];
        swatch.wantsLayer = YES;
        swatch.layer.cornerRadius = 3.0;
        swatch.layer.backgroundColor = [NSColor colorWithCalibratedRed:r green:g blue:b alpha:1.0].CGColor;
        [card addSubview:swatch];
    }

    // Apply / Applied button
    NSButton* actionButton;
    if (isActive) {
        actionButton = [NSButton buttonWithTitle:@"Applied" target:nil action:nil];
        actionButton.enabled = NO;
    } else {
        actionButton = [NSButton buttonWithTitle:meta->installed ? @"Apply" : @"Install"
                                          target:self
                                          action:@selector(themeCardAction:)];
    }
    actionButton.bezelStyle = NSBezelStyleRounded;
    actionButton.controlSize = NSControlSizeMini;
    actionButton.frame = NSMakeRect(frame.size.width - 70, frame.size.height - 32, 60, 22);

    // Store theme name in accessibility identifier for retrieval
    actionButton.accessibilityIdentifier = themeName;
    [card addSubview:actionButton];

    return card;
}

#pragma mark - Actions

- (void)themeCardAction:(NSButton*)sender {
    NSString* themeName = sender.accessibilityIdentifier;
    if (!themeName) return;

    std::string name([themeName UTF8String]);
    UnifiedSettingsWindowController* ctrl = _controller;
    if (!ctrl) return;

    // Check if theme is installed (can be applied directly)
    bool installed = false;
    for (const auto& t : ctrl.themeIndex.all()) {
        if (t.name == name) {
            installed = t.installed;
            break;
        }
    }

    if (installed) {
        [self applyTheme:name];
    } else {
        [self downloadAndApplyTheme:name];
    }
}

- (void)applyTheme:(const std::string&)name {
    UnifiedSettingsWindowController* ctrl = _controller;
    if (!ctrl) return;

    auto theme = termcore::findTheme(name);
    if (!theme) {
        NSLog(@"UnifiedSettings: theme '%s' not found", name.c_str());
        return;
    }

    ctrl.config.theme = name;
    termcore::applyTheme(ctrl.config, *theme);
    [ctrl configDidChange];
    [self reloadCards];
}

- (void)downloadAndApplyTheme:(const std::string&)name {
    UnifiedSettingsWindowController* ctrl = _controller;
    if (!ctrl) return;

    // Find the source URL
    std::string url;
    for (const auto& t : ctrl.themeIndex.all()) {
        if (t.name == name) {
            url = t.source_url;
            break;
        }
    }

    if (url.empty()) {
        // Try applying anyway (might be built-in)
        [self applyTheme:name];
        return;
    }

    NSString* nsName = [NSString stringWithUTF8String:name.c_str()];
    NSString* nsURL = [NSString stringWithUTF8String:url.c_str()];

    __weak UnifiedSettingsThemeCards* weakSelf = self;
    [[ThemeDownloader sharedDownloader] downloadTheme:nsName
                                              fromURL:nsURL
                                           completion:^(BOOL success, NSError* error) {
        UnifiedSettingsThemeCards* strongSelf = weakSelf;
        if (!strongSelf) return;

        UnifiedSettingsWindowController* ctrl2 = strongSelf->_controller;
        if (success && ctrl2) {
            ctrl2.themeIndex.markInstalled(std::string([nsName UTF8String]));
            [strongSelf applyTheme:std::string([nsName UTF8String])];
        } else {
            NSLog(@"UnifiedSettings: theme download failed for '%@': %@",
                  nsName, error.localizedDescription);
            NSAlert* alert = [[NSAlert alloc] init];
            alert.messageText = @"Download Failed";
            alert.informativeText = [NSString stringWithFormat:@"Could not download theme '%@': %@",
                                     nsName, error.localizedDescription ?: @"Unknown error"];
            alert.alertStyle = NSAlertStyleWarning;
            [alert addButtonWithTitle:@"OK"];
            [alert runModal];
        }
    }];
}

#pragma mark - Filter / Search

- (void)filterChanged:(id)sender {
    (void)sender;
    _activeFilter = (ThemeFilterType)_filterSegment.selectedSegment;
    [self reloadCards];
}

- (void)localSearchAction:(id)sender {
    (void)sender;
    _localSearchQuery = _localSearchField.stringValue;
    [self reloadCards];
}

- (void)localSearchTextDidChange:(NSNotification*)notification {
    (void)notification;
    [_searchDebounce invalidate];
    __weak UnifiedSettingsThemeCards* weakSelf = self;
    _searchDebounce = [NSTimer scheduledTimerWithTimeInterval:0.2
                                                      repeats:NO
                                                        block:^(NSTimer* timer) {
        (void)timer;
        UnifiedSettingsThemeCards* s = weakSelf;
        if (!s) return;
        s->_localSearchQuery = s->_localSearchField.stringValue;
        [s reloadCards];
    }];
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [_searchDebounce invalidate];
}

@end

#endif // __APPLE__
