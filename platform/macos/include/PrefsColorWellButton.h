#ifndef BREADTERMINAL_PREFS_COLOR_WELL_BUTTON_H
#define BREADTERMINAL_PREFS_COLOR_WELL_BUTTON_H

#import <Cocoa/Cocoa.h>

/// Reusable NSColorWell subclass for preferences panels.
/// Converts between uint32_t hex (0xRRGGBB) and NSColor via sRGB.
@interface PrefsColorWellButton : NSColorWell

/// The color as a 24-bit hex value (0xRRGGBB).
@property (nonatomic, assign) uint32_t hexColor;

/// Called whenever the user picks a new color.
@property (nonatomic, copy) void (^onColorChanged)(uint32_t newHex);

@end

#endif // BREADTERMINAL_PREFS_COLOR_WELL_BUTTON_H
