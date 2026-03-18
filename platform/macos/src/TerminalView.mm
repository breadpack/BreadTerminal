#import "TerminalView.h"

#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include "termcore/pty.h"
#include "termcore/mouse.h"
#include "termcore/font/font_collection.h"
#include "termcore/font/font_shaper.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/glyph_cache.h"
#include "MetalTextRenderer.h"
#include "CoreTextRasterizer.h"
#include "CoreTextDiscovery.h"

#include <dispatch/dispatch.h>
#include <memory>

// Private Impl — holds C++ members via a raw pointer.
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

@interface TerminalView () {
    TerminalViewImpl* _impl;
    id<MTLDevice> _device;
    CAMetalLayer* _metalLayer;
    float _cellWidth;
    float _cellHeight;
    NSPoint _selectionStart;
    NSPoint _selectionEnd;
    BOOL _selecting;
    int _scrollOffset;
}
@end

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
    _impl->fontCollection->setPrimaryFont("Menlo", 14.0f);
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
    float w = self.bounds.size.width  * _metalLayer.contentsScale;
    float h = self.bounds.size.height * _metalLayer.contentsScale;
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
    _impl->renderer->render(*_impl->screen);
}

#pragma mark - Keyboard input

- (void)keyDown:(NSEvent*)event {
    if ([self handleSpecialKey:event]) return;
    NSString* chars = event.characters;
    if (chars.length > 0) [self sendText:chars];
}

- (BOOL)handleSpecialKey:(NSEvent*)event {
    unsigned short kc = event.keyCode;
    // Arrow keys — SS3 prefix when application cursor keys mode is active
    bool appCur = _impl->screen && _impl->screen->appCursorKeys();
    const char* pfx = appCur ? "\x1bO" : "\x1b[";
    switch (kc) {
        case 126: { char s[3]={pfx[0],pfx[1],'A'}; _impl->pty->write(s,3); return YES; }
        case 125: { char s[3]={pfx[0],pfx[1],'B'}; _impl->pty->write(s,3); return YES; }
        case 124: { char s[3]={pfx[0],pfx[1],'C'}; _impl->pty->write(s,3); return YES; }
        case 123: { char s[3]={pfx[0],pfx[1],'D'}; _impl->pty->write(s,3); return YES; }
        default: break;
    }
    if (kc == 36 || kc == 76) { [self writePty:"\r"]; return YES; }
    if (kc == 51) { [self writePty:"\x7f"]; return YES; }
    if (kc == 48) { [self writePty:"\t"]; return YES; }
    if (kc == 53) { [self writePty:"\x1b"]; return YES; }
    if (kc == 122) { [self writePty:"\x1bOP"];  return YES; } // F1
    if (kc == 120) { [self writePty:"\x1bOQ"];  return YES; } // F2
    if (kc == 99)  { [self writePty:"\x1bOR"];  return YES; } // F3
    if (kc == 118) { [self writePty:"\x1bOS"];  return YES; } // F4
    if (kc == 115) { [self writePty:"\x1b[H"]; return YES; }  // Home
    if (kc == 119) { [self writePty:"\x1b[F"]; return YES; }  // End
    if (kc == 116) { [self writePty:"\x1b[5~"]; return YES; } // PageUp
    if (kc == 121) { [self writePty:"\x1b[6~"]; return YES; } // PageDown
    if (kc == 117) { [self writePty:"\x1b[3~"]; return YES; } // Delete fwd
    return NO;
}

- (BOOL)performKeyEquivalent:(NSEvent*)event {
    if (event.modifierFlags & NSEventModifierFlagCommand) {
        NSString* chars = event.charactersIgnoringModifiers;
        if ([chars isEqualToString:@"c"]) { [self copy:nil]; return YES; }
        if ([chars isEqualToString:@"v"]) { [self paste:nil]; return YES; }
        return [super performKeyEquivalent:event];
    }
    return NO;
}

#pragma mark - Clipboard

- (void)paste:(id)sender {
    NSString* text = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    if (!text || !_impl->pty) return;
    const char* utf8 = text.UTF8String;
    size_t len = strlen(utf8);
    bool bracketed = _impl->screen && _impl->screen->bracketedPaste();
    if (bracketed) _impl->pty->write("\033[200~", 6);
    _impl->pty->write(utf8, len);
    if (bracketed) _impl->pty->write("\033[201~", 6);
}

