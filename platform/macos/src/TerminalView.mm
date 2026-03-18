#import "TerminalView.h"

#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include "termcore/pty.h"
#include "termcore/font/font_collection.h"
#include "termcore/font/font_shaper.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/glyph_cache.h"
#include "MetalTextRenderer.h"
#include "CoreTextRasterizer.h"
#include "CoreTextDiscovery.h"

#include <dispatch/dispatch.h>
#include <memory>

// ---------------------------------------------------------------------------
// Private extension — holds C++ members via a raw pointer to an Impl struct.
// ---------------------------------------------------------------------------
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

    dispatch_source_t ptyReadSource = nullptr;
    NSTimer* renderTimer = nil;

    bool needsRender = false;
};

// ---------------------------------------------------------------------------
@interface TerminalView () {
    TerminalViewImpl* _impl;
    id<MTLDevice> _device;
    CAMetalLayer* _metalLayer;
}
@end

@implementation TerminalView

// ---------------------------------------------------------------------------
#pragma mark - Initialisation
// ---------------------------------------------------------------------------

- (instancetype)initWithFrame:(NSRect)frame device:(id<MTLDevice>)device {
    self = [super initWithFrame:frame];
    if (!self) return nil;

    _device = device;
    _impl = new TerminalViewImpl();

    // --- Metal layer ---
    self.wantsLayer = YES;
    _metalLayer = [CAMetalLayer layer];
    _metalLayer.device = _device;
    _metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    _metalLayer.framebufferOnly = YES;
    _metalLayer.frame = self.bounds;
    _metalLayer.contentsScale = self.window.backingScaleFactor ?: 2.0;
    self.layer = _metalLayer;

    // --- Font stack ---
    _impl->rasterizer = termcore::createCoreTextRasterizer();
    _impl->discovery  = termcore::createCoreTextDiscovery();
    _impl->shaper     = std::make_unique<termcore::FontShaper>();
    _impl->fontCollection = std::make_unique<termcore::FontCollection>(
        *_impl->rasterizer, *_impl->discovery, *_impl->shaper);
    _impl->fontCollection->setPrimaryFont("Menlo", 14.0f);

    _impl->atlas = std::make_unique<termcore::GlyphAtlas>();
    _impl->cache = std::make_unique<termcore::GlyphCache>();

    // --- Renderer ---
    _impl->renderer = std::make_unique<termcore::MetalTextRenderer>(_device, _metalLayer);
    _impl->renderer->setFontStack(
        _impl->fontCollection.get(),
        _impl->cache.get(),
        _impl->atlas.get(),
        _impl->rasterizer.get());

    // --- Screen + Parser ---
    auto [rows, cols] = [self calculateGridSize];
    _impl->screen = std::make_unique<termcore::Screen>(rows, cols);
    _impl->parser = std::make_unique<termcore::VtParser>(*_impl->screen);

    _termRows = rows;
    _termCols = cols;

    // --- Render timer (60 fps) ---
    __weak TerminalView* weakSelf = self;
    _impl->renderTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60.0
                                                         repeats:YES
                                                           block:^(NSTimer* _Nonnull timer) {
        TerminalView* strongSelf = weakSelf;
        if (!strongSelf) { [timer invalidate]; return; }
        [strongSelf renderFrame];
    }];

    return self;
}

- (void)dealloc {
    if (_impl->ptyReadSource) {
        dispatch_source_cancel(_impl->ptyReadSource);
    }
    [_impl->renderTimer invalidate];
    delete _impl;
}

// ---------------------------------------------------------------------------
#pragma mark - NSView overrides
// ---------------------------------------------------------------------------

- (BOOL)wantsLayer { return YES; }

- (CALayer*)makeBackingLayer {
    if (!_metalLayer) {
        _metalLayer = [CAMetalLayer layer];
        _metalLayer.device = _device;
        _metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    }
    return _metalLayer;
}

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)becomeFirstResponder  { return YES; }

- (BOOL)wantsUpdateLayer { return YES; }

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    if (self.window) {
        _metalLayer.contentsScale = self.window.backingScaleFactor;
    }
}

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    _metalLayer.frame = self.bounds;
    _metalLayer.drawableSize = NSMakeSize(
        newSize.width  * _metalLayer.contentsScale,
        newSize.height * _metalLayer.contentsScale);
    [self updateGridSize];
}

- (void)viewDidEndLiveResize {
    [super viewDidEndLiveResize];
    [self updateGridSize];
}

// ---------------------------------------------------------------------------
#pragma mark - Grid helpers
// ---------------------------------------------------------------------------

- (std::pair<int,int>)calculateGridSize {
    auto metrics = _impl->fontCollection->primaryMetrics();
    float cw = metrics.cell_width;
    float ch = metrics.cell_height;
    if (cw <= 0) cw = 8;
    if (ch <= 0) ch = 16;

    int cols = std::max(1, (int)(self.bounds.size.width  / cw));
    int rows = std::max(1, (int)(self.bounds.size.height / ch));
    return {rows, cols};
}

