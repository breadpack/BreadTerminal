#import "TerminalView.h"

#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include "termcore/pty.h"
#include "termcore/keybinding.h"
#include "termcore/search.h"
#include "termcore/url_detector.h"
#include "termcore/font/font_collection.h"
#include "termcore/font/font_shaper.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/glyph_cache.h"
#include "MetalTextRenderer.h"
#include "CoreTextRasterizer.h"
#include "CoreTextDiscovery.h"
#include "TerminalViewImpl.h"

#include <dispatch/dispatch.h>
#include <memory>

@implementation TerminalView

#pragma mark - Initialisation

- (instancetype)initWithFrame:(NSRect)frame device:(id<MTLDevice>)device {
    self = [super initWithFrame:frame];
    if (!self) return nil;

    _device = device;
    _impl = new TerminalViewImpl();

    // Metal layer
    self.wantsLayer = YES;
    _metalLayer = [CAMetalLayer layer];
    _metalLayer.device = _device;
    _metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    _metalLayer.framebufferOnly = YES;
    _metalLayer.frame = self.bounds;
    _metalLayer.contentsScale = self.window.backingScaleFactor ?: 2.0;
    self.layer = _metalLayer;

    // Font stack
    _impl->rasterizer = termcore::createCoreTextRasterizer();
    _impl->discovery  = termcore::createCoreTextDiscovery();
    _impl->shaper     = std::make_unique<termcore::FontShaper>();
    _impl->fontCollection = std::make_unique<termcore::FontCollection>(
        *_impl->rasterizer, *_impl->discovery, *_impl->shaper);
    bool fontOk = _impl->fontCollection->setPrimaryFont("Menlo", 14.0f);
    NSLog(@"BreadTerminal: setPrimaryFont Menlo = %d", fontOk);
    auto fm = _impl->fontCollection->primaryMetrics();
    NSLog(@"BreadTerminal: metrics cellW=%.1f cellH=%.1f ascent=%.1f", fm.cell_width, fm.cell_height, fm.ascent);
    _impl->atlas = std::make_unique<termcore::GlyphAtlas>();
    _impl->cache = std::make_unique<termcore::GlyphCache>();

    // Renderer
    _impl->renderer = std::make_unique<termcore::MetalTextRenderer>(_device, _metalLayer);
    _impl->renderer->setFontStack(
        _impl->fontCollection.get(), _impl->cache.get(),
        _impl->atlas.get(), _impl->rasterizer.get());

    // Cell dimensions from font metrics
    auto metrics = _impl->fontCollection->primaryMetrics();
    _cellWidth  = metrics.cell_width  > 0 ? metrics.cell_width  : 8.0f;
    _cellHeight = metrics.cell_height > 0 ? metrics.cell_height : 16.0f;

    // Screen + Parser
    auto [rows, cols] = [self calculateGridSize];
    _impl->screen = std::make_unique<termcore::Screen>(rows, cols);
    _impl->parser = std::make_unique<termcore::VtParser>(*_impl->screen);
    _termRows = rows;
    _termCols = cols;
    _scrollOffset = 0;

    // Keybinding manager (defaults are loaded in constructor)
    _impl->keybindings = std::make_unique<termcore::KeybindingManager>();

    // Search engine
    _impl->search = std::make_unique<termcore::TerminalSearch>();

    // URL detector
    _impl->urlDetector = std::make_unique<termcore::UrlDetector>();
    _searchActive = NO;

    // Set initial drawable size (pixels) and viewport (points)
    CGFloat scale = 2.0;  // Retina default
    _metalLayer.drawableSize = NSMakeSize(frame.size.width * scale, frame.size.height * scale);
    _impl->renderer->resize(frame.size.width, frame.size.height);
    _impl->needsRender = true;

    // Render timer (60 fps)
    __weak TerminalView* weakSelf = self;
    _impl->renderTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60.0
                                                         repeats:YES
                                                           block:^(NSTimer* _Nonnull timer) {
        TerminalView* s = weakSelf;
        if (!s) { [timer invalidate]; return; }
        [s renderFrame];
    }];
    return self;
}

- (void)dealloc {
    if (_impl->ptyReadSource) dispatch_source_cancel(_impl->ptyReadSource);
    [_impl->renderTimer invalidate];
    delete _impl;
}

