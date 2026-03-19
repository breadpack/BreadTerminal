#import "SidebarViewController.h"
#import "WorkspaceRowView.h"
#include "termcore/mux.h"
#include <vector>

static NSString* const kWorkspaceRowIdentifier = @"WorkspaceRow";
static const CGFloat kRowHeight = 48.0;

@implementation SidebarViewController {
    NSScrollView* _scrollView;
    NSTableView* _tableView;
    std::vector<termcore::WorkspaceStatusSnapshot> _snapshots;
}

- (void)loadView {
    self.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 220, 400)];
}

- (void)viewDidLoad {
    [super viewDidLoad];

    // Background
    self.view.wantsLayer = YES;
    self.view.layer.backgroundColor =
        [NSColor colorWithCalibratedWhite:0.12 alpha:1.0].CGColor;

    // Table view
    _tableView = [[NSTableView alloc] initWithFrame:self.view.bounds];
    _tableView.headerView = nil;
    _tableView.rowHeight = kRowHeight;
    _tableView.intercellSpacing = NSMakeSize(0, 1);
    _tableView.backgroundColor = [NSColor clearColor];
    _tableView.selectionHighlightStyle = NSTableViewSelectionHighlightStyleNone;
    _tableView.dataSource = self;
    _tableView.delegate = self;

    NSTableColumn* column = [[NSTableColumn alloc]
        initWithIdentifier:@"MainColumn"];
    column.width = 220;
    [_tableView addTableColumn:column];

    // Scroll view
    _scrollView = [[NSScrollView alloc] initWithFrame:self.view.bounds];
    _scrollView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    _scrollView.documentView = _tableView;
    _scrollView.hasVerticalScroller = YES;
    _scrollView.drawsBackground = NO;

    [self.view addSubview:_scrollView];
}

#pragma mark - Public

- (void)updateSnapshots:(std::vector<termcore::WorkspaceStatusSnapshot>)snapshots {
    _snapshots = std::move(snapshots);
    [_tableView reloadData];
}

#pragma mark - NSTableViewDataSource

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView {
    (void)tableView;
    return static_cast<NSInteger>(_snapshots.size());
}

#pragma mark - NSTableViewDelegate

- (NSView*)tableView:(NSTableView*)tableView
   viewForTableColumn:(NSTableColumn*)tableColumn
                  row:(NSInteger)row {
    (void)tableColumn;
    WorkspaceRowView* rowView =
        [tableView makeViewWithIdentifier:kWorkspaceRowIdentifier
                                    owner:self];
    if (!rowView) {
        rowView = [[WorkspaceRowView alloc]
            initWithFrame:NSMakeRect(0, 0, tableView.bounds.size.width, kRowHeight)];
        rowView.identifier = kWorkspaceRowIdentifier;
    }

    if (row >= 0 && static_cast<size_t>(row) < _snapshots.size()) {
        const auto& snap = _snapshots[static_cast<size_t>(row)];
        [rowView updateWithSnapshot:snap isActive:snap.is_active];
    }

    return rowView;
}

- (CGFloat)tableView:(NSTableView*)tableView heightOfRow:(NSInteger)row {
    (void)tableView;
    (void)row;
    return kRowHeight;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification {
    (void)notification;
    NSInteger selectedRow = _tableView.selectedRow;
    if (selectedRow >= 0 && static_cast<size_t>(selectedRow) < _snapshots.size()) {
        uint32_t wsId = _snapshots[static_cast<size_t>(selectedRow)].id;
        if ([self.delegate respondsToSelector:@selector(sidebarDidSelectWorkspace:)]) {
            [self.delegate sidebarDidSelectWorkspace:wsId];
        }
    }
}

@end
