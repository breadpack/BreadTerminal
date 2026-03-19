#import "PrefsFontViewController.h"

@interface PrefsFontViewController () <NSTextFieldDelegate>
@property (nonatomic, strong) NSTextField* fontLabel;
@property (nonatomic, strong) NSTextField* sizeField;
@property (nonatomic, strong) NSStepper* sizeStepper;
@property (nonatomic, strong) NSTextField* featuresField;
@property (nonatomic, strong) NSTextField* previewField;
@end

@implementation PrefsFontViewController

- (void)loadView {
    self.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 480, 360)];

    CGFloat labelWidth = 80;
    CGFloat margin = 20;

    // --- Font row ---
    NSTextField* fontRowLabel = [NSTextField labelWithString:@"Font:"];
    fontRowLabel.font = [NSFont systemFontOfSize:13];
    fontRowLabel.alignment = NSTextAlignmentRight;
    fontRowLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:fontRowLabel];

    _fontLabel = [NSTextField labelWithString:@""];
    _fontLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_fontLabel];

    NSButton* selectFontBtn = [NSButton buttonWithTitle:@"Select Font..."
                                                 target:self
                                                 action:@selector(selectFont:)];
    selectFontBtn.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:selectFontBtn];

    // --- Size row ---
    NSTextField* sizeRowLabel = [NSTextField labelWithString:@"Size:"];
    sizeRowLabel.font = [NSFont systemFontOfSize:13];
    sizeRowLabel.alignment = NSTextAlignmentRight;
    sizeRowLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:sizeRowLabel];

    _sizeField = [[NSTextField alloc] initWithFrame:NSZeroRect];
    _sizeField.translatesAutoresizingMaskIntoConstraints = NO;
    _sizeField.delegate = self;
    NSNumberFormatter* numFmt = [[NSNumberFormatter alloc] init];
    numFmt.numberStyle = NSNumberFormatterDecimalStyle;
    numFmt.minimum = @6;
    numFmt.maximum = @72;
    numFmt.allowsFloats = YES;
    _sizeField.formatter = numFmt;
    [self.view addSubview:_sizeField];

    _sizeStepper = [[NSStepper alloc] initWithFrame:NSZeroRect];
    _sizeStepper.translatesAutoresizingMaskIntoConstraints = NO;
    _sizeStepper.minValue = 6;
    _sizeStepper.maxValue = 72;
    _sizeStepper.increment = 1;
    _sizeStepper.valueWraps = NO;
    _sizeStepper.target = self;
    _sizeStepper.action = @selector(stepperChanged:);
    [self.view addSubview:_sizeStepper];

    // --- Features row ---
    NSTextField* featuresRowLabel = [NSTextField labelWithString:@"Features:"];
    featuresRowLabel.font = [NSFont systemFontOfSize:13];
    featuresRowLabel.alignment = NSTextAlignmentRight;
    featuresRowLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:featuresRowLabel];

    _featuresField = [[NSTextField alloc] initWithFrame:NSZeroRect];
    _featuresField.translatesAutoresizingMaskIntoConstraints = NO;
    _featuresField.placeholderString = @"calt,liga";
    _featuresField.delegate = self;
    [self.view addSubview:_featuresField];

    // --- Preview box ---
    NSTextField* previewLabel = [NSTextField labelWithString:@"Preview:"];
    previewLabel.font = [NSFont systemFontOfSize:13];
    previewLabel.alignment = NSTextAlignmentRight;
    previewLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:previewLabel];

    NSScrollView* scrollView = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    scrollView.translatesAutoresizingMaskIntoConstraints = NO;
    scrollView.hasVerticalScroller = YES;
    scrollView.borderType = NSBezelBorder;

    _previewField = [[NSTextField alloc] initWithFrame:NSZeroRect];
    _previewField.editable = NO;
    _previewField.selectable = YES;
    _previewField.bezeled = NO;
    _previewField.drawsBackground = NO;
    _previewField.maximumNumberOfLines = 0;
    _previewField.lineBreakMode = NSLineBreakByWordWrapping;
    _previewField.stringValue = @"The quick brown fox jumps over the lazy dog\n"
                                 "ABCDEF abcdef 0123456789 !@#$%^&*()";
    _previewField.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_previewField];

    // --- Layout ---
    [NSLayoutConstraint activateConstraints:@[
        // Font row
        [fontRowLabel.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:margin],
        [fontRowLabel.topAnchor constraintEqualToAnchor:self.view.topAnchor constant:margin],
        [fontRowLabel.widthAnchor constraintEqualToConstant:labelWidth],

        [_fontLabel.leadingAnchor constraintEqualToAnchor:fontRowLabel.trailingAnchor constant:8],
        [_fontLabel.centerYAnchor constraintEqualToAnchor:fontRowLabel.centerYAnchor],

        [selectFontBtn.leadingAnchor constraintEqualToAnchor:_fontLabel.trailingAnchor constant:8],
        [selectFontBtn.centerYAnchor constraintEqualToAnchor:fontRowLabel.centerYAnchor],
        [selectFontBtn.trailingAnchor constraintLessThanOrEqualToAnchor:self.view.trailingAnchor constant:-margin],

        // Size row
        [sizeRowLabel.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:margin],
        [sizeRowLabel.topAnchor constraintEqualToAnchor:fontRowLabel.bottomAnchor constant:16],
        [sizeRowLabel.widthAnchor constraintEqualToConstant:labelWidth],

        [_sizeField.leadingAnchor constraintEqualToAnchor:sizeRowLabel.trailingAnchor constant:8],
        [_sizeField.centerYAnchor constraintEqualToAnchor:sizeRowLabel.centerYAnchor],
        [_sizeField.widthAnchor constraintEqualToConstant:60],

        [_sizeStepper.leadingAnchor constraintEqualToAnchor:_sizeField.trailingAnchor constant:4],
        [_sizeStepper.centerYAnchor constraintEqualToAnchor:sizeRowLabel.centerYAnchor],

        // Features row
        [featuresRowLabel.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:margin],
        [featuresRowLabel.topAnchor constraintEqualToAnchor:sizeRowLabel.bottomAnchor constant:16],
        [featuresRowLabel.widthAnchor constraintEqualToConstant:labelWidth],

        [_featuresField.leadingAnchor constraintEqualToAnchor:featuresRowLabel.trailingAnchor constant:8],
        [_featuresField.centerYAnchor constraintEqualToAnchor:featuresRowLabel.centerYAnchor],
        [_featuresField.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-margin],

        // Preview
        [previewLabel.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:margin],
        [previewLabel.topAnchor constraintEqualToAnchor:featuresRowLabel.bottomAnchor constant:16],
        [previewLabel.widthAnchor constraintEqualToConstant:labelWidth],

        [_previewField.leadingAnchor constraintEqualToAnchor:previewLabel.trailingAnchor constant:8],
        [_previewField.topAnchor constraintEqualToAnchor:previewLabel.topAnchor],
        [_previewField.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-margin],
        [_previewField.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor constant:-margin],
    ]];

    [self updateUI];
}

