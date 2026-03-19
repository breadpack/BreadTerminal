#import "PrefsGeneralViewController.h"

#include <set>
#include <string>
#include <vector>
#include "termcore/theme_loader.h"

@interface PrefsGeneralViewController ()
@property (nonatomic, strong) NSTextField* shellField;
@property (nonatomic, strong) NSTextField* scrollbackField;
@property (nonatomic, strong) NSStepper* scrollbackStepper;
@property (nonatomic, strong) NSPopUpButton* cursorStylePopUp;
@property (nonatomic, strong) NSButton* cursorBlinkCheck;
@property (nonatomic, strong) NSPopUpButton* themePopUp;
@end

@implementation PrefsGeneralViewController {
    std::vector<std::string> _themeNames;
}

- (void)loadView {
    self.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 480, 320)];

    // --- Shell ---
    NSTextField* shellLabel = [NSTextField labelWithString:@"Shell:"];
    shellLabel.alignment = NSTextAlignmentRight;
    shellLabel.font = [NSFont systemFontOfSize:13];

    _shellField = [[NSTextField alloc] initWithFrame:NSZeroRect];
    _shellField.placeholderString = @"$SHELL default";
    _shellField.font = [NSFont systemFontOfSize:13];
    _shellField.stringValue = [NSString stringWithUTF8String:_config.shell.c_str()];
    _shellField.target = self;
    _shellField.action = @selector(shellChanged:);
    [_shellField setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];

    // --- Scrollback ---
    NSTextField* scrollbackLabel = [NSTextField labelWithString:@"Scrollback:"];
    scrollbackLabel.alignment = NSTextAlignmentRight;
    scrollbackLabel.font = [NSFont systemFontOfSize:13];

    _scrollbackField = [[NSTextField alloc] initWithFrame:NSZeroRect];
    _scrollbackField.font = [NSFont systemFontOfSize:13];
    NSNumberFormatter* numFmt = [[NSNumberFormatter alloc] init];
    numFmt.numberStyle = NSNumberFormatterDecimalStyle;
    numFmt.minimum = @(1000);
    numFmt.maximum = @(1000000);
    numFmt.allowsFloats = NO;
    _scrollbackField.formatter = numFmt;
    _scrollbackField.integerValue = _config.scrollback_limit;
    _scrollbackField.target = self;
    _scrollbackField.action = @selector(scrollbackFieldChanged:);

    _scrollbackStepper = [[NSStepper alloc] initWithFrame:NSZeroRect];
    _scrollbackStepper.minValue = 1000;
    _scrollbackStepper.maxValue = 1000000;
    _scrollbackStepper.increment = 1000;
    _scrollbackStepper.integerValue = _config.scrollback_limit;
    _scrollbackStepper.valueWraps = NO;
    _scrollbackStepper.target = self;
    _scrollbackStepper.action = @selector(scrollbackStepperChanged:);

    NSStackView* scrollbackStack = [NSStackView stackViewWithViews:@[_scrollbackField, _scrollbackStepper]];
    scrollbackStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    scrollbackStack.spacing = 4;
    [_scrollbackField setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];

    // --- Cursor Style ---
    NSTextField* cursorStyleLabel = [NSTextField labelWithString:@"Cursor Style:"];
    cursorStyleLabel.alignment = NSTextAlignmentRight;
    cursorStyleLabel.font = [NSFont systemFontOfSize:13];

    _cursorStylePopUp = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [_cursorStylePopUp addItemsWithTitles:@[@"Block", @"Underline", @"Bar"]];
    NSString* curStyle = [NSString stringWithUTF8String:_config.cursor_style.c_str()];
    if ([curStyle isEqualToString:@"underline"]) {
        [_cursorStylePopUp selectItemAtIndex:1];
    } else if ([curStyle isEqualToString:@"bar"]) {
        [_cursorStylePopUp selectItemAtIndex:2];
    } else {
        [_cursorStylePopUp selectItemAtIndex:0];
    }
    _cursorStylePopUp.target = self;
    _cursorStylePopUp.action = @selector(cursorStyleChanged:);

    // --- Cursor Blink ---
    NSTextField* cursorBlinkLabel = [NSTextField labelWithString:@"Cursor Blink:"];
    cursorBlinkLabel.alignment = NSTextAlignmentRight;
    cursorBlinkLabel.font = [NSFont systemFontOfSize:13];

    _cursorBlinkCheck = [NSButton checkboxWithTitle:@"" target:self action:@selector(cursorBlinkChanged:)];
    _cursorBlinkCheck.state = _config.cursor_blink ? NSControlStateValueOn : NSControlStateValueOff;

    // --- Theme ---
    NSTextField* themeLabel = [NSTextField labelWithString:@"Theme:"];
    themeLabel.alignment = NSTextAlignmentRight;
    themeLabel.font = [NSFont systemFontOfSize:13];

    _themePopUp = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    // Populate with all available themes (built-in + user directory)
    auto allThemes = termcore::allAvailableThemes();
    _themeNames.clear();
    bool addedSeparator = false;
    auto builtinNames = termcore::listBuiltinThemes();
    std::set<std::string> builtinSet(builtinNames.begin(), builtinNames.end());
    // Built-in themes first
    for (const auto& t : allThemes) {
        if (builtinSet.count(t.name)) {
            _themeNames.push_back(t.name);
            [_themePopUp addItemWithTitle:[NSString stringWithUTF8String:t.name.c_str()]];
        }
    }
    // Separator + user themes
    for (const auto& t : allThemes) {
        if (!builtinSet.count(t.name)) {
            if (!addedSeparator) {
                [[_themePopUp menu] addItem:[NSMenuItem separatorItem]];
                addedSeparator = true;
            }
            _themeNames.push_back(t.name);
            [_themePopUp addItemWithTitle:[NSString stringWithUTF8String:t.name.c_str()]];
        }
    }
    [[_themePopUp menu] addItem:[NSMenuItem separatorItem]];
    [_themePopUp addItemWithTitle:@"Custom"];

    // Select current theme
    NSInteger selectedIndex = (NSInteger)_themeNames.size(); // default to "Custom"
    for (NSInteger i = 0; i < (NSInteger)_themeNames.size(); ++i) {
        if (_themeNames[i] == _config.theme) {
            selectedIndex = i;
            break;
        }
    }
    [_themePopUp selectItemAtIndex:selectedIndex];
    _themePopUp.target = self;
    _themePopUp.action = @selector(themeChanged:);

    // --- Grid layout ---
    NSGridView* grid = [NSGridView gridViewWithViews:@[
        @[shellLabel, _shellField],
        @[scrollbackLabel, scrollbackStack],
        @[cursorStyleLabel, _cursorStylePopUp],
        @[cursorBlinkLabel, _cursorBlinkCheck],
        @[themeLabel, _themePopUp],
    ]];
    grid.translatesAutoresizingMaskIntoConstraints = NO;
    grid.rowSpacing = 12;
    grid.columnSpacing = 10;
    [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
    [grid columnAtIndex:1].xPlacement = NSGridCellPlacementLeading;

    [self.view addSubview:grid];

    [NSLayoutConstraint activateConstraints:@[
        [grid.topAnchor constraintEqualToAnchor:self.view.topAnchor constant:20],
        [grid.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:20],
        [grid.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-20],
    ]];
}

