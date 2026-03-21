#ifndef BREADTERMINAL_UNIFIED_SETTINGS_CONTENT_H
#define BREADTERMINAL_UNIFIED_SETTINGS_CONTENT_H

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#include "termcore/settings_model.h"

@class UnifiedSettingsWindowController;

/// Content view for displaying standard settings items (toggles, text, number, etc.).
@interface UnifiedSettingsContent : NSView

- (instancetype)initWithController:(UnifiedSettingsWindowController*)controller
                          category:(const termcore::SettingsCategory*)category;

@end

#endif // __APPLE__
#endif
