#import "PrefsClipboardViewController.h"

@interface PrefsClipboardViewController ()
@property (nonatomic, strong) NSPopUpButton* pasteProtectionPopup;
@property (nonatomic, strong) NSButton* bracketedSafeCheck;
@end

@implementation PrefsClipboardViewController

- (void)loadView {
    self.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 480, 260)];

    CGFloat labelWidth = 140;
    CGFloat margin = 20;

    // --- Paste Protection row ---
    NSTextField* protectionLabel = [NSTextField labelWithString:@"Paste Protection:"];
    protectionLabel.font = [NSFont systemFontOfSize:13];
    protectionLabel.alignment = NSTextAlignmentRight;
    protectionLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:protectionLabel];

    _pasteProtectionPopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    _pasteProtectionPopup.translatesAutoresizingMaskIntoConstraints = NO;
    [_pasteProtectionPopup addItemsWithTitles:@[@"Never", @"Multiline Only", @"Always"]];
    _pasteProtectionPopup.target = self;
    _pasteProtectionPopup.action = @selector(pasteProtectionChanged:);
    [self.view addSubview:_pasteProtectionPopup];

    // --- Bracketed Paste row ---
    NSTextField* bracketedLabel = [NSTextField labelWithString:@"Bracketed Paste:"];
    bracketedLabel.font = [NSFont systemFontOfSize:13];
    bracketedLabel.alignment = NSTextAlignmentRight;
    bracketedLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:bracketedLabel];

    _bracketedSafeCheck = [NSButton checkboxWithTitle:@"Trust Bracketed Paste"
                                               target:self
                                               action:@selector(bracketedSafeChanged:)];
    _bracketedSafeCheck.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_bracketedSafeCheck];

    // --- Description ---
    NSTextField* desc = [NSTextField wrappingLabelWithString:
        @"Never: Paste without any confirmation.\n"
         "Multiline Only: Warn when pasting text that contains newlines.\n"
         "Always: Warn before every paste operation.\n\n"
         "Trust Bracketed Paste: If enabled, pastes that use the terminal's bracketed "
         "paste mode are considered safe and will not trigger a warning."];
    desc.font = [NSFont systemFontOfSize:11];
    desc.textColor = [NSColor secondaryLabelColor];
    desc.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:desc];

    // --- Layout ---
    [NSLayoutConstraint activateConstraints:@[
        // Paste Protection row
        [protectionLabel.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:margin],
        [protectionLabel.topAnchor constraintEqualToAnchor:self.view.topAnchor constant:margin],
        [protectionLabel.widthAnchor constraintEqualToConstant:labelWidth],

        [_pasteProtectionPopup.leadingAnchor constraintEqualToAnchor:protectionLabel.trailingAnchor constant:8],
        [_pasteProtectionPopup.centerYAnchor constraintEqualToAnchor:protectionLabel.centerYAnchor],
        [_pasteProtectionPopup.widthAnchor constraintEqualToConstant:160],

        // Bracketed Paste row
        [bracketedLabel.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:margin],
        [bracketedLabel.topAnchor constraintEqualToAnchor:protectionLabel.bottomAnchor constant:16],
        [bracketedLabel.widthAnchor constraintEqualToConstant:labelWidth],

        [_bracketedSafeCheck.leadingAnchor constraintEqualToAnchor:bracketedLabel.trailingAnchor constant:8],
        [_bracketedSafeCheck.centerYAnchor constraintEqualToAnchor:bracketedLabel.centerYAnchor],

        // Description
        [desc.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:margin],
        [desc.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-margin],
        [desc.topAnchor constraintEqualToAnchor:bracketedLabel.bottomAnchor constant:20],
    ]];

    [self updateUI];
}

- (void)updateUI {
    const auto& prot = _config.clipboard_paste_protection;
    if (prot == "never") {
        [_pasteProtectionPopup selectItemAtIndex:0];
    } else if (prot == "multiline") {
        [_pasteProtectionPopup selectItemAtIndex:1];
    } else {
        [_pasteProtectionPopup selectItemAtIndex:2];
    }

    _bracketedSafeCheck.state = _config.clipboard_paste_bracketed_safe
                                    ? NSControlStateValueOn
                                    : NSControlStateValueOff;
}

#pragma mark - Actions

- (void)pasteProtectionChanged:(id)sender {
    (void)sender;
    NSInteger idx = _pasteProtectionPopup.indexOfSelectedItem;
    if (idx == 0) {
        _config.clipboard_paste_protection = "never";
    } else if (idx == 1) {
        _config.clipboard_paste_protection = "multiline";
    } else {
        _config.clipboard_paste_protection = "always";
    }

    if (self.saveBlock) {
        self.saveBlock(_config);
    }
}

- (void)bracketedSafeChanged:(id)sender {
    (void)sender;
    _config.clipboard_paste_bracketed_safe = (_bracketedSafeCheck.state == NSControlStateValueOn);

    if (self.saveBlock) {
        self.saveBlock(_config);
    }
}

- (NSSize)preferredContentSize {
    return NSMakeSize(480, 260);
}

@end
