#import "ThemeHubViewController.h"
#import "ThemeCardView.h"
#import "ThemeDownloader.h"

#include "termcore/theme_index.h"
#include "termcore/theme_loader.h"

#include <string>
#include <vector>
#include <fstream>
#include <sstream>

static const CGFloat kCardSpacingH = 12.0;
static const CGFloat kCardSpacingV = 12.0;
static const CGFloat kGridPadding  = 16.0;
static const CGFloat kTopBarHeight = 40.0;

// Filter segments
enum ThemeFilter : NSInteger {
    ThemeFilterAll       = 0,
    ThemeFilterDark      = 1,
    ThemeFilterLight     = 2,
    ThemeFilterInstalled = 3,
};

@interface ThemeHubViewController () <NSSearchFieldDelegate>
@end

@implementation ThemeHubViewController {
    termcore::ThemeIndex _themeIndex;
    BOOL _indexLoaded;

    NSSearchField* _searchField;
    NSSegmentedControl* _filterSegment;
    NSScrollView* _scrollView;
    NSView* _gridContainer;
    NSTimer* _searchDebounce;

    NSString* _searchQuery;
    ThemeFilter _activeFilter;
}

- (void)loadView {
    self.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 760, 520)];

    // --- Top bar ---
    _searchField = [[NSSearchField alloc] initWithFrame:NSZeroRect];
    _searchField.placeholderString = @"Search themes...";
    _searchField.delegate = self;
    _searchField.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_searchField];

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
    _filterSegment.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_filterSegment];

    // --- Scroll view with grid ---
    _scrollView = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    _scrollView.hasVerticalScroller = YES;
    _scrollView.hasHorizontalScroller = NO;
    _scrollView.drawsBackground = NO;
    _scrollView.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_scrollView];

    _gridContainer = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 760, 400)];
    _gridContainer.translatesAutoresizingMaskIntoConstraints = NO;
    _scrollView.documentView = _gridContainer;

    // --- Constraints ---
    [NSLayoutConstraint activateConstraints:@[
        [_searchField.topAnchor constraintEqualToAnchor:self.view.topAnchor constant:10],
        [_searchField.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:kGridPadding],
        [_searchField.widthAnchor constraintEqualToConstant:220],
        [_searchField.heightAnchor constraintEqualToConstant:28],

        [_filterSegment.centerYAnchor constraintEqualToAnchor:_searchField.centerYAnchor],
        [_filterSegment.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-kGridPadding],

        [_scrollView.topAnchor constraintEqualToAnchor:_searchField.bottomAnchor constant:10],
        [_scrollView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_scrollView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [_scrollView.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],

        // Grid container width pins to scroll view
        [_gridContainer.widthAnchor constraintEqualToAnchor:_scrollView.widthAnchor],
    ]];

    _searchQuery = @"";
    _activeFilter = ThemeFilterAll;

    [self loadThemeIndex];
    [self reloadGrid];
}

- (void)loadThemeIndex {
    _indexLoaded = NO;

    // Try to load theme_index.json from the app bundle Resources
    NSString* indexPath = [[NSBundle mainBundle] pathForResource:@"theme_index" ofType:@"json"];
    if (!indexPath) {
        NSLog(@"ThemeHub: theme_index.json not found in bundle");
        return;
    }

    std::ifstream ifs([indexPath UTF8String]);
    if (!ifs.is_open()) {
        NSLog(@"ThemeHub: failed to open theme_index.json");
        return;
    }

    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string json = ss.str();

    if (!_themeIndex.loadFromJSON(json)) {
        NSLog(@"ThemeHub: failed to parse theme_index.json");
        return;
    }

    _themeIndex.refreshInstallStatus();
    _indexLoaded = YES;
    NSLog(@"ThemeHub: loaded %zu themes", _themeIndex.count());
}

