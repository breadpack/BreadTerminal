#ifndef BREADTERMINAL_THEME_CARD_VIEW_H
#define BREADTERMINAL_THEME_CARD_VIEW_H

#import <Cocoa/Cocoa.h>

@interface ThemeCardView : NSView
@property (nonatomic, copy) NSString* themeName;
@property (nonatomic, assign) uint32_t backgroundColor;
@property (nonatomic, assign) uint32_t foregroundColor;
@property (nonatomic, assign) BOOL installed;
@property (nonatomic, assign) BOOL isActive;
@property (nonatomic, copy) void (^onAction)(void); // install or apply
- (void)setPaletteColor:(uint32_t)color atIndex:(NSUInteger)index;
- (void)updateColors;
@end

#endif
