#import "PrefsKeybindingsViewController.h"

static NSString* const kTriggerColumnID = @"TriggerColumn";
static NSString* const kActionColumnID  = @"ActionColumn";

@interface PrefsKeybindingsViewController () <NSTableViewDataSource, NSTableViewDelegate, NSTextFieldDelegate>
@property (nonatomic, strong) NSTableView* tableView;
@end

@implementation PrefsKeybindingsViewController

- (void)loadView {
    self.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 520, 400)];

    CGFloat margin = 20;

    // --- Scroll view + Table view ---
    NSScrollView* scrollView = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    scrollView.translatesAutoresizingMaskIntoConstraints = NO;
    scrollView.hasVerticalScroller = YES;
    scrollView.borderType = NSBezelBorder;

    _tableView = [[NSTableView alloc] initWithFrame:NSZeroRect];
    _tableView.usesAlternatingRowBackgroundColors = YES;
    _tableView.dataSource = self;
    _tableView.delegate = self;
    _tableView.rowHeight = 24;

    NSTableColumn* triggerCol = [[NSTableColumn alloc] initWithIdentifier:kTriggerColumnID];
    triggerCol.title = @"Trigger";
    triggerCol.width = 200;
    triggerCol.editable = YES;
    [_tableView addTableColumn:triggerCol];

    NSTableColumn* actionCol = [[NSTableColumn alloc] initWithIdentifier:kActionColumnID];
    actionCol.title = @"Action";
    actionCol.width = 260;
    actionCol.editable = YES;
    [_tableView addTableColumn:actionCol];

    scrollView.documentView = _tableView;
    [self.view addSubview:scrollView];

    // --- Add/Remove buttons ---
    NSButton* addBtn = [NSButton buttonWithTitle:@"+" target:self action:@selector(addKeybinding:)];
    addBtn.translatesAutoresizingMaskIntoConstraints = NO;
    addBtn.bezelStyle = NSBezelStyleSmallSquare;
    [self.view addSubview:addBtn];

    NSButton* removeBtn = [NSButton buttonWithTitle:@"\u2212" target:self action:@selector(removeKeybinding:)];
    removeBtn.translatesAutoresizingMaskIntoConstraints = NO;
    removeBtn.bezelStyle = NSBezelStyleSmallSquare;
    [self.view addSubview:removeBtn];

    // --- Layout ---
    [NSLayoutConstraint activateConstraints:@[
        [scrollView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:margin],
        [scrollView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-margin],
        [scrollView.topAnchor constraintEqualToAnchor:self.view.topAnchor constant:margin],
        [scrollView.bottomAnchor constraintEqualToAnchor:addBtn.topAnchor constant:-8],

        [addBtn.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:margin],
        [addBtn.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor constant:-margin],
        [addBtn.widthAnchor constraintEqualToConstant:28],
        [addBtn.heightAnchor constraintEqualToConstant:24],

        [removeBtn.leadingAnchor constraintEqualToAnchor:addBtn.trailingAnchor constant:2],
        [removeBtn.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor constant:-margin],
        [removeBtn.widthAnchor constraintEqualToConstant:28],
        [removeBtn.heightAnchor constraintEqualToConstant:24],
    ]];
}

#pragma mark - Actions

- (void)addKeybinding:(id)sender {
    (void)sender;
    termcore::KeyBinding kb;
    kb.trigger = "";
    kb.action = "";
    _config.keybindings.push_back(std::move(kb));

    [_tableView reloadData];
    NSInteger newRow = static_cast<NSInteger>(_config.keybindings.size()) - 1;
    [_tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:newRow] byExtendingSelection:NO];
    [_tableView editColumn:0 row:newRow withEvent:nil select:YES];
}

- (void)removeKeybinding:(id)sender {
    (void)sender;
    NSInteger row = _tableView.selectedRow;
    if (row < 0 || static_cast<size_t>(row) >= _config.keybindings.size()) return;

    _config.keybindings.erase(_config.keybindings.begin() + row);
    [_tableView reloadData];

    if (self.saveBlock) {
        self.saveBlock(_config);
    }
}

#pragma mark - NSTableViewDataSource

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView {
    return static_cast<NSInteger>(_config.keybindings.size());
}

#pragma mark - NSTableViewDelegate

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row {
    NSString* identifier = tableColumn.identifier;
    NSTextField* cell = [tableView makeViewWithIdentifier:identifier owner:self];
    if (!cell) {
        cell = [[NSTextField alloc] initWithFrame:NSZeroRect];
        cell.identifier = identifier;
        cell.bordered = NO;
        cell.drawsBackground = NO;
        cell.editable = YES;
        cell.delegate = self;
        cell.lineBreakMode = NSLineBreakByTruncatingTail;
    }

    const auto& kb = _config.keybindings[static_cast<size_t>(row)];
    if ([identifier isEqualToString:kTriggerColumnID]) {
        cell.stringValue = [NSString stringWithUTF8String:kb.trigger.c_str()];
    } else {
        cell.stringValue = [NSString stringWithUTF8String:kb.action.c_str()];
    }

    return cell;
}

#pragma mark - NSTextFieldDelegate (cell editing)

- (void)controlTextDidEndEditing:(NSNotification*)notification {
    NSTextField* field = notification.object;
    NSInteger row = [_tableView rowForView:field];
    NSInteger col = [_tableView columnForView:field];

    if (row < 0 || static_cast<size_t>(row) >= _config.keybindings.size()) return;

    std::string value = field.stringValue.UTF8String ?: "";
    if (col == 0) {
        _config.keybindings[static_cast<size_t>(row)].trigger = value;
    } else {
        _config.keybindings[static_cast<size_t>(row)].action = value;
    }

    if (self.saveBlock) {
        self.saveBlock(_config);
    }
}

- (NSSize)preferredContentSize {
    return NSMakeSize(520, 400);
}

@end
