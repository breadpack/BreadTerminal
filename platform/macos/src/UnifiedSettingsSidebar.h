#ifndef BREADTERMINAL_UNIFIED_SETTINGS_SIDEBAR_H
#define BREADTERMINAL_UNIFIED_SETTINGS_SIDEBAR_H

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#include <string>

@class UnifiedSettingsWindowController;

/// Sidebar view for the unified settings window.
/// Displays category tree with top-level headers and selectable subcategories.
@interface UnifiedSettingsSidebar : NSView <NSTableViewDataSource, NSTableViewDelegate>

- (instancetype)initWithController:(UnifiedSettingsWindowController*)controller;
- (void)reloadData;
- (void)selectCategory:(const std::string&)categoryId;
- (void)filterWithSearchQuery:(NSString*)query;

@end

#endif // __APPLE__
#endif