- (void)updateGridSize {
    auto [rows, cols] = [self calculateGridSize];
    if (rows == _termRows && cols == _termCols) return;

    _termRows = rows;
    _termCols = cols;
    _impl->screen->resize(rows, cols);

    if (_impl->pty && _impl->pty->isAlive()) {
        _impl->pty->resize(rows, cols);
    }

    float w = self.bounds.size.width  * _metalLayer.contentsScale;
    float h = self.bounds.size.height * _metalLayer.contentsScale;
    _impl->renderer->resize(w, h);

    _impl->needsRender = true;
}

// ---------------------------------------------------------------------------
#pragma mark - Shell / PTY
// ---------------------------------------------------------------------------

- (void)startShell {
    _impl->pty = termcore::createPty();
    if (!_impl->pty->spawn("", {}, "", _termRows, _termCols)) {
        NSLog(@"BreadTerminal: failed to spawn shell");
        return;
    }

    // Async read from PTY fd via GCD.
    int fd = _impl->pty->fd();
    dispatch_source_t src = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_READ, fd, 0, dispatch_get_main_queue());

    __weak TerminalView* weakSelf = self;
    dispatch_source_set_event_handler(src, ^{
        TerminalView* strongSelf = weakSelf;
        if (!strongSelf) return;
        [strongSelf readPtyData];
    });

    dispatch_source_set_cancel_handler(src, ^{
        // Nothing to clean up — Pty owns the fd.
    });

    _impl->ptyReadSource = src;
    dispatch_resume(src);
}

- (void)readPtyData {
    char buf[8192];
    int n = _impl->pty->read(buf, sizeof(buf));
    if (n > 0) {
        _impl->parser->feed(buf, static_cast<size_t>(n));
        _impl->needsRender = true;
    } else if (n < 0) {
        // PTY closed — stop reading.
        if (_impl->ptyReadSource) {
            dispatch_source_cancel(_impl->ptyReadSource);
            _impl->ptyReadSource = nullptr;
        }
    }
}

// ---------------------------------------------------------------------------
#pragma mark - Text I/O
// ---------------------------------------------------------------------------

- (void)sendText:(NSString*)text {
    if (!_impl->pty) return;
    const char* utf8 = [text UTF8String];
    size_t len = [text lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
    _impl->pty->write(utf8, len);
}

// ---------------------------------------------------------------------------
#pragma mark - Rendering
// ---------------------------------------------------------------------------

- (void)setNeedsRender {
    _impl->needsRender = true;
}

- (void)renderFrame {
    if (!_impl->needsRender) return;
    _impl->needsRender = false;

    _impl->renderer->render(*_impl->screen);
}

// ---------------------------------------------------------------------------
#pragma mark - Keyboard input
// ---------------------------------------------------------------------------

- (void)keyDown:(NSEvent*)event {
    // Handle special keys first.
    if ([self handleSpecialKey:event]) return;

    // Regular characters.
    NSString* chars = event.characters;
    if (chars.length > 0) {
        [self sendText:chars];
    }
}

- (BOOL)handleSpecialKey:(NSEvent*)event {
    unsigned short keyCode = event.keyCode;
    NSEventModifierFlags mods = event.modifierFlags;
    (void)mods;

    // Arrow keys
    switch (keyCode) {
        case 126: [self writePty:"\x1b[A"]; return YES; // Up
        case 125: [self writePty:"\x1b[B"]; return YES; // Down
        case 124: [self writePty:"\x1b[C"]; return YES; // Right
        case 123: [self writePty:"\x1b[D"]; return YES; // Left
        default: break;
    }

    // Enter / Return
    if (keyCode == 36 || keyCode == 76) {
        [self writePty:"\r"];
        return YES;
    }

    // Backspace / Delete
    if (keyCode == 51) {
        [self writePty:"\x7f"];
        return YES;
    }

    // Tab
    if (keyCode == 48) {
        [self writePty:"\t"];
        return YES;
    }

    // Escape
    if (keyCode == 53) {
        [self writePty:"\x1b"];
        return YES;
    }

    // Function keys F1..F4
    if (keyCode == 122) { [self writePty:"\x1bOP"];  return YES; } // F1
    if (keyCode == 120) { [self writePty:"\x1bOQ"];  return YES; } // F2
    if (keyCode == 99)  { [self writePty:"\x1bOR"];  return YES; } // F3
    if (keyCode == 118) { [self writePty:"\x1bOS"];  return YES; } // F4

    // Home / End / PageUp / PageDown
    if (keyCode == 115) { [self writePty:"\x1b[H"]; return YES; } // Home
    if (keyCode == 119) { [self writePty:"\x1b[F"]; return YES; } // End
    if (keyCode == 116) { [self writePty:"\x1b[5~"]; return YES; } // PageUp
    if (keyCode == 121) { [self writePty:"\x1b[6~"]; return YES; } // PageDown

    // Delete forward
    if (keyCode == 117) { [self writePty:"\x1b[3~"]; return YES; }

    return NO;
}

- (void)writePty:(const char*)str {
    if (!_impl->pty) return;
    _impl->pty->write(str, strlen(str));
}

// Prevent the system "bonk" sound for handled keys.
- (BOOL)performKeyEquivalent:(NSEvent*)event {
    // Let Cmd+Q etc. through.
    if (event.modifierFlags & NSEventModifierFlagCommand) {
        return [super performKeyEquivalent:event];
    }
    return NO;
}

@end