- (void)copy:(id)sender {
    if (!_impl->screen || !_selecting) return;
    int sr = (int)_selectionStart.y, er = (int)_selectionEnd.y;
    int sc = (int)_selectionStart.x, ec = (int)_selectionEnd.x;
    if (sr > er || (sr == er && sc > ec)) { std::swap(sr, er); std::swap(sc, ec); }
    NSMutableString* result = [NSMutableString string];
    for (int r = sr; r <= er; r++) {
        auto lt = _impl->screen->getLineText(r);
        NSString* line = [NSString stringWithUTF8String:lt.c_str()];
        if (!line) continue;
        if (sr == er) {
            NSRange rng = NSMakeRange(sc, std::max(0, ec - sc));
            if (rng.location + rng.length <= line.length)
                [result appendString:[line substringWithRange:rng]];
        } else if (r == sr) {
            if ((NSUInteger)sc < line.length)
                [result appendString:[line substringFromIndex:sc]];
        } else if (r == er) {
            NSUInteger idx = std::min((NSUInteger)ec, line.length);
            [result appendString:[line substringToIndex:idx]];
        } else {
            [result appendString:line];
        }
        if (r < er) [result appendString:@"\n"];
    }
    if (result.length > 0) {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        [pb setString:result forType:NSPasteboardTypeString];
    }
}

#pragma mark - Mouse events

- (NSPoint)cellPositionForEvent:(NSEvent*)event {
    NSPoint loc = [self convertPoint:event.locationInWindow fromView:nil];
    float flippedY = self.bounds.size.height - loc.y;
    int col = std::max(0, std::min((int)(loc.x / _cellWidth), _termCols - 1));
    int row = std::max(0, std::min((int)(flippedY / _cellHeight), _termRows - 1));
    return NSMakePoint(col, row);
}

- (int)modifierBitsForEvent:(NSEvent*)event {
    int m = 0;
    if (event.modifierFlags & NSEventModifierFlagShift)   m |= 1;
    if (event.modifierFlags & NSEventModifierFlagOption)  m |= 2;
    if (event.modifierFlags & NSEventModifierFlagControl) m |= 4;
    return m;
}

- (BOOL)sendMouseEvent:(int)type button:(int)button event:(NSEvent*)event {
    if (!_impl->screen || !_impl->pty) return NO;
    if (_impl->screen->mouseMode() == termcore::MouseMode::None) return NO;
    NSPoint cell = [self cellPositionForEvent:event];
    int mods = [self modifierBitsForEvent:event];
    termcore::MouseEvent me;
    me.type   = static_cast<termcore::MouseEventType>(type);
    me.button = static_cast<termcore::MouseButton>(button);
    me.col = (int)cell.x; me.row = (int)cell.y;
    me.shift = (mods & 1) != 0; me.alt = (mods & 2) != 0; me.ctrl = (mods & 4) != 0;
    auto seq = termcore::encodeMouseEvent(
        me, _impl->screen->mouseMode(), _impl->screen->mouseEncoding());
    if (seq.empty()) return NO;
    _impl->pty->write(seq.data(), seq.size());
    return YES;
}

- (void)mouseDown:(NSEvent*)event {
    if ([self sendMouseEvent:0 button:0 event:event]) return;
    _selectionStart = [self cellPositionForEvent:event];
    _selectionEnd = _selectionStart;
    _selecting = YES;
}

- (void)mouseUp:(NSEvent*)event {
    if ([self sendMouseEvent:1 button:3 event:event]) return;
    if (_selecting) _selectionEnd = [self cellPositionForEvent:event];
}

- (void)mouseDragged:(NSEvent*)event {
    if ([self sendMouseEvent:2 button:0 event:event]) return;
    if (_selecting) { _selectionEnd = [self cellPositionForEvent:event]; _impl->needsRender = true; }
}

- (void)rightMouseDown:(NSEvent*)event {
    if ([self sendMouseEvent:0 button:2 event:event]) return;
    [super rightMouseDown:event];
}

- (void)rightMouseUp:(NSEvent*)event {
    if ([self sendMouseEvent:1 button:3 event:event]) return;
    [super rightMouseUp:event];
}

- (void)scrollWheel:(NSEvent*)event {
    if (!_impl->screen || !_impl->pty) return;
    float dy = event.scrollingDeltaY;
    if (event.hasPreciseScrollingDeltas) dy /= _cellHeight;
    if (fabs(dy) < 0.1) return;
    if (_impl->screen->mouseMode() != termcore::MouseMode::None) {
        int lines = std::max(1, (int)fabs(dy));
        int sType = dy > 0 ? 3 : 4, sBtn = dy > 0 ? 4 : 5;
        for (int i = 0; i < lines; i++)
            [self sendMouseEvent:sType button:sBtn event:event];
        return;
    }
    int delta = (int)(dy > 0 ? ceil(dy) : floor(dy));
    int maxScroll = (int)_impl->screen->scrollbackSize();
    _scrollOffset = std::max(0, std::min(_scrollOffset + delta, maxScroll));
}

@end
