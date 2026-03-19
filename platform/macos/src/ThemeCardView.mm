#import "ThemeCardView.h"

static const CGFloat kCardWidth  = 200.0;
static const CGFloat kCardHeight = 130.0;
static const CGFloat kCornerRadius = 8.0;
static const CGFloat kPreviewHeight = 60.0;
static const CGFloat kSwatchSize = 14.0;
static const CGFloat kSwatchSpacing = 3.0;
static const CGFloat kSwatchPadding = 10.0;
static const CGFloat kButtonHeight = 22.0;
static const CGFloat kButtonWidth  = 70.0;

static NSColor* colorFrom32(uint32_t hex) {
    return [NSColor colorWithSRGBRed:((hex >> 16) & 0xFF) / 255.0
                               green:((hex >> 8) & 0xFF) / 255.0
                                blue:(hex & 0xFF) / 255.0
                               alpha:1.0];
}

static CGFloat luminance(uint32_t hex) {
    CGFloat r = ((hex >> 16) & 0xFF) / 255.0;
    CGFloat g = ((hex >> 8) & 0xFF) / 255.0;
    CGFloat b = (hex & 0xFF) / 255.0;
    return 0.299 * r + 0.587 * g + 0.114 * b;
}

@implementation ThemeCardView {
    uint32_t _palette[16];
    NSButton* _actionButton;
    NSTextField* _nameLabel;
    NSTrackingArea* _trackingArea;
    BOOL _hovered;
}

- (void)setPaletteColor:(uint32_t)color atIndex:(NSUInteger)index {
    if (index < 16) _palette[index] = color;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:NSMakeRect(frame.origin.x, frame.origin.y, kCardWidth, kCardHeight)];
    if (self) {
        self.wantsLayer = YES;
        self.layer.cornerRadius = kCornerRadius;
        self.layer.masksToBounds = YES;
        self.layer.borderWidth = 1.0;
        self.layer.borderColor = [NSColor.separatorColor CGColor];

        _nameLabel = [NSTextField labelWithString:@""];
        _nameLabel.font = [NSFont systemFontOfSize:11 weight:NSFontWeightMedium];
        _nameLabel.lineBreakMode = NSLineBreakByTruncatingTail;
        _nameLabel.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:_nameLabel];

        _actionButton = [NSButton buttonWithTitle:@"Install"
                                           target:self
                                           action:@selector(actionClicked:)];
        _actionButton.controlSize = NSControlSizeMini;
        _actionButton.bezelStyle = NSBezelStyleRecessed;
        _actionButton.font = [NSFont systemFontOfSize:10 weight:NSFontWeightMedium];
        _actionButton.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:_actionButton];

        [NSLayoutConstraint activateConstraints:@[
            [_nameLabel.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:8],
            [_nameLabel.trailingAnchor constraintLessThanOrEqualToAnchor:_actionButton.leadingAnchor constant:-4],
            [_nameLabel.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-6],

            [_actionButton.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-6],
            [_actionButton.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-5],
            [_actionButton.widthAnchor constraintEqualToConstant:kButtonWidth],
            [_actionButton.heightAnchor constraintEqualToConstant:kButtonHeight],
        ]];

        _hovered = NO;
    }
    return self;
}