- (void)applyConfig:(const termcore::Config&)config {
    // Font
    if (!config.font_family.empty()) {
        _impl->fontCollection->setPrimaryFont(config.font_family, config.font_size);
        auto metrics = _impl->fontCollection->primaryMetrics();
        _cellWidth  = metrics.cell_width  > 0 ? metrics.cell_width  : 8.0f;
        _cellHeight = metrics.cell_height > 0 ? metrics.cell_height : 16.0f;
    }

    // Keybindings from config
    if (!config.keybindings.empty()) {
        std::vector<std::pair<std::string, std::string>> bindings;
        for (const auto& kb : config.keybindings) {
            bindings.emplace_back(kb.trigger, kb.action);
        }
        _impl->keybindings->loadFromConfig(bindings);
    }

    // Recalculate grid with potentially new font metrics
    [self updateGridSize];
}

#pragma mark - NSView overrides

- (BOOL)wantsLayer { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)becomeFirstResponder  { return YES; }
- (BOOL)wantsUpdateLayer { return YES; }

- (CALayer*)makeBackingLayer {
    if (!_metalLayer) {
        _metalLayer = [CAMetalLayer layer];
        _metalLayer.device = _device;
        _metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    }
    return _metalLayer;
}

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    if (self.window) _metalLayer.contentsScale = self.window.backingScaleFactor;
}

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    _metalLayer.frame = self.bounds;
    _metalLayer.drawableSize = NSMakeSize(
        newSize.width * _metalLayer.contentsScale,
        newSize.height * _metalLayer.contentsScale);
    [self updateGridSize];
}

- (void)viewDidEndLiveResize {
    [super viewDidEndLiveResize];
    [self updateGridSize];
}

#pragma mark - Grid helpers

- (std::pair<int,int>)calculateGridSize {
    float cw = _cellWidth  > 0 ? _cellWidth  : 8;
    float ch = _cellHeight > 0 ? _cellHeight : 16;
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
    if (_impl->pty && _impl->pty->isAlive()) _impl->pty->resize(rows, cols);
    // Use points (not pixels) for viewport — cell positions are in points
    float w = self.bounds.size.width;
    float h = self.bounds.size.height;
    _impl->renderer->resize(w, h);
    _impl->needsRender = true;
}

#pragma mark - Shell / PTY

- (void)startShell {
    _impl->pty = termcore::createPty();
    if (!_impl->pty->spawn("", {}, "", _termRows, _termCols)) {
        NSLog(@"BreadTerminal: failed to spawn shell");
        return;
    }
    int fd = _impl->pty->fd();
    dispatch_source_t src = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_READ, fd, 0, dispatch_get_main_queue());
    __weak TerminalView* weakSelf = self;
    dispatch_source_set_event_handler(src, ^{
        TerminalView* s = weakSelf;
        if (s) [s readPtyData];
    });
    dispatch_source_set_cancel_handler(src, ^{});
    _impl->ptyReadSource = src;
    dispatch_resume(src);
}

- (void)readPtyData {
    char buf[8192];
    int n = _impl->pty->read(buf, sizeof(buf));
    if (n > 0) {
        _impl->parser->feed(buf, static_cast<size_t>(n));
        _impl->needsRender = true;
        // Update window title from OSC sequences
        auto title = _impl->screen->title();
        if (!title.empty()) {
            NSString* t = [NSString stringWithUTF8String:title.c_str()];
            if (t && ![self.window.title isEqualToString:t])
                self.window.title = t;
        }
    } else if (n < 0) {
        if (_impl->ptyReadSource) {
            dispatch_source_cancel(_impl->ptyReadSource);
            _impl->ptyReadSource = nullptr;
        }
    }
}

#pragma mark - Text I/O

- (void)sendText:(NSString*)text {
    if (!_impl->pty) return;
    const char* utf8 = [text UTF8String];
    _impl->pty->write(utf8, [text lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
}

- (void)writePty:(const char*)str {
    if (_impl->pty) _impl->pty->write(str, strlen(str));
}

#pragma mark - Rendering

- (void)setNeedsRender { _impl->needsRender = true; }

- (void)renderFrame {
    if (!_impl->needsRender) return;
    _impl->needsRender = false;

    // Ensure drawable size is valid
    if (_metalLayer.drawableSize.width <= 0 || _metalLayer.drawableSize.height <= 0) {
        CGFloat scale = self.window.backingScaleFactor ?: 2.0;
        NSSize sz = self.bounds.size;
        if (sz.width > 0 && sz.height > 0) {
            _metalLayer.drawableSize = NSMakeSize(sz.width * scale, sz.height * scale);
            _impl->renderer->resize(sz.width * scale, sz.height * scale);
        }
    }

    _impl->renderer->render(*_impl->screen);
}

@end
