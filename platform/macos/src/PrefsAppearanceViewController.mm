#import "PrefsAppearanceViewController.h"
#import "PrefsColorWellButton.h"

static NSColor* colorFromHex(uint32_t hex) {
    return [NSColor colorWithSRGBRed:((hex >> 16) & 0xFF) / 255.0
                               green:((hex >> 8) & 0xFF) / 255.0
                                blue:(hex & 0xFF) / 255.0
                               alpha:1.0];
}

static uint32_t hexFromColor(NSColor* color) {
    CGFloat r, g, b, a;
    [[color colorUsingColorSpace:[NSColorSpace sRGBColorSpace]] getRed:&r green:&g blue:&b alpha:&a];
    return ((uint32_t)(r * 255) << 16) | ((uint32_t)(g * 255) << 8) | (uint32_t)(b * 255);
}

@interface PrefsAppearanceViewController ()
@property (nonatomic, strong) PrefsColorWellButton* bgColorWell;
@property (nonatomic, strong) PrefsColorWellButton* fgColorWell;
@property (nonatomic, strong) PrefsColorWellButton* cursorColorWell;
@property (nonatomic, strong) PrefsColorWellButton* selBgColorWell;
@property (nonatomic, strong) PrefsColorWellButton* selFgColorWell;
@property (nonatomic, strong) NSSlider* opacitySlider;
@property (nonatomic, strong) NSTextField* opacityLabel;
@property (nonatomic, strong) NSPopUpButton* blurPopUp;
@property (nonatomic, strong) NSTimer* opacityTimer;
@end

@implementation PrefsAppearanceViewController {
    PrefsColorWellButton* _paletteWells[16];
}

