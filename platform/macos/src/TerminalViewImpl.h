#ifndef BREADTERMINAL_TERMINAL_VIEW_IMPL_H
#define BREADTERMINAL_TERMINAL_VIEW_IMPL_H

#import "TerminalView.h"

#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include "termcore/pty.h"
#include "termcore/keybinding.h"
#include "termcore/search.h"
#include "termcore/url_detector.h"
#include "termcore/mouse.h"
#include "termcore/font/font_collection.h"
#include "termcore/font/font_shaper.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/glyph_cache.h"
#include "MetalTextRenderer.h"

#include <dispatch/dispatch.h>
#include <memory>

/// Private C++ implementation details for TerminalView.
struct TerminalViewImpl {
    std::unique_ptr<termcore::Screen> screen;
    std::unique_ptr<termcore::VtParser> parser;
    std::unique_ptr<termcore::Pty> pty;
    std::unique_ptr<termcore::IFontRasterizer> rasterizer;
    std::unique_ptr<termcore::IFontDiscovery> discovery;
    std::unique_ptr<termcore::FontShaper> shaper;
    std::unique_ptr<termcore::FontCollection> fontCollection;
    std::unique_ptr<termcore::GlyphAtlas> atlas;
    std::unique_ptr<termcore::GlyphCache> cache;
    std::unique_ptr<termcore::MetalTextRenderer> renderer;
    std::unique_ptr<termcore::KeybindingManager> keybindings;
    std::unique_ptr<termcore::TerminalSearch> search;
    std::unique_ptr<termcore::UrlDetector> urlDetector;
    dispatch_source_t ptyReadSource = nullptr;
    NSTimer* renderTimer = nil;
    bool needsRender = false;
};

/// Expose ivars so that the category in TerminalViewInput.mm can access them.
@interface TerminalView () {
@public
    TerminalViewImpl* _impl;
    id<MTLDevice> _device;
    CAMetalLayer* _metalLayer;
    float _cellWidth;
    float _cellHeight;
    NSPoint _selectionStart;
    NSPoint _selectionEnd;
    BOOL _selecting;
    int _scrollOffset;
    BOOL _searchActive;
    NSTextField* _searchField;
    NSTrackingArea* _trackingArea;
    // Note: _termRows and _termCols are synthesized properties on TerminalView.
}

/// Internal helper: write raw bytes to the PTY.
- (void)writePty:(const char*)str;

@end

#endif // BREADTERMINAL_TERMINAL_VIEW_IMPL_H
