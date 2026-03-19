#import "PrefsSidebarViewController.h"

@interface PrefsSidebarViewController ()
@property (nonatomic, strong) NSButton* visibleCheck;
@property (nonatomic, strong) NSSlider* widthSlider;
@property (nonatomic, strong) NSTextField* widthValueLabel;
@property (nonatomic, strong) NSTimer* debounceTimer;
@end

@implementation PrefsSidebarViewController

- (void)loadView {
    self.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 480, 200)];

    CGFloat labelWidth = 120;
    CGFloat margin = 20;

    // --- Show Sidebar row ---
    NSTextField* visibleLabel = [NSTextField labelWithString:@"Show Sidebar:"];
    visibleLabel.font = [NSFont systemFontOfSize:13];
    visibleLabel.alignment = NSTextAlignmentRight;
    visibleLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:visibleLabel];

    _visibleCheck = [NSButton checkboxWithTitle:@""
                                         target:self
                                         action:@selector(visibleChanged:)];
    _visibleCheck.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_visibleCheck];

    // --- Sidebar Width row ---
    NSTextField* widthLabel = [NSTextField labelWithString:@"Sidebar Width:"];
    widthLabel.font = [NSFont systemFontOfSize:13];
    widthLabel.alignment = NSTextAlignmentRight;
    widthLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:widthLabel];

    _widthSlider = [[NSSlider alloc] initWithFrame:NSZeroRect];
    _widthSlider.translatesAutoresizingMaskIntoConstraints = NO;
    _widthSlider.minValue = 160;
    _widthSlider.maxValue = 400;
    _widthSlider.continuous = YES;
    _widthSlider.target = self;
    _widthSlider.action = @selector(widthSliderChanged:);
    [self.view addSubview:_widthSlider];

    _widthValueLabel = [NSTextField labelWithString:@"220"];
    _widthValueLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _widthValueLabel.alignment = NSTextAlignmentCenter;
    [self.view addSubview:_widthValueLabel];

    // --- Layout ---
    [NSLayoutConstraint activateConstraints:@[
        // Show Sidebar row
        [visibleLabel.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:margin],
        [visibleLabel.topAnchor constraintEqualToAnchor:self.view.topAnchor constant:margin],
        [visibleLabel.widthAnchor constraintEqualToConstant:labelWidth],

        [_visibleCheck.leadingAnchor constraintEqualToAnchor:visibleLabel.trailingAnchor constant:8],
        [_visibleCheck.centerYAnchor constraintEqualToAnchor:visibleLabel.centerYAnchor],

        // Sidebar Width row
        [widthLabel.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:margin],
        [widthLabel.topAnchor constraintEqualToAnchor:visibleLabel.bottomAnchor constant:16],
        [widthLabel.widthAnchor constraintEqualToConstant:labelWidth],

        [_widthSlider.leadingAnchor constraintEqualToAnchor:widthLabel.trailingAnchor constant:8],
        [_widthSlider.centerYAnchor constraintEqualToAnchor:widthLabel.centerYAnchor],
        [_widthSlider.widthAnchor constraintEqualToConstant:180],

        [_widthValueLabel.leadingAnchor constraintEqualToAnchor:_widthSlider.trailingAnchor constant:8],
        [_widthValueLabel.centerYAnchor constraintEqualToAnchor:widthLabel.centerYAnchor],
        [_widthValueLabel.widthAnchor constraintEqualToConstant:40],
    ]];

    [self updateUI];
}

- (void)updateUI {
    _visibleCheck.state = _config.sidebar_visible
                              ? NSControlStateValueOn
                              : NSControlStateValueOff;
    _widthSlider.intValue = _config.sidebar_width;
    _widthValueLabel.stringValue = [NSString stringWithFormat:@"%d", _config.sidebar_width];
}

#pragma mark - Actions

- (void)visibleChanged:(id)sender {
    (void)sender;
    _config.sidebar_visible = (_visibleCheck.state == NSControlStateValueOn);

    if (self.saveBlock) {
        self.saveBlock(_config);
    }
}

- (void)widthSliderChanged:(id)sender {
    (void)sender;
    int value = _widthSlider.intValue;
    _widthValueLabel.stringValue = [NSString stringWithFormat:@"%d", value];

    // Debounce: only save after 150ms of no changes
    [_debounceTimer invalidate];
    __weak PrefsSidebarViewController* weakSelf = self;
    _debounceTimer = [NSTimer scheduledTimerWithTimeInterval:0.15
                                                     repeats:NO
                                                       block:^(NSTimer* timer) {
        (void)timer;
        PrefsSidebarViewController* strongSelf = weakSelf;
        if (!strongSelf) return;
        strongSelf->_config.sidebar_width = value;
        if (strongSelf.saveBlock) {
            strongSelf.saveBlock(strongSelf->_config);
        }
    }];
}

- (void)dealloc {
    [_debounceTimer invalidate];
}

- (NSSize)preferredContentSize {
    return NSMakeSize(480, 200);
}

@end
