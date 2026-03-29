#if defined(__APPLE__)

#import "UnifiedSettingsFontCards.h"
#import "UnifiedSettingsWindowController.h"

#include "termcore/font_index.h"
#include "termcore/config.h"

#import "FontDownloader.h"

#import <CoreText/CoreText.h>

static const CGFloat kCardWidth    = 220.0;
static const CGFloat kCardHeight   = 150.0;
static const CGFloat kCardGap      = 12.0;
static const CGFloat kGridPadding  = 24.0;
static const CGFloat kFilterBarH   = 40.0;

enum FontFilterType : NSInteger {
    FontFilterAll       = 0,
    FontFilterInstalled = 1,
    FontFilterNerdFonts = 2,
    FontFilterLigatures = 3,
};

@implementation UnifiedSettingsFontCards {
    __weak UnifiedSettingsWindowController* _controller;

    // Filter bar
    NSSegmentedControl* _filterSegment;
    NSSearchField* _localSearchField;
    FontFilterType _activeFilter;
    NSString* _localSearchQuery;

    // Grid
    NSView* _gridContainer;
    NSTimer* _searchDebounce;
}

- (instancetype)initWithController:(UnifiedSettingsWindowController*)controller {
    self = [super initWithFrame:NSMakeRect(0, 0, 500, 600)];
    if (self) {
        _controller = controller;
        _activeFilter = FontFilterAll;
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
    _filterSegment.frame = NSMakeRect(kGridPadding, 8, 320, 24);
    [self addSubview:_filterSegment];

    _localSearchField = [[NSSearchField alloc] initWithFrame:NSZeroRect];
    _localSearchField.placeholderString = @"Search fonts...";
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
    [_gridContainer removeFromSuperview];
    _gridContainer = [[NSView alloc] initWithFrame:NSZeroRect];
    [self addSubview:_gridContainer];

    UnifiedSettingsWindowController* ctrl = _controller;
    if (!ctrl) return;

    // Get filtered fonts
    std::vector<const termcore::FontMetadata*> fonts;

    if (_localSearchQuery.length > 0) {
        fonts = ctrl.fontIndex.search(std::string([_localSearchQuery UTF8String]));
    } else {
        switch (_activeFilter) {
            case FontFilterAll:
                for (const auto& f : ctrl.fontIndex.all()) fonts.push_back(&f);
                break;
            case FontFilterInstalled:
                fonts = ctrl.fontIndex.filter(true, false, false);
                break;
            case FontFilterNerdFonts:
                fonts = ctrl.fontIndex.filter(false, true, false);
                break;
            case FontFilterLigatures:
                fonts = ctrl.fontIndex.filter(false, false, true);
                break;
        }
    }

    std::string activeFont = ctrl.config.font_family;

    // Calculate grid layout
    CGFloat availWidth = self.frame.size.width;
    if (availWidth < kCardWidth + 2 * kGridPadding) availWidth = 500;
    NSInteger cols = (NSInteger)((availWidth - 2 * kGridPadding + kCardGap) / (kCardWidth + kCardGap));
    if (cols < 1) cols = 1;

    CGFloat totalGridWidth = cols * kCardWidth + (cols - 1) * kCardGap;
    CGFloat offsetX = (availWidth - totalGridWidth) / 2.0;

    NSInteger rows = ((NSInteger)fonts.size() + cols - 1) / cols;
    CGFloat gridHeight = rows * kCardHeight + (rows > 0 ? (rows - 1) * kCardGap : 0) + kGridPadding;

    _gridContainer.frame = NSMakeRect(0, kFilterBarH, availWidth, gridHeight);

    for (NSInteger i = 0; i < (NSInteger)fonts.size(); i++) {
        const termcore::FontMetadata* meta = fonts[i];

        NSInteger col = i % cols;
        NSInteger row = i / cols;

        CGFloat x = offsetX + col * (kCardWidth + kCardGap);
        CGFloat y = row * (kCardHeight + kCardGap);

        BOOL isActive = (meta->name == activeFont);
        NSView* card = [self createCardForFont:meta isActive:isActive
                                         frame:NSMakeRect(x, y, kCardWidth, kCardHeight)];
        [_gridContainer addSubview:card];
    }

    if (fonts.empty()) {
        NSTextField* emptyLabel = [NSTextField labelWithString:@"No fonts found"];
        emptyLabel.font = [NSFont systemFontOfSize:14];
        emptyLabel.textColor = [NSColor secondaryLabelColor];
        emptyLabel.alignment = NSTextAlignmentCenter;
        emptyLabel.frame = NSMakeRect(0, 40, availWidth, 24);
        [_gridContainer addSubview:emptyLabel];
    }

    CGFloat totalHeight = kFilterBarH + gridHeight + kGridPadding;
    self.frame = NSMakeRect(0, 0, availWidth, totalHeight);
}

#pragma mark - Card Creation

- (NSView*)createCardForFont:(const termcore::FontMetadata*)meta
                    isActive:(BOOL)isActive
                       frame:(NSRect)frame {
    NSView* card = [[NSView alloc] initWithFrame:frame];
    card.wantsLayer = YES;
    card.layer.cornerRadius = 8.0;
    card.layer.borderWidth = isActive ? 2.0 : 1.0;
    card.layer.backgroundColor = [NSColor controlBackgroundColor].CGColor;

    if (isActive) {
        card.layer.borderColor = [NSColor controlAccentColor].CGColor;
    } else {
        card.layer.borderColor = [NSColor separatorColor].CGColor;
    }

    // Font name
    NSString* fontName = [NSString stringWithUTF8String:meta->name.c_str()];
    NSTextField* nameLabel = [NSTextField labelWithString:fontName];
    nameLabel.font = [NSFont boldSystemFontOfSize:12];
    nameLabel.textColor = [NSColor labelColor];
    nameLabel.frame = NSMakeRect(10, 10, frame.size.width - 20, 18);
    nameLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    [card addSubview:nameLabel];

    // Font preview text
    NSString* previewText = @"ABCDEFG abcdefg 0123456789";
    NSFont* previewFont = [NSFont fontWithName:fontName size:13];
    if (!previewFont) previewFont = [NSFont monospacedSystemFontOfSize:13 weight:NSFontWeightRegular];

    NSTextField* previewLabel = [NSTextField labelWithString:previewText];
    previewLabel.font = previewFont;
    previewLabel.textColor = [NSColor labelColor];
    previewLabel.frame = NSMakeRect(10, 34, frame.size.width - 20, 40);
    previewLabel.maximumNumberOfLines = 2;
    previewLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    [card addSubview:previewLabel];

    // Badges
    CGFloat badgeX = 10;
    CGFloat badgeY = 80;

    if (meta->has_nerd_font_variant) {
        NSView* badge = [self createBadge:@"NerdFont" frame:NSMakeRect(badgeX, badgeY, 70, 18)];
        [card addSubview:badge];
        badgeX += 76;
    }

    if (meta->has_ligatures) {
        NSView* badge = [self createBadge:@"Ligatures" frame:NSMakeRect(badgeX, badgeY, 70, 18)];
        [card addSubview:badge];
        badgeX += 76;
    }

    if (meta->category == "system") {
        NSView* badge = [self createBadge:@"System" frame:NSMakeRect(badgeX, badgeY, 60, 18)];
        [card addSubview:badge];
    }

    // Action button
    NSButton* actionButton;
    if (isActive) {
        actionButton = [NSButton buttonWithTitle:@"Active" target:nil action:nil];
        actionButton.enabled = NO;
    } else if (meta->installed) {
        actionButton = [NSButton buttonWithTitle:@"Apply" target:self action:@selector(fontCardAction:)];
    } else {
        actionButton = [NSButton buttonWithTitle:@"Install" target:self action:@selector(fontCardAction:)];
    }
    actionButton.bezelStyle = NSBezelStyleRounded;
    actionButton.controlSize = NSControlSizeMini;
    actionButton.frame = NSMakeRect(frame.size.width - 70, frame.size.height - 32, 60, 22);
    actionButton.accessibilityIdentifier = fontName;
    [card addSubview:actionButton];

    return card;
}

- (NSView*)createBadge:(NSString*)text frame:(NSRect)frame {
    NSView* badge = [[NSView alloc] initWithFrame:frame];
    badge.wantsLayer = YES;
    badge.layer.cornerRadius = 4.0;
    badge.layer.backgroundColor = [NSColor tertiaryLabelColor].CGColor;

    NSTextField* label = [NSTextField labelWithString:text];
    label.font = [NSFont systemFontOfSize:9 weight:NSFontWeightMedium];
    label.textColor = [NSColor labelColor];
    label.alignment = NSTextAlignmentCenter;
    label.frame = NSMakeRect(0, 0, frame.size.width, frame.size.height);
    [badge addSubview:label];

    return badge;
}

#pragma mark - Actions

- (void)fontCardAction:(NSButton*)sender {
    NSString* fontName = sender.accessibilityIdentifier;
    if (!fontName) return;

    std::string name([fontName UTF8String]);
    UnifiedSettingsWindowController* ctrl = _controller;
    if (!ctrl) return;

    // Check if installed
    bool installed = false;
    for (const auto& f : ctrl.fontIndex.all()) {
        if (f.name == name) {
            installed = f.installed;
            break;
        }
    }

    if (installed) {
        [self applyFont:name];
    } else {
        [self downloadFont:name];
    }
}

- (void)applyFont:(const std::string&)name {
    UnifiedSettingsWindowController* ctrl = _controller;
    if (!ctrl) return;

    ctrl.config.font_family = name;
    [ctrl configDidChange];
    [self reloadCards];
}

- (void)downloadFont:(const std::string&)name {
    UnifiedSettingsWindowController* ctrl = _controller;
    if (!ctrl) return;

    // Find download URL
    std::string url;
    for (const auto& f : ctrl.fontIndex.all()) {
        if (f.name == name) {
            url = f.download_url;
            break;
        }
    }

    if (url.empty()) {
        NSLog(@"UnifiedSettings: no download URL for font '%s'", name.c_str());
        return;
    }

    NSString* nsName = [NSString stringWithUTF8String:name.c_str()];
    NSString* nsURL = [NSString stringWithUTF8String:url.c_str()];

    __weak UnifiedSettingsFontCards* weakSelf = self;
    [[FontDownloader sharedDownloader] downloadFont:nsName
                                            fromURL:nsURL
                                           progress:nil
                                         completion:^(BOOL success, NSError* error) {
        UnifiedSettingsFontCards* strongSelf = weakSelf;
        if (!strongSelf) return;

        UnifiedSettingsWindowController* ctrl2 = strongSelf->_controller;
        if (success && ctrl2) {
            ctrl2.fontIndex.markInstalled(std::string([nsName UTF8String]));
            ctrl2.fontIndex.refreshInstallStatus();
            [strongSelf reloadCards];
        } else {
            NSLog(@"UnifiedSettings: font download failed for '%@': %@",
                  nsName, error.localizedDescription);
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

#pragma mark - Filter / Search

- (void)filterChanged:(id)sender {
    (void)sender;
    _activeFilter = (FontFilterType)_filterSegment.selectedSegment;
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
    __weak UnifiedSettingsFontCards* weakSelf = self;
    _searchDebounce = [NSTimer scheduledTimerWithTimeInterval:0.2
                                                      repeats:NO
                                                        block:^(NSTimer* timer) {
        (void)timer;
        UnifiedSettingsFontCards* s = weakSelf;
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