- (void)reloadGrid {
    // Remove old card views
    for (NSView* sub in [_gridContainer.subviews copy]) {
        [sub removeFromSuperview];
    }

    // Get filtered themes
    std::vector<const termcore::ThemeMetadata*> themes;

    if (_searchQuery.length > 0) {
        themes = _themeIndex.search(std::string([_searchQuery UTF8String]));
    } else {
        switch (_activeFilter) {
            case ThemeFilterAll:
                for (auto& t : _themeIndex.all()) themes.push_back(&t);
                break;
            case ThemeFilterDark:
                themes = _themeIndex.filterByCategory(true, false, false);
                break;
            case ThemeFilterLight:
                themes = _themeIndex.filterByCategory(false, true, false);
                break;
            case ThemeFilterInstalled:
                themes = _themeIndex.filterByCategory(false, false, true);
                break;
        }
    }

    // Determine the active theme name
    std::string activeTheme = _config.theme;

    // Layout cards in a grid
    CGFloat availWidth = self.view.frame.size.width;
    CGFloat cardW = 200.0;
    CGFloat cardH = 130.0;
    NSInteger cols = (NSInteger)((availWidth - 2 * kGridPadding + kCardSpacingH) / (cardW + kCardSpacingH));
    if (cols < 1) cols = 1;

    CGFloat totalGridWidth = cols * cardW + (cols - 1) * kCardSpacingH;
    CGFloat offsetX = (availWidth - totalGridWidth) / 2.0;

    NSInteger rows = ((NSInteger)themes.size() + cols - 1) / cols;
    CGFloat gridHeight = rows * cardH + (rows > 0 ? (rows - 1) * kCardSpacingV : 0) + 2 * kGridPadding;

    // Update container size
    NSRect containerFrame = _gridContainer.frame;
    containerFrame.size.height = MAX(gridHeight, _scrollView.frame.size.height);
    _gridContainer.frame = containerFrame;

    __weak ThemeHubViewController* weakSelf = self;

    for (NSInteger i = 0; i < (NSInteger)themes.size(); i++) {
        const termcore::ThemeMetadata* meta = themes[i];

        NSInteger col = i % cols;
        NSInteger row = i / cols;

        // Cards layout from top (flipped coordinate: top = high Y in scroll)
        CGFloat x = offsetX + col * (cardW + kCardSpacingH);
        CGFloat y = containerFrame.size.height - kGridPadding - (row + 1) * cardH - row * kCardSpacingV;

        ThemeCardView* card = [[ThemeCardView alloc] initWithFrame:NSMakeRect(x, y, cardW, cardH)];
        card.themeName = [NSString stringWithUTF8String:meta->name.c_str()];
        card.backgroundColor = meta->background;
        card.foregroundColor = meta->foreground;
        for (int p = 0; p < 16; p++) {
            [card setPaletteColor:meta->palette[p] atIndex:p];
        }
        card.installed = meta->installed;
        card.isActive = (meta->name == activeTheme);

        // Capture name and URL for the action block
        std::string capName = meta->name;
        std::string capURL = meta->source_url;
        BOOL capInstalled = meta->installed;

        card.onAction = ^{
            ThemeHubViewController* vc = weakSelf;
            if (!vc) return;

            if (capInstalled) {
                [vc applyThemeWithName:capName];
            } else {
                [vc downloadAndApplyTheme:capName fromURL:capURL];
            }
        };

        [card updateColors];
        [_gridContainer addSubview:card];
    }

    // Show placeholder if empty
    if (themes.empty() && _indexLoaded) {
        NSTextField* emptyLabel = [NSTextField labelWithString:@"No themes found"];
        emptyLabel.font = [NSFont systemFontOfSize:14];
        emptyLabel.textColor = [NSColor secondaryLabelColor];
        emptyLabel.alignment = NSTextAlignmentCenter;
        emptyLabel.translatesAutoresizingMaskIntoConstraints = NO;
        [_gridContainer addSubview:emptyLabel];
        [NSLayoutConstraint activateConstraints:@[
            [emptyLabel.centerXAnchor constraintEqualToAnchor:_gridContainer.centerXAnchor],
            [emptyLabel.topAnchor constraintEqualToAnchor:_gridContainer.topAnchor constant:60],
        ]];
    } else if (!_indexLoaded) {
        NSTextField* loadingLabel = [NSTextField labelWithString:@"Theme index not available.\nPlace theme_index.json in app Resources."];
        loadingLabel.font = [NSFont systemFontOfSize:13];
        loadingLabel.textColor = [NSColor secondaryLabelColor];
        loadingLabel.alignment = NSTextAlignmentCenter;
        loadingLabel.maximumNumberOfLines = 2;
        loadingLabel.translatesAutoresizingMaskIntoConstraints = NO;
        [_gridContainer addSubview:loadingLabel];
        [NSLayoutConstraint activateConstraints:@[
            [loadingLabel.centerXAnchor constraintEqualToAnchor:_gridContainer.centerXAnchor],
            [loadingLabel.topAnchor constraintEqualToAnchor:_gridContainer.topAnchor constant:60],
        ]];
    }
}

