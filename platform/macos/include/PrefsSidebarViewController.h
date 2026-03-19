#ifndef BREADTERMINAL_PREFS_SIDEBAR_VIEW_CONTROLLER_H
#define BREADTERMINAL_PREFS_SIDEBAR_VIEW_CONTROLLER_H

#import <Cocoa/Cocoa.h>
#include "termcore/config.h"
#include "PreferencesWindowController.h"

@interface PrefsSidebarViewController : NSViewController
@property (nonatomic, assign) termcore::Config config;
@property (nonatomic, copy) PrefsSaveBlock saveBlock;
@end

#endif
