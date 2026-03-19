#ifndef BREADTERMINAL_WORKSPACE_ROW_VIEW_H
#define BREADTERMINAL_WORKSPACE_ROW_VIEW_H

#import <Cocoa/Cocoa.h>
#include "termcore/workspace_status.h"

@interface WorkspaceRowView : NSTableCellView

/// Update the row with a workspace snapshot and active state.
- (void)updateWithSnapshot:(const termcore::WorkspaceStatusSnapshot&)snapshot
                  isActive:(BOOL)active;

@end

#endif // BREADTERMINAL_WORKSPACE_ROW_VIEW_H
