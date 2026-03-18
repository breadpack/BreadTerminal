#ifndef BREADTERMINAL_TERMINAL_VIEW_H
#define BREADTERMINAL_TERMINAL_VIEW_H

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

namespace termcore {
class Screen;
class MetalTextRenderer;
class GlyphAtlas;
class GlyphCache;
class FontCollection;
class IFontRasterizer;
class FontShaper;
class IFontDiscovery;
class VtParser;
class Pty;
} // namespace termcore

@interface TerminalView : NSView

- (instancetype)initWithFrame:(NSRect)frame device:(id<MTLDevice>)device;

/// Start a shell session in this terminal view.
- (void)startShell;

/// Send typed text to the PTY.
- (void)sendText:(NSString*)text;

/// Trigger a render update.
- (void)setNeedsRender;

/// Resize the terminal grid to match current view size.
- (void)updateGridSize;

/// Copy selection to clipboard.
- (void)copy:(id)sender;

/// Paste from clipboard with bracketed-paste support.
- (void)paste:(id)sender;

@property (nonatomic, readonly) int termRows;
@property (nonatomic, readonly) int termCols;

@end

#endif // BREADTERMINAL_TERMINAL_VIEW_H
