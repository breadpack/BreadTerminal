#ifndef BREADTERMINAL_PREFS_KEYBINDINGS_VIEW_CONTROLLER_H
#define BREADTERMINAL_PREFS_KEYBINDINGS_VIEW_CONTROLLER_H

#import <Cocoa/Cocoa.h>
#include "termcore/config.h"
#include "PreferencesWindowController.h"

@interface PrefsKeybindingsViewController : NSViewController
@property (nonatomic, assign) termcore::Config config;
@property (nonatomic, copy) PrefsSaveBlock saveBlock;
@end

#endif
