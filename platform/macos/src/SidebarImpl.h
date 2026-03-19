#ifndef BREADTERMINAL_SIDEBAR_IMPL_H
#define BREADTERMINAL_SIDEBAR_IMPL_H

#import <Foundation/Foundation.h>
#include "termcore/workspace_status.h"
#include "termcore/git_branch_detector.h"
#include <memory>
#include <vector>

struct SidebarImpl {
    std::unique_ptr<termcore::GitBranchDetector> gitDetector;
    std::vector<termcore::WorkspaceStatusSnapshot> snapshots;
};

#endif // BREADTERMINAL_SIDEBAR_IMPL_H
