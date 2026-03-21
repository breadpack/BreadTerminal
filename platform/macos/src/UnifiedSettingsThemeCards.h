#ifndef BREADTERMINAL_UNIFIED_SETTINGS_THEME_CARDS_H
#define BREADTERMINAL_UNIFIED_SETTINGS_THEME_CARDS_H

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>

@class UnifiedSettingsWindowController;

/// Theme card grid view for the unified settings window.
/// Shows a filter bar + grid of theme preview cards.
@interface UnifiedSettingsThemeCards : NSView

- (instancetype)initWithController:(UnifiedSettingsWindowController*)controller;
- (void)reloadCards;

@end

#endif // __APPLE__
#endif