- (void)updateUI {
    NSString* family = [NSString stringWithUTF8String:_config.font_family.c_str()];
    float size = _config.font_size;
    _fontLabel.stringValue = [NSString stringWithFormat:@"%@ %.0f pt", family, size];
    _sizeField.floatValue = size;
    _sizeStepper.doubleValue = size;

    // Features
    NSMutableArray<NSString*>* feats = [NSMutableArray new];
    for (const auto& f : _config.font_features) {
        [feats addObject:[NSString stringWithUTF8String:f.c_str()]];
    }
    _featuresField.stringValue = [feats componentsJoinedByString:@","];

    // Preview font
    NSFont* font = [NSFont fontWithName:family size:size];
    if (!font) {
        font = [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightRegular];
    }
    _previewField.font = font;
}

- (void)save {
    if (self.saveBlock) {
        self.saveBlock(_config);
    }
}

#pragma mark - Font Panel

- (void)selectFont:(id)sender {
    (void)sender;
    NSString* family = [NSString stringWithUTF8String:_config.font_family.c_str()];
    NSFont* current = [NSFont fontWithName:family size:_config.font_size];
    if (!current) {
        current = [NSFont monospacedSystemFontOfSize:_config.font_size weight:NSFontWeightRegular];
    }

    NSFontManager* fm = [NSFontManager sharedFontManager];
    [fm setSelectedFont:current isMultiple:NO];
    [fm orderFrontFontPanel:self];

    // Make us the first responder so we get changeFont:
    [self.view.window makeFirstResponder:self.view];
}

- (void)changeFont:(id)sender {
    NSFontManager* fm = (NSFontManager*)sender;
    NSString* family = [NSString stringWithUTF8String:_config.font_family.c_str()];
    NSFont* current = [NSFont fontWithName:family size:_config.font_size];
    if (!current) {
        current = [NSFont monospacedSystemFontOfSize:_config.font_size weight:NSFontWeightRegular];
    }

    NSFont* newFont = [fm convertFont:current];
    if (newFont) {
        _config.font_family = newFont.familyName.UTF8String;
        _config.font_size = static_cast<float>(newFont.pointSize);
        [self updateUI];
        [self save];
    }
}

#pragma mark - Size Stepper

- (void)stepperChanged:(NSStepper*)sender {
    _config.font_size = static_cast<float>(sender.doubleValue);
    [self updateUI];
    [self save];
}

#pragma mark - NSTextFieldDelegate

- (void)controlTextDidEndEditing:(NSNotification*)notification {
    NSTextField* field = notification.object;

    if (field == _sizeField) {
        float val = _sizeField.floatValue;
        if (val < 6) val = 6;
        if (val > 72) val = 72;
        _config.font_size = val;
        [self updateUI];
        [self save];
    } else if (field == _featuresField) {
        _config.font_features.clear();
        NSString* text = _featuresField.stringValue;
        NSArray<NSString*>* parts = [text componentsSeparatedByString:@","];
        for (NSString* part in parts) {
            NSString* trimmed = [part stringByTrimmingCharactersInSet:
                                 [NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if (trimmed.length > 0) {
                _config.font_features.push_back(trimmed.UTF8String);
            }
        }
        [self save];
    }
}

- (NSSize)preferredContentSize {
    return NSMakeSize(480, 360);
}

@end
