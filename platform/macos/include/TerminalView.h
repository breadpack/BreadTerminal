#ifndef BREADTERMINAL_TERMINAL_VIEW_H
#define BREADTERMINAL_TERMINAL_VIEW_H

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "termcore/config.h"
#include "termcore/config_diff.h"

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
class KeybindingManager;
class TerminalSearch;
class UrlDetector;
class PasteGuard;
struct PasteAnalysis;
enum class Action : uint16_t;
} // namespace termcore

@interface TerminalView : NSView <NSTextInputClient>

- (instancetype)initWithFrame:(NSRect)frame device:(id<MTLDevice>)device;

/// Apply a loaded config (font, keybindings, etc.)
- (void)applyConfig:(const termcore::Config&)config;

/// Start a shell session in this terminal view.
- (void)startShell;

/// Send typed text to the PTY.
- (void)sendText:(NSString*)text;

/// Trigger a render update.
- (void)setNeedsRender;

/// Resize the terminal grid to match current view size.
- (void)updateGridSize;

/// Apply incremental config changes based on dirty flags.
- (void)applyConfigDelta:(const termcore::Config&)config
                   dirty:(termcore::ConfigDirtyFlags)dirty;

/// Show a transient config error banner at the top of the view.
- (void)showConfigError:(NSString*)message;

/// Apply a named theme (resolves from built-in and user themes).
/// Updates screen dynamic colors and triggers re-render.
- (void)applyThemeByName:(const std::string&)themeName;

@property (nonatomic, readonly) int termRows;
@property (nonatomic, readonly) int termCols;

@end

/// Input handling category: keyboard, mouse, clipboard, search, URL detection.
@interface TerminalView (Input)

/// Copy selection to clipboard.
- (void)copy:(id)sender;

/// Paste from clipboard with bracketed-paste support.
- (void)paste:(id)sender;

/// Open the search bar.
- (void)openSearch;

/// Close the search bar.
- (void)closeSearch;

/// Handle a keybinding action.
- (void)handleAction:(termcore::Action)action;

/// Capture debug screenshot + state dump (Cmd+Shift+S)
- (void)captureScreenshot;

@end

/// Paste protection category: paste analysis, confirmation dialog, execution.
@interface TerminalView (Paste)

/// Execute the paste, writing text to PTY with optional bracketed-paste wrapping.
- (void)executePaste:(NSString*)text bracketed:(BOOL)bracketed;

/// Show a confirmation dialog for potentially dangerous paste content.
- (void)confirmPaste:(NSString*)text
            analysis:(const termcore::PasteAnalysis&)analysis
           bracketed:(BOOL)bracketed;

@end

#endif // BREADTERMINAL_TERMINAL_VIEW_H
