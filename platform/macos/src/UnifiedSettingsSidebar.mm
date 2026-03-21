#if defined(__APPLE__)

#import "UnifiedSettingsSidebar.h"
#import "UnifiedSettingsWindowController.h"

#include "termcore/settings_model.h"

#include <string>
#include <vector>

static const CGFloat kCategoryRowHeight    = 28.0;
static const CGFloat kSubCategoryRowHeight = 26.0;
static const CGFloat kSidebarPadding       = 8.0;

/// Represents a row in the sidebar table: either a top-level header or a subcategory.
struct SidebarRow {
    std::string categoryId;
    std::string label;
    bool isHeader = false;        // top-level = bold, non-selectable
    bool isExpanded = true;       // headers can expand/collapse
    std::string parentId;         // non-empty for subcategories
};

@implementation UnifiedSettingsSidebar {
    __weak UnifiedSettingsWindowController* _controller;
    NSScrollView* _scrollView;
    NSTableView* _tableView;

    std::vector<SidebarRow> _allRows;      // full unfiltered list
    std::vector<SidebarRow> _visibleRows;  // after filtering + collapse
    std::string _selectedCategoryId;
    NSString* _searchQuery;
}

- (instancetype)initWithController:(UnifiedSettingsWindowController*)controller {
    self = [super initWithFrame:NSZeroRect];
    if (self) {
        _controller = controller;
        _searchQuery = @"";
        _selectedCategoryId = "general.shell";
        [self setupTableView];
        [self reloadData];
    }
    return self;
}