#pragma mark - Theme Actions

- (void)applyThemeWithName:(const std::string&)name {
    auto theme = termcore::findTheme(name);
    if (!theme) {
        NSLog(@"ThemeHub: theme '%s' not found", name.c_str());
        return;
    }

    _config.theme = name;
    termcore::applyTheme(_config, *theme);

    if (_saveBlock) {
        _saveBlock(_config);
    }

    [self reloadGrid]; // refresh active state
}

- (void)downloadAndApplyTheme:(const std::string&)name fromURL:(const std::string&)url {
    if (url.empty()) {
        NSLog(@"ThemeHub: no download URL for theme '%s'", name.c_str());
        // Try applying anyway (might be a built-in)
        [self applyThemeWithName:name];
        return;
    }

    NSString* nsName = [NSString stringWithUTF8String:name.c_str()];
    NSString* nsURL = [NSString stringWithUTF8String:url.c_str()];

    __weak ThemeHubViewController* weakSelf = self;
    [[ThemeDownloader sharedDownloader] downloadTheme:nsName
                                              fromURL:nsURL
                                           completion:^(BOOL success, NSError* error) {
        ThemeHubViewController* vc = weakSelf;
        if (!vc) return;

        if (success) {
            vc->_themeIndex.markInstalled(std::string([nsName UTF8String]));
            [vc applyThemeWithName:std::string([nsName UTF8String])];
        } else {
            NSLog(@"ThemeHub: download failed for '%@': %@", nsName, error.localizedDescription);

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

#pragma mark - NSSearchFieldDelegate

- (void)controlTextDidChange:(NSNotification*)obj {
    (void)obj;
    // Debounce search by 200ms
    [_searchDebounce invalidate];
    __weak ThemeHubViewController* weakSelf = self;
    _searchDebounce = [NSTimer scheduledTimerWithTimeInterval:0.2
                                                      repeats:NO
                                                        block:^(NSTimer* timer) {
        (void)timer;
        ThemeHubViewController* vc = weakSelf;
        if (!vc) return;
        vc->_searchQuery = vc->_searchField.stringValue;
        [vc reloadGrid];
    }];
}

- (void)searchFieldDidEndSearching:(NSSearchField*)sender {
    (void)sender;
    [_searchDebounce invalidate];
    _searchQuery = @"";
    [self reloadGrid];
}

#pragma mark - Filter

- (void)filterChanged:(id)sender {
    (void)sender;
    _activeFilter = (ThemeFilter)_filterSegment.selectedSegment;
    [self reloadGrid];
}

#pragma mark - Content Size

- (NSSize)preferredContentSize {
    return NSMakeSize(760, 520);
}

- (void)dealloc {
    [_searchDebounce invalidate];
}

@end
