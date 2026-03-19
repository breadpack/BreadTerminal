#import "FontCardView.h"

static const CGFloat kFontCardWidth   = 220.0;
static const CGFloat kFontCardHeight  = 150.0;
static const CGFloat kCornerRadius    = 8.0;
static const CGFloat kPreviewHeight   = 80.0;
static const CGFloat kBadgeHeight     = 20.0;
static const CGFloat kBottomBarHeight = 30.0;
static const CGFloat kButtonWidth     = 70.0;
static const CGFloat kButtonHeight    = 22.0;

@implementation FontCardView {
    NSTextField* _nameLabel;
    NSButton* _actionButton;
    NSTrackingArea* _trackingArea;
    BOOL _hovered;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:NSMakeRect(frame.origin.x, frame.origin.y,
                                           kFontCardWidth, kFontCardHeight)];
    if (self) {
        self.wantsLayer = YES;
        self.layer.cornerRadius = kCornerRadius;
        self.layer.masksToBounds = YES;
        self.layer.borderWidth = 1.0;
        self.layer.borderColor = [NSColor.separatorColor CGColor];

        // Name label (bottom bar)
        _nameLabel = [NSTextField labelWithString:@""];
        _nameLabel.font = [NSFont systemFontOfSize:11 weight:NSFontWeightMedium];
        _nameLabel.lineBreakMode = NSLineBreakByTruncatingTail;
        _nameLabel.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:_nameLabel];

        // Action button
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

- (void)updateState {
    _nameLabel.stringValue = _fontName ?: @"";

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

    // --- Preview zone (top 80pt) ---
    CGFloat previewY = bounds.size.height - kPreviewHeight;
    NSRect previewRect = NSMakeRect(0, previewY, bounds.size.width, kPreviewHeight);

    if (_installed && _postscriptName.length > 0) {
        // Draw with actual font
        [[NSColor colorWithWhite:0.15 alpha:1.0] setFill];
        NSRectFill(previewRect);

        NSFont* font = [NSFont fontWithName:_postscriptName size:16];
        if (!font) {
            font = [NSFont monospacedSystemFontOfSize:16 weight:NSFontWeightRegular];
        }

        NSString* sampleText = @"AaBb 0123\n!= => ->";
        NSDictionary* attrs = @{
            NSFontAttributeName: font,
            NSForegroundColorAttributeName: [NSColor colorWithWhite:0.9 alpha:1.0],
        };
        NSSize textSize = [sampleText sizeWithAttributes:attrs];
        CGFloat textX = (bounds.size.width - textSize.width) / 2.0;
        CGFloat textY = previewY + (kPreviewHeight - textSize.height) / 2.0;
        [sampleText drawAtPoint:NSMakePoint(MAX(8, textX), textY) withAttributes:attrs];
    } else {
        // Gradient placeholder with font name
        NSGradient* gradient = [[NSGradient alloc]
            initWithStartingColor:[NSColor colorWithSRGBRed:0.2 green:0.2 blue:0.3 alpha:1.0]
                      endingColor:[NSColor colorWithSRGBRed:0.15 green:0.15 blue:0.25 alpha:1.0]];
        [gradient drawInRect:previewRect angle:135];

        NSString* displayName = _fontName ?: @"Font";
        NSDictionary* attrs = @{
            NSFontAttributeName: [NSFont systemFontOfSize:14 weight:NSFontWeightLight],
            NSForegroundColorAttributeName: [NSColor colorWithWhite:0.7 alpha:1.0],
        };
        NSSize textSize = [displayName sizeWithAttributes:attrs];
        CGFloat textX = (bounds.size.width - textSize.width) / 2.0;
        CGFloat textY = previewY + (kPreviewHeight - textSize.height) / 2.0;
        [displayName drawAtPoint:NSMakePoint(MAX(8, textX), textY) withAttributes:attrs];
    }

    // --- Badge strip (middle area) ---
    CGFloat badgeY = previewY - kBadgeHeight - 2;
    [[NSColor colorWithWhite:0.12 alpha:1.0] setFill];
    NSRectFill(NSMakeRect(0, badgeY, bounds.size.width, kBadgeHeight + 4));

    CGFloat badgeX = 8.0;
    if (_hasLigatures) {
        badgeX = [self drawBadge:@"Ligatures"
                           color:[NSColor systemBlueColor]
                              atX:badgeX y:badgeY + 2];
    }
    if (_hasNerdFontVariant) {
        [self drawBadge:@"Nerd Font"
                  color:[NSColor systemPurpleColor]
                    atX:badgeX y:badgeY + 2];
    }

    // --- Bottom bar background ---
    [[NSColor colorWithWhite:0.08 alpha:1.0] setFill];
    NSRectFill(NSMakeRect(0, 0, bounds.size.width, kBottomBarHeight + 2));

    // --- Hover border ---
    if (_hovered) {
        [[NSColor controlAccentColor] setStroke];
        NSBezierPath* border = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(bounds, 0.5, 0.5)
                                                               xRadius:kCornerRadius yRadius:kCornerRadius];
        border.lineWidth = 2.0;
        [border stroke];
    }
}

- (CGFloat)drawBadge:(NSString*)text
               color:(NSColor*)color
                 atX:(CGFloat)x y:(CGFloat)y {
    NSDictionary* attrs = @{
        NSFontAttributeName: [NSFont systemFontOfSize:9 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: [NSColor whiteColor],
    };
    NSSize textSize = [text sizeWithAttributes:attrs];
    CGFloat padH = 6.0, padV = 2.0;
    CGFloat badgeW = textSize.width + padH * 2;
    CGFloat badgeH = textSize.height + padV * 2;

    NSRect badgeRect = NSMakeRect(x, y, badgeW, badgeH);
    NSBezierPath* bgPath = [NSBezierPath bezierPathWithRoundedRect:badgeRect
                                                           xRadius:4 yRadius:4];
    [[color colorWithAlphaComponent:0.8] setFill];
    [bgPath fill];

    [text drawAtPoint:NSMakePoint(x + padH, y + padV) withAttributes:attrs];

    return x + badgeW + 6.0;
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
    return NSMakeSize(kFontCardWidth, kFontCardHeight);
}

@end
