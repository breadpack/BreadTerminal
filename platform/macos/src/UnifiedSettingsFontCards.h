#ifndef BREADTERMINAL_UNIFIED_SETTINGS_FONT_CARDS_H
#define BREADTERMINAL_UNIFIED_SETTINGS_FONT_CARDS_H

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>

@class UnifiedSettingsWindowController;

/// Font card grid view for the unified settings window.
/// Shows a filter bar + grid of font preview cards.
@interface UnifiedSettingsFontCards : NSView

- (instancetype)initWithController:(UnifiedSettingsWindowController*)controller;
- (void)reloadCards;

@end

#endif // __APPLE__
#endif
