#ifndef BREADTERMINAL_SIDEBAR_VIEW_CONTROLLER_H
#define BREADTERMINAL_SIDEBAR_VIEW_CONTROLLER_H

#import <Cocoa/Cocoa.h>
#include "termcore/workspace_status.h"
#include <vector>

@protocol SidebarViewControllerDelegate <NSObject>
- (void)sidebarDidSelectWorkspace:(uint32_t)workspaceId;
@end

@interface SidebarViewController : NSViewController <NSTableViewDataSource, NSTableViewDelegate>

@property (nonatomic, weak) id<SidebarViewControllerDelegate> delegate;

/// Update the displayed workspace snapshots and reload the table.
- (void)updateSnapshots:(std::vector<termcore::WorkspaceStatusSnapshot>)snapshots;

@end

#endif // BREADTERMINAL_SIDEBAR_VIEW_CONTROLLER_H