- (void)updateColors {
    _nameLabel.stringValue = _themeName ?: @"";

    BOOL lightBg = luminance(_backgroundColor) > 0.5;
    _nameLabel.textColor = lightBg ? [NSColor blackColor] : [NSColor whiteColor];

    if (_isActive) {
        _actionButton.title = @"\u2713 Active";
        _actionButton.enabled = NO;
    } else if (_installed) {
        _actionButton.title = @"Apply";
        _actionButton.enabled = YES;
    } else {
        _actionButton.title = @"Install";
        _actionButton.enabled = YES;
    }

    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)dirtyRect {
    [super drawRect:dirtyRect];

    NSRect bounds = self.bounds;

    // Background color fill (top preview area)
    NSColor* bgColor = colorFrom32(_backgroundColor);
    [bgColor setFill];
    NSRectFill(NSMakeRect(0, bounds.size.height - kPreviewHeight,
                          bounds.size.width, kPreviewHeight));

    // Sample text in preview area
    NSColor* fgColor = colorFrom32(_foregroundColor);
    NSDictionary* textAttrs = @{
        NSFontAttributeName: [NSFont monospacedSystemFontOfSize:10 weight:NSFontWeightRegular],
        NSForegroundColorAttributeName: fgColor,
    };
    NSString* sampleText = @"$ hello world";
    NSSize textSize = [sampleText sizeWithAttributes:textAttrs];
    CGFloat textY = bounds.size.height - kPreviewHeight + (kPreviewHeight - textSize.height) / 2.0;
    [sampleText drawAtPoint:NSMakePoint(10, textY) withAttributes:textAttrs];

    // Middle band background (palette area)
    CGFloat paletteAreaTop = bounds.size.height - kPreviewHeight;
    CGFloat paletteAreaHeight = paletteAreaTop - kButtonHeight - 6;
    [[NSColor colorWithWhite:luminance(_backgroundColor) > 0.5 ? 0.95 : 0.12 alpha:1.0] setFill];
    NSRectFill(NSMakeRect(0, kButtonHeight + 6, bounds.size.width, paletteAreaHeight));

    // Bottom band (button area)
    [[NSColor colorWithWhite:luminance(_backgroundColor) > 0.5 ? 0.92 : 0.08 alpha:1.0] setFill];
    NSRectFill(NSMakeRect(0, 0, bounds.size.width, kButtonHeight + 8));

    // Draw palette swatches - row 1 (colors 0-7)
    CGFloat rowY1 = paletteAreaTop - kSwatchSize - 6;
    CGFloat rowY2 = rowY1 - kSwatchSize - kSwatchSpacing;
    CGFloat startX = kSwatchPadding;

    for (int i = 0; i < 8; i++) {
        NSColor* c = colorFrom32(_palette[i]);
        [c setFill];
        NSRect swatchRect = NSMakeRect(startX + i * (kSwatchSize + kSwatchSpacing),
                                        rowY1, kSwatchSize, kSwatchSize);
        NSBezierPath* path = [NSBezierPath bezierPathWithRoundedRect:swatchRect
                                                             xRadius:2 yRadius:2];
        [path fill];
    }

    // Row 2 (colors 8-15)
    for (int i = 0; i < 8; i++) {
        NSColor* c = colorFrom32(_palette[8 + i]);
        [c setFill];
        NSRect swatchRect = NSMakeRect(startX + i * (kSwatchSize + kSwatchSpacing),
                                        rowY2, kSwatchSize, kSwatchSize);
        NSBezierPath* path = [NSBezierPath bezierPathWithRoundedRect:swatchRect
                                                             xRadius:2 yRadius:2];
        [path fill];
    }

    // Hover border highlight
    if (_hovered) {
        [[NSColor controlAccentColor] setStroke];
        NSBezierPath* border = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(bounds, 0.5, 0.5)
                                                               xRadius:kCornerRadius yRadius:kCornerRadius];
        border.lineWidth = 2.0;
        [border stroke];
    }
}

- (void)actionClicked:(id)sender {
    (void)sender;
    if (_onAction) _onAction();
}

#pragma mark - Tracking / Hover

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_trackingArea) [self removeTrackingArea:_trackingArea];
    _trackingArea = [[NSTrackingArea alloc]
        initWithRect:self.bounds
             options:(NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow)
               owner:self
            userInfo:nil];
    [self addTrackingArea:_trackingArea];
}

- (void)mouseEntered:(NSEvent*)event {
    (void)event;
    _hovered = YES;
    [self setNeedsDisplay:YES];
}

- (void)mouseExited:(NSEvent*)event {
    (void)event;
    _hovered = NO;
    [self setNeedsDisplay:YES];
}

- (NSSize)intrinsicContentSize {
    return NSMakeSize(kCardWidth, kCardHeight);
}

@end
