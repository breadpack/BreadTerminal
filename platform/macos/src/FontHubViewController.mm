#import "FontHubViewController.h"
#import "FontCardView.h"
#import "FontDownloader.h"

#include "termcore/font_index.h"

#include <string>
#include <vector>
#include <fstream>
#include <sstream>

#import <CoreText/CoreText.h>

static const CGFloat kCardSpacingH = 12.0;
static const CGFloat kCardSpacingV = 12.0;
static const CGFloat kGridPadding  = 16.0;

enum FontFilter : NSInteger {
    FontFilterAll       = 0,
    FontFilterInstalled = 1,
    FontFilterNerdFonts = 2,
    FontFilterLigatures = 3,
};

@interface FontHubViewController () <NSSearchFieldDelegate>
@end

@implementation FontHubViewController {
    termcore::FontIndex _fontIndex;
    BOOL _indexLoaded;

    NSSearchField* _searchField;
    NSSegmentedControl* _filterSegment;
    NSScrollView* _scrollView;
    NSView* _gridContainer;
    NSTimer* _searchDebounce;

    NSString* _searchQuery;
    FontFilter _activeFilter;
}

- (void)loadView {
    self.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 780, 560)];

    // --- Search field ---
    _searchField = [[NSSearchField alloc] initWithFrame:NSZeroRect];
    _searchField.placeholderString = @"Search fonts...";
    _searchField.delegate = self;
    _searchField.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_searchField];

    // --- Filter segment ---
    _filterSegment = [[NSSegmentedControl alloc] init];
    [_filterSegment setSegmentCount:4];
    [_filterSegment setLabel:@"All" forSegment:0];
    [_filterSegment setLabel:@"Installed" forSegment:1];
    [_filterSegment setLabel:@"Nerd Fonts" forSegment:2];
    [_filterSegment setLabel:@"Ligatures" forSegment:3];
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

    _gridContainer = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 780, 500)];
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

        [_gridContainer.widthAnchor constraintEqualToAnchor:_scrollView.widthAnchor],
    ]];

    _searchQuery = @"";
    _activeFilter = FontFilterAll;

    [self loadFontIndex];
    [self rebuildCards];
}

- (void)loadFontIndex {
    _indexLoaded = NO;

    // Load font_index.json from bundle
    NSString* indexPath = [[NSBundle mainBundle] pathForResource:@"font_index" ofType:@"json"];
    if (!indexPath) {
        NSLog(@"FontHub: font_index.json not found in bundle");
        return;
    }

    std::ifstream ifs([indexPath UTF8String]);
    if (!ifs.is_open()) {
        NSLog(@"FontHub: failed to open font_index.json");
        return;
    }

    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string json = ss.str();

    if (!_fontIndex.loadFromJSON(json)) {
        NSLog(@"FontHub: failed to parse font_index.json");
        return;
    }

    // Inject CoreText install-check predicate
    _fontIndex.setInstalledPredicate([](const std::string& postscriptName) -> bool {
        if (postscriptName.empty()) return false;
        CFStringRef cfName = CFStringCreateWithCString(kCFAllocatorDefault,
                                                        postscriptName.c_str(),
                                                        kCFStringEncodingUTF8);
        if (!cfName) return false;
        CTFontRef font = CTFontCreateWithName(cfName, 12.0, NULL);
        CFRelease(cfName);
        if (!font) return false;

        // Check if CoreText actually resolved to the requested font
        // (it may fall back to a different font if not installed)
        CFStringRef actualName = CTFontCopyPostScriptName(font);
        CFRelease(font);
        if (!actualName) return false;

        CFStringRef requestedCF = CFStringCreateWithCString(kCFAllocatorDefault,
                                                             postscriptName.c_str(),
                                                             kCFStringEncodingUTF8);
        CFComparisonResult cmp = CFStringCompare(actualName, requestedCF,
                                                  kCFCompareCaseInsensitive);
        CFRelease(actualName);
        CFRelease(requestedCF);

        return (cmp == kCFCompareEqualTo);
    });

    _fontIndex.refreshInstallStatus();
    _indexLoaded = YES;
    NSLog(@"FontHub: loaded %zu fonts", _fontIndex.count());
}

- (void)rebuildCards {
    // Remove old card views
    for (NSView* sub in [_gridContainer.subviews copy]) {
        [sub removeFromSuperview];
    }

    // Get filtered/searched fonts
    std::vector<const termcore::FontMetadata*> fonts;

    if (_searchQuery.length > 0) {
        fonts = _fontIndex.search(std::string([_searchQuery UTF8String]));
    } else {
        switch (_activeFilter) {
            case FontFilterAll:
                for (auto& f : _fontIndex.all()) fonts.push_back(&f);
                break;
            case FontFilterInstalled:
                fonts = _fontIndex.filter(true, false, false);
                break;
            case FontFilterNerdFonts:
                fonts = _fontIndex.filter(false, true, false);
                break;
            case FontFilterLigatures:
                fonts = _fontIndex.filter(false, false, true);
                break;
        }
    }

    [self relayoutCards:fonts];
}

