#ifndef BREADTERMINAL_FONT_CARD_VIEW_H
#define BREADTERMINAL_FONT_CARD_VIEW_H

#import <Cocoa/Cocoa.h>

@interface FontCardView : NSView

@property (nonatomic, copy) NSString* fontName;
@property (nonatomic, copy) NSString* postscriptName;
@property (nonatomic, assign) BOOL hasLigatures;
@property (nonatomic, assign) BOOL hasNerdFontVariant;
@property (nonatomic, assign) BOOL installed;
@property (nonatomic, assign) BOOL isActive;
@property (nonatomic, copy) void (^onAction)(void);

/// Update visual state (call after setting properties).
- (void)updateState;

@end

#endif