- (void)loadView {
    self.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 520, 580)];

    // Helper to create a color well row
    __weak PrefsAppearanceViewController* weakSelf = self;

    auto makeColorWell = ^PrefsColorWellButton*(uint32_t hex) {
        PrefsColorWellButton* well = [[PrefsColorWellButton alloc] initWithFrame:NSMakeRect(0, 0, 44, 24)];
        well.hexColor = hex;
        well.colorWellStyle = NSColorWellStyleMinimal;
        [well setContentHuggingPriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];
        [well.widthAnchor constraintEqualToConstant:44].active = YES;
        [well.heightAnchor constraintEqualToConstant:24].active = YES;
        return well;
    };

    // --- Color rows ---
    NSTextField* bgLabel = [NSTextField labelWithString:@"Background:"];
    bgLabel.alignment = NSTextAlignmentRight;
    bgLabel.font = [NSFont systemFontOfSize:13];

    _bgColorWell = makeColorWell(_config.background);
    _bgColorWell.onColorChanged = ^(uint32_t newHex) {
        PrefsAppearanceViewController* vc = weakSelf; if (!vc) return;
        vc->_config.background = newHex;
        if (vc.saveBlock) vc.saveBlock(vc->_config);
    };

    NSTextField* fgLabel = [NSTextField labelWithString:@"Foreground:"];
    fgLabel.alignment = NSTextAlignmentRight;
    fgLabel.font = [NSFont systemFontOfSize:13];

    _fgColorWell = makeColorWell(_config.foreground);
    _fgColorWell.onColorChanged = ^(uint32_t newHex) {
        PrefsAppearanceViewController* vc = weakSelf; if (!vc) return;
        vc->_config.foreground = newHex;
        if (vc.saveBlock) vc.saveBlock(vc->_config);
    };

    NSTextField* cursorLabel = [NSTextField labelWithString:@"Cursor:"];
    cursorLabel.alignment = NSTextAlignmentRight;
    cursorLabel.font = [NSFont systemFontOfSize:13];

    _cursorColorWell = makeColorWell(_config.cursor_color);
    _cursorColorWell.onColorChanged = ^(uint32_t newHex) {
        PrefsAppearanceViewController* vc = weakSelf; if (!vc) return;
        vc->_config.cursor_color = newHex;
        if (vc.saveBlock) vc.saveBlock(vc->_config);
    };

    NSTextField* selBgLabel = [NSTextField labelWithString:@"Selection BG:"];
    selBgLabel.alignment = NSTextAlignmentRight;
    selBgLabel.font = [NSFont systemFontOfSize:13];

    _selBgColorWell = makeColorWell(_config.selection_background);
    _selBgColorWell.onColorChanged = ^(uint32_t newHex) {
        PrefsAppearanceViewController* vc = weakSelf; if (!vc) return;
        vc->_config.selection_background = newHex;
        if (vc.saveBlock) vc.saveBlock(vc->_config);
    };

    NSTextField* selFgLabel = [NSTextField labelWithString:@"Selection FG:"];
    selFgLabel.alignment = NSTextAlignmentRight;
    selFgLabel.font = [NSFont systemFontOfSize:13];

    _selFgColorWell = makeColorWell(_config.selection_foreground);
    _selFgColorWell.onColorChanged = ^(uint32_t newHex) {
        PrefsAppearanceViewController* vc = weakSelf; if (!vc) return;
        vc->_config.selection_foreground = newHex;
        if (vc.saveBlock) vc.saveBlock(vc->_config);
    };

    // --- Opacity ---
    NSTextField* opacityRowLabel = [NSTextField labelWithString:@"Opacity:"];
    opacityRowLabel.alignment = NSTextAlignmentRight;
    opacityRowLabel.font = [NSFont systemFontOfSize:13];

    _opacitySlider = [[NSSlider alloc] initWithFrame:NSZeroRect];
    _opacitySlider.minValue = 0.0;
    _opacitySlider.maxValue = 1.0;
    _opacitySlider.doubleValue = _config.background_opacity;
    _opacitySlider.continuous = YES;
    _opacitySlider.target = self;
    _opacitySlider.action = @selector(opacityChanged:);
    [_opacitySlider setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];

    _opacityLabel = [NSTextField labelWithString:[NSString stringWithFormat:@"%d%%", (int)(_config.background_opacity * 100)]];
    _opacityLabel.font = [NSFont monospacedDigitSystemFontOfSize:13 weight:NSFontWeightRegular];
    [_opacityLabel setContentHuggingPriority:NSLayoutPriorityRequired forOrientation:NSLayoutConstraintOrientationHorizontal];

    NSStackView* opacityStack = [NSStackView stackViewWithViews:@[_opacitySlider, _opacityLabel]];
    opacityStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    opacityStack.spacing = 8;

    // --- Background Blur ---
    NSTextField* blurLabel = [NSTextField labelWithString:@"Background Blur:"];
    blurLabel.alignment = NSTextAlignmentRight;
    blurLabel.font = [NSFont systemFontOfSize:13];

    _blurPopUp = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [_blurPopUp addItemsWithTitles:@[@"None", @"HUD Window", @"Sheet", @"Under Window"]];
    NSInteger blurIdx = 0;
    if (_config.background_blur_material == "hud_window") blurIdx = 1;
    else if (_config.background_blur_material == "sheet") blurIdx = 2;
    else if (_config.background_blur_material == "under_window") blurIdx = 3;
    [_blurPopUp selectItemAtIndex:blurIdx];
    _blurPopUp.target = self;
    _blurPopUp.action = @selector(blurChanged:);

    // --- Main grid ---
    NSGridView* grid = [NSGridView gridViewWithViews:@[
        @[bgLabel, _bgColorWell],
        @[fgLabel, _fgColorWell],
        @[cursorLabel, _cursorColorWell],
        @[selBgLabel, _selBgColorWell],
        @[selFgLabel, _selFgColorWell],
        @[opacityRowLabel, opacityStack],
        @[blurLabel, _blurPopUp],
    ]];
    grid.translatesAutoresizingMaskIntoConstraints = NO;
    grid.rowSpacing = 12;
    grid.columnSpacing = 10;
    [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    [grid columnAtIndex:1].xPlacement = NSGridCellPlacementLeading;

    [self.view addSubview:grid];

    // --- Separator ---
    NSBox* separator = [[NSBox alloc] initWithFrame:NSZeroRect];
    separator.boxType = NSBoxSeparator;
    separator.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:separator];

    // --- ANSI Palette section header ---
    NSTextField* paletteHeader = [NSTextField labelWithString:@"ANSI Palette"];
    paletteHeader.font = [NSFont systemFontOfSize:14 weight:NSFontWeightSemibold];
    paletteHeader.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:paletteHeader];

    // --- 4x4 palette grid ---
    NSGridView* paletteGrid = [self buildPaletteGridWithMakeColorWell:makeColorWell];
    paletteGrid.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:paletteGrid];

    // --- Layout ---
    [NSLayoutConstraint activateConstraints:@[
        [grid.topAnchor constraintEqualToAnchor:self.view.topAnchor constant:20],
        [grid.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:20],
        [grid.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-20],

        [separator.topAnchor constraintEqualToAnchor:grid.bottomAnchor constant:16],
        [separator.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:20],
        [separator.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-20],

        [paletteHeader.topAnchor constraintEqualToAnchor:separator.bottomAnchor constant:12],
        [paletteHeader.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:20],

        [paletteGrid.topAnchor constraintEqualToAnchor:paletteHeader.bottomAnchor constant:10],
        [paletteGrid.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:20],
        [paletteGrid.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-20],
    ]];
}