- (void)relayoutCards:(const std::vector<const termcore::FontMetadata*>&)fonts {
    std::string activeFont = _config.font_family;

    CGFloat availWidth = self.view.frame.size.width;
    CGFloat cardW = 220.0;
    CGFloat cardH = 150.0;
    NSInteger cols = (NSInteger)((availWidth - 2 * kGridPadding + kCardSpacingH) / (cardW + kCardSpacingH));
    if (cols < 1) cols = 1;

    CGFloat totalGridWidth = cols * cardW + (cols - 1) * kCardSpacingH;
    CGFloat offsetX = (availWidth - totalGridWidth) / 2.0;

    NSInteger rows = ((NSInteger)fonts.size() + cols - 1) / cols;
    CGFloat gridHeight = rows * cardH + (rows > 0 ? (rows - 1) * kCardSpacingV : 0) + 2 * kGridPadding;

    NSRect containerFrame = _gridContainer.frame;
    containerFrame.size.height = MAX(gridHeight, _scrollView.frame.size.height);
    _gridContainer.frame = containerFrame;

    __weak FontHubViewController* weakSelf = self;

    for (NSInteger i = 0; i < (NSInteger)fonts.size(); i++) {
        const termcore::FontMetadata* meta = fonts[i];

        NSInteger col = i % cols;
        NSInteger row = i / cols;

        CGFloat x = offsetX + col * (cardW + kCardSpacingH);
        CGFloat y = containerFrame.size.height - kGridPadding - (row + 1) * cardH - row * kCardSpacingV;

        FontCardView* card = [[FontCardView alloc] initWithFrame:NSMakeRect(x, y, cardW, cardH)];
        card.fontName = [NSString stringWithUTF8String:meta->name.c_str()];
        card.postscriptName = [NSString stringWithUTF8String:meta->postscript_name.c_str()];
        card.hasLigatures = meta->has_ligatures;
        card.hasNerdFontVariant = meta->has_nerd_font_variant;
        card.installed = meta->installed;
        card.isActive = (meta->name == activeFont);

        std::string capName = meta->name;
        std::string capURL = meta->download_url;
        BOOL capInstalled = meta->installed;

        card.onAction = ^{
            FontHubViewController* vc = weakSelf;
            if (!vc) return;

            if (capInstalled) {
                [vc applyFontWithName:capName];
            } else {
                [vc downloadAndInstallFont:capName fromURL:capURL];
            }
        };

        [card updateState];
        [_gridContainer addSubview:card];
    }

    // Placeholder messages
    if (fonts.empty() && _indexLoaded) {
        NSTextField* emptyLabel = [NSTextField labelWithString:@"No fonts found"];
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
        NSTextField* loadingLabel = [NSTextField labelWithString:@"Font index not available.\nPlace font_index.json in app Resources."];
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

#pragma mark - Font Actions

- (void)applyFontWithName:(const std::string&)name {
    _config.font_family = name;

    if (_saveBlock) {
        _saveBlock(_config);
    }

    [self rebuildCards]; // refresh active state
}

- (void)downloadAndInstallFont:(const std::string&)name fromURL:(const std::string&)url {
    if (url.empty()) {
        NSLog(@"FontHub: no download URL for font '%s'", name.c_str());
        // Font might be a system font; try applying directly
        [self applyFontWithName:name];
        return;
    }

    NSString* nsName = [NSString stringWithUTF8String:name.c_str()];
    NSString* nsURL = [NSString stringWithUTF8String:url.c_str()];

    __weak FontHubViewController* weakSelf = self;
    [[FontDownloader sharedDownloader] downloadFont:nsName
                                            fromURL:nsURL
                                           progress:nil
                                         completion:^(BOOL success, NSError* error) {
        FontHubViewController* vc = weakSelf;
        if (!vc) return;

        if (success) {
            vc->_fontIndex.markInstalled(std::string([nsName UTF8String]));
            vc->_fontIndex.refreshInstallStatus();
            [vc applyFontWithName:std::string([nsName UTF8String])];
        } else {
            NSLog(@"FontHub: download failed for '%@': %@", nsName, error.localizedDescription);

            NSAlert* alert = [[NSAlert alloc] init];
            alert.messageText = @"Download Failed";
            alert.informativeText = [NSString stringWithFormat:@"Could not download font '%@': %@",
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
    [_searchDebounce invalidate];
    __weak FontHubViewController* weakSelf = self;
    _searchDebounce = [NSTimer scheduledTimerWithTimeInterval:0.2
                                                      repeats:NO
                                                        block:^(NSTimer* timer) {
        (void)timer;
        FontHubViewController* vc = weakSelf;
        if (!vc) return;
        vc->_searchQuery = vc->_searchField.stringValue;
        [vc rebuildCards];
    }];
}

- (void)searchFieldDidEndSearching:(NSSearchField*)sender {
    (void)sender;
    [_searchDebounce invalidate];
    _searchQuery = @"";
    [self rebuildCards];
}

#pragma mark - Filter

- (void)filterChanged:(id)sender {
    (void)sender;
    _activeFilter = (FontFilter)_filterSegment.selectedSegment;
    [self rebuildCards];
}

#pragma mark - Content Size

- (NSSize)preferredContentSize {
    return NSMakeSize(780, 560);
}

- (void)dealloc {
    [_searchDebounce invalidate];
}

@end