- (NSSize)preferredContentSize {
    return NSMakeSize(480, 320);
}

#pragma mark - Actions

- (void)shellChanged:(id)sender {
    (void)sender;
    _config.shell = _shellField.stringValue.UTF8String ?: "";
    if (_saveBlock) _saveBlock(_config);
}

- (void)scrollbackFieldChanged:(id)sender {
    (void)sender;
    NSInteger val = _scrollbackField.integerValue;
    if (val < 1000) val = 1000;
    if (val > 1000000) val = 1000000;
    _config.scrollback_limit = (int)val;
    _scrollbackStepper.integerValue = val;
    if (_saveBlock) _saveBlock(_config);
}

- (void)scrollbackStepperChanged:(id)sender {
    (void)sender;
    NSInteger val = _scrollbackStepper.integerValue;
    _config.scrollback_limit = (int)val;
    _scrollbackField.integerValue = val;
    if (_saveBlock) _saveBlock(_config);
}

- (void)cursorStyleChanged:(id)sender {
    (void)sender;
    static const char* styles[] = {"block", "underline", "bar"};
    NSInteger idx = _cursorStylePopUp.indexOfSelectedItem;
    if (idx >= 0 && idx < 3) {
        _config.cursor_style = styles[idx];
    }
    if (_saveBlock) _saveBlock(_config);
}

- (void)cursorBlinkChanged:(id)sender {
    (void)sender;
    _config.cursor_blink = (_cursorBlinkCheck.state == NSControlStateValueOn);
    if (_saveBlock) _saveBlock(_config);
}

- (void)themeChanged:(id)sender {
    (void)sender;
    NSInteger idx = _themePopUp.indexOfSelectedItem;
    // Account for separator menu items (they shift indices)
    NSString* title = _themePopUp.titleOfSelectedItem;
    if ([title isEqualToString:@"Custom"]) {
        _config.theme = "";
    } else {
        std::string name = title.UTF8String ?: "";
        auto theme = termcore::findTheme(name);
        if (theme) {
            termcore::applyTheme(_config, *theme);
            _config.theme = name;
        }
    }
    if (_saveBlock) _saveBlock(_config);
}

@end