- (NSGridView*)buildPaletteGridWithMakeColorWell:(PrefsColorWellButton*(^)(uint32_t))makeColorWell {
    __weak PrefsAppearanceViewController* weakSelf = self;

    // Build 4 rows x 4 columns
    NSMutableArray<NSArray<NSView*>*>* rows = [NSMutableArray array];
    for (int row = 0; row < 4; row++) {
        NSMutableArray<NSView*>* rowViews = [NSMutableArray array];
        for (int col = 0; col < 4; col++) {
            int idx = row * 4 + col;
            PrefsColorWellButton* well = makeColorWell(_config.palette[idx]);
            _paletteWells[idx] = well;

            // Capture index for the block
            int capturedIdx = idx;
            well.onColorChanged = ^(uint32_t newHex) {
                PrefsAppearanceViewController* vc = weakSelf; if (!vc) return;
                vc->_config.palette[capturedIdx] = newHex;
                if (vc.saveBlock) vc.saveBlock(vc->_config);
            };

            // Label + well stack
            NSTextField* idxLabel = [NSTextField labelWithString:[NSString stringWithFormat:@"%d", idx]];
            idxLabel.font = [NSFont monospacedDigitSystemFontOfSize:10 weight:NSFontWeightRegular];
            idxLabel.textColor = [NSColor secondaryLabelColor];
            idxLabel.alignment = NSTextAlignmentCenter;

            NSStackView* cellStack = [NSStackView stackViewWithViews:@[idxLabel, well]];
            cellStack.orientation = NSUserInterfaceLayoutOrientationVertical;
            cellStack.spacing = 2;
            cellStack.alignment = NSLayoutAttributeCenterX;

            [rowViews addObject:cellStack];
        }
        [rows addObject:rowViews];
    }

    NSGridView* paletteGrid = [NSGridView gridViewWithViews:rows];
    paletteGrid.rowSpacing = 8;
    paletteGrid.columnSpacing = 12;
    for (NSInteger c = 0; c < paletteGrid.numberOfColumns; c++) {
        [paletteGrid columnAtIndex:c].xPlacement = NSGridCellPlacementCenter;
    }
    return paletteGrid;
}

- (NSSize)preferredContentSize {
    return NSMakeSize(520, 580);
}

#pragma mark - Actions

- (void)opacityChanged:(id)sender {
    (void)sender;
    double val = _opacitySlider.doubleValue;
    _opacityLabel.stringValue = [NSString stringWithFormat:@"%d%%", (int)(val * 100)];

    // Debounce save with 150ms timer
    [_opacityTimer invalidate];
    __weak PrefsAppearanceViewController* weakSelf = self;
    _opacityTimer = [NSTimer scheduledTimerWithTimeInterval:0.15
                                                   repeats:NO
                                                     block:^(NSTimer* timer) {
        (void)timer;
        PrefsAppearanceViewController* vc = weakSelf; if (!vc) return;
        vc->_config.background_opacity = (float)vc.opacitySlider.doubleValue;
        if (vc.saveBlock) vc.saveBlock(vc->_config);
    }];
}

- (void)blurChanged:(id)sender {
    (void)sender;
    static const char* materials[] = {"none", "hud_window", "sheet", "under_window"};
    NSInteger idx = _blurPopUp.indexOfSelectedItem;
    if (idx >= 0 && idx < 4)
        _config.background_blur_material = materials[idx];
    if (_saveBlock) _saveBlock(_config);
}

- (void)dealloc {
    [_opacityTimer invalidate];
}

@end