- (void)setupTableView {
    _scrollView = [[NSScrollView alloc] initWithFrame:self.bounds];
    _scrollView.hasVerticalScroller = YES;
    _scrollView.hasHorizontalScroller = NO;
    _scrollView.drawsBackground = NO;
    _scrollView.autohidesScrollers = YES;
    _scrollView.translatesAutoresizingMaskIntoConstraints = NO;
    [self addSubview:_scrollView];

    _tableView = [[NSTableView alloc] initWithFrame:NSZeroRect];
    _tableView.headerView = nil;  // no column header
    _tableView.backgroundColor = [NSColor clearColor];
    _tableView.selectionHighlightStyle = NSTableViewSelectionHighlightStyleRegular;
    _tableView.rowSizeStyle = NSTableViewRowSizeStyleCustom;
    _tableView.intercellSpacing = NSMakeSize(0, 0);
    _tableView.dataSource = self;
    _tableView.delegate = self;

    // Single text column
    NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"CategoryColumn"];
    column.resizingMask = NSTableColumnAutoresizingMask;
    [_tableView addTableColumn:column];

    _scrollView.documentView = _tableView;

    [NSLayoutConstraint activateConstraints:@[
        [_scrollView.topAnchor constraintEqualToAnchor:self.topAnchor],
        [_scrollView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
        [_scrollView.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
        [_scrollView.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],
    ]];
}

#pragma mark - Data

- (void)reloadData {
    [self buildRowList];
    [self rebuildVisibleRows];
    [_tableView reloadData];
    [self restoreSelection];
}

- (void)buildRowList {
    _allRows.clear();

    UnifiedSettingsWindowController* ctrl = _controller;
    if (!ctrl || !ctrl.settingsModel) return;

    auto topLevel = ctrl.settingsModel->topLevelCategories();
    for (const auto* topCat : topLevel) {
        // Add header row
        SidebarRow header;
        header.categoryId = topCat->id;
        header.label = topCat->label;
        header.isHeader = true;
        header.isExpanded = true;
        _allRows.push_back(header);

        // Add subcategory rows
        auto subs = ctrl.settingsModel->subcategories(topCat->id);
        for (const auto* sub : subs) {
            SidebarRow row;
            row.categoryId = sub->id;
            row.label = sub->label;
            row.isHeader = false;
            row.parentId = topCat->id;
            _allRows.push_back(row);
        }
    }
}

- (void)rebuildVisibleRows {
    _visibleRows.clear();

    // Track which headers are collapsed
    std::string collapsedParent;
    bool skipping = false;

    for (const auto& row : _allRows) {
        if (row.isHeader) {
            skipping = !row.isExpanded;
            collapsedParent = row.categoryId;

            // When searching, only show headers with matching children
            if (_searchQuery.length > 0) {
                bool hasMatch = [self headerHasMatchingChildren:row.categoryId];
                if (!hasMatch) {
                    skipping = true;
                    continue;
                }
            }
            _visibleRows.push_back(row);
        } else {
            if (skipping && row.parentId == collapsedParent) continue;

            // When searching, filter subcategories
            if (_searchQuery.length > 0) {
                std::string query([_searchQuery UTF8String]);
                std::string label = row.label;
                std::string catId = row.categoryId;

                // Case-insensitive match
                std::string lowerQuery = query;
                std::string lowerLabel = label;
                std::string lowerCatId = catId;
                for (auto& c : lowerQuery) c = std::tolower(c);
                for (auto& c : lowerLabel) c = std::tolower(c);
                for (auto& c : lowerCatId) c = std::tolower(c);

                bool matches = (lowerLabel.find(lowerQuery) != std::string::npos) ||
                               (lowerCatId.find(lowerQuery) != std::string::npos);

                // Also check if any items in this category match
                UnifiedSettingsWindowController* ctrl = _controller;
                if (!matches && ctrl && ctrl.settingsModel) {
                    auto searchResults = ctrl.settingsModel->search(query);
                    for (const auto& match : searchResults) {
                        if (match.categoryId == catId) {
                            matches = true;
                            break;
                        }
                    }
                }

                if (!matches) continue;
            }

            _visibleRows.push_back(row);
        }
    }
}

- (BOOL)headerHasMatchingChildren:(const std::string&)parentId {
    std::string query([_searchQuery UTF8String]);
    std::string lowerQuery = query;
    for (auto& c : lowerQuery) c = std::tolower(c);

    for (const auto& row : _allRows) {
        if (row.parentId != parentId) continue;

        std::string lowerLabel = row.label;
        for (auto& c : lowerLabel) c = std::tolower(c);
        if (lowerLabel.find(lowerQuery) != std::string::npos) return YES;

        // Check items via SettingsModel search
        UnifiedSettingsWindowController* ctrl = _controller;
        if (ctrl && ctrl.settingsModel) {
            auto results = ctrl.settingsModel->search(query);
            for (const auto& match : results) {
                if (match.categoryId == row.categoryId) return YES;
            }
        }
    }
    return NO;
}

#pragma mark - Selection

- (void)selectCategory:(const std::string&)categoryId {
    _selectedCategoryId = categoryId;
    [self restoreSelection];
}

- (void)restoreSelection {
    for (NSInteger i = 0; i < (NSInteger)_visibleRows.size(); i++) {
        if (_visibleRows[i].categoryId == _selectedCategoryId && !_visibleRows[i].isHeader) {
            [_tableView selectRowIndexes:[NSIndexSet indexSetWithIndex:i] byExtendingSelection:NO];
            [_tableView scrollRowToVisible:i];
            return;
        }
    }
}

#pragma mark - Search Filtering

- (void)filterWithSearchQuery:(NSString*)query {
    _searchQuery = query ? query : @"";
    [self rebuildVisibleRows];
    [_tableView reloadData];
    [self restoreSelection];
}

#pragma mark - NSTableViewDataSource

- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView {
    (void)tableView;
    return (NSInteger)_visibleRows.size();
}

#pragma mark - NSTableViewDelegate

- (CGFloat)tableView:(NSTableView*)tableView heightOfRow:(NSInteger)row {
    (void)tableView;
    if (row < 0 || row >= (NSInteger)_visibleRows.size()) return kSubCategoryRowHeight;
    return _visibleRows[row].isHeader ? kCategoryRowHeight : kSubCategoryRowHeight;
}

- (NSView*)tableView:(NSTableView*)tableView viewForTableColumn:(NSTableColumn*)tableColumn row:(NSInteger)row {
    (void)tableColumn;

    if (row < 0 || row >= (NSInteger)_visibleRows.size()) return nil;
    const auto& sidebarRow = _visibleRows[row];

    NSString* identifier = sidebarRow.isHeader ? @"HeaderCell" : @"CategoryCell";
    NSTableCellView* cell = [tableView makeViewWithIdentifier:identifier owner:self];

    if (!cell) {
        cell = [[NSTableCellView alloc] initWithFrame:NSZeroRect];
        cell.identifier = identifier;

        NSTextField* textField = [NSTextField labelWithString:@""];
        textField.translatesAutoresizingMaskIntoConstraints = NO;
        textField.lineBreakMode = NSLineBreakByTruncatingTail;
        [cell addSubview:textField];
        cell.textField = textField;

        CGFloat leading = sidebarRow.isHeader ? kSidebarPadding : (kSidebarPadding + 16.0);
        [NSLayoutConstraint activateConstraints:@[
            [textField.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:leading],
            [textField.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor constant:-kSidebarPadding],
            [textField.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
        ]];
    }

    NSString* label = [NSString stringWithUTF8String:sidebarRow.label.c_str()];
    cell.textField.stringValue = label;

    if (sidebarRow.isHeader) {
        cell.textField.font = [NSFont boldSystemFontOfSize:12];
        cell.textField.textColor = [NSColor secondaryLabelColor];
    } else {
        cell.textField.font = [NSFont systemFontOfSize:13];
        cell.textField.textColor = [NSColor labelColor];
    }

    return cell;
}

- (BOOL)tableView:(NSTableView*)tableView shouldSelectRow:(NSInteger)row {
    (void)tableView;
    if (row < 0 || row >= (NSInteger)_visibleRows.size()) return NO;

    const auto& sidebarRow = _visibleRows[row];
    if (sidebarRow.isHeader) {
        // Toggle expand/collapse on header click
        [self toggleHeaderAtRow:row];
        return NO;
    }
    return YES;
}

- (void)tableViewSelectionDidChange:(NSNotification*)notification {
    (void)notification;
    NSInteger row = _tableView.selectedRow;
    if (row < 0 || row >= (NSInteger)_visibleRows.size()) return;

    const auto& sidebarRow = _visibleRows[row];
    if (sidebarRow.isHeader) return;

    _selectedCategoryId = sidebarRow.categoryId;

    UnifiedSettingsWindowController* ctrl = _controller;
    if (ctrl) {
        [ctrl didSelectCategory:sidebarRow.categoryId];
    }
}

- (void)tableView:(NSTableView*)tableView didAddRowView:(NSTableRowView*)rowView forRow:(NSInteger)row {
    (void)tableView;
    (void)rowView;
    (void)row;
    // Custom selection color could be applied here if desired
}

#pragma mark - Expand/Collapse

- (void)toggleHeaderAtRow:(NSInteger)row {
    if (row < 0 || row >= (NSInteger)_visibleRows.size()) return;
    auto& headerRow = _visibleRows[row];
    if (!headerRow.isHeader) return;

    // Find corresponding row in _allRows and toggle
    for (auto& r : _allRows) {
        if (r.categoryId == headerRow.categoryId && r.isHeader) {
            r.isExpanded = !r.isExpanded;
            break;
        }
    }

    [self rebuildVisibleRows];
    [_tableView reloadData];
    [self restoreSelection];
}

@end

#endif // __APPLE__
