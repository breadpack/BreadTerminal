#import "TerminalViewImpl.h"

#include "termcore/terminal_controller.h"
#include "termcore/platform_host.h"
#include "termcore/keybinding.h"
#include "termcore/screen.h"

#include <AppKit/AppKit.h>

// Map an NSEvent key code to the termcore keycode used by KeybindingManager.
// The keybinding system uses lowercase ASCII for printable keys and special
// constants (0xF7xx) for non-printable keys.
static uint32_t keycodeFromEvent(NSEvent* event) {
    unsigned short kc = event.keyCode;
    // Map macOS virtual keycodes for special keys to termcore constants.
    switch (kc) {
        case 126: return 0xF700; // Up
        case 125: return 0xF701; // Down
        case 123: return 0xF702; // Left
        case 124: return 0xF703; // Right
        case 115: return 0xF704; // Home
        case 119: return 0xF705; // End
        case 116: return 0xF706; // PageUp
        case 121: return 0xF707; // PageDown
        case 48:  return 0xF708; // Tab
        case 36:  return 0xF709; // Enter/Return
        case 76:  return 0xF709; // Numpad Enter
        case 53:  return 0xF70A; // Escape
        case 51:  return 0xF70B; // Backspace
        case 49:  return 0xF70C; // Space
        case 117: return 0xF70D; // Delete forward
        case 122: return 0xF710; // F1
        case 120: return 0xF711; // F2
        case 99:  return 0xF712; // F3
        case 118: return 0xF713; // F4
        case 96:  return 0xF714; // F5
        case 97:  return 0xF715; // F6
        case 98:  return 0xF716; // F7
        case 100: return 0xF717; // F8
        case 101: return 0xF718; // F9
        case 109: return 0xF719; // F10
        case 103: return 0xF71A; // F11
        case 111: return 0xF71B; // F12
        default: break;
    }
    // For printable characters, use the lowercase character code.
    NSString* chars = event.charactersIgnoringModifiers;
    if (chars.length > 0) {
        unichar ch = [chars characterAtIndex:0];
        if (ch >= 'A' && ch <= 'Z') ch = ch - 'A' + 'a';
        return static_cast<uint32_t>(ch);
    }
    return 0;
}

static uint8_t modsFromEvent(NSEvent* event) {
    uint8_t mods = 0;
    NSEventModifierFlags flags = event.modifierFlags;
    if (flags & NSEventModifierFlagShift)   mods |= termcore::ModShift;
    if (flags & NSEventModifierFlagControl) mods |= termcore::ModCtrl;
    if (flags & NSEventModifierFlagOption)  mods |= termcore::ModAlt;
    if (flags & NSEventModifierFlagCommand) mods |= termcore::ModSuper;
    return mods;
}

/// Build a KeyEvent from an NSEvent.
static termcore::KeyEvent keyEventFromNSEvent(NSEvent* event) {
    termcore::KeyEvent ke;
    ke.keycode = keycodeFromEvent(event);
    ke.modifiers = modsFromEvent(event);
    ke.isRepeat = event.isARepeat;
    NSString* chars = event.characters;
    if (chars && chars.length > 0) {
        ke.text = std::string([chars UTF8String]);
    }
    return ke;
}

#pragma mark - Input category

@implementation TerminalView (Input)

#pragma mark - Keyboard

- (void)keyDown:(NSEvent*)event {
    // Debug screenshot: Cmd+Shift+S
    if ((event.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagShift)) ==
        (NSEventModifierFlagCommand | NSEventModifierFlagShift)) {
        NSString* chars = event.charactersIgnoringModifiers;
        if ([chars isEqualToString:@"s"] || [chars isEqualToString:@"S"]) {
            [self captureScreenshot];
            return;
        }
    }

    // If search field is active and Escape is pressed, close search.
    if (_searchActive && event.keyCode == 53) {
        [self closeSearch];
        return;
    }

    // If IME composition is active, route ALL keys through IME
    if (_markedText != nil && _markedText.length > 0) {
        [self interpretKeyEvents:@[event]];
        return;
    }

    // Build KeyEvent and delegate to controller
    if (_impl->controller) {
        termcore::KeyEvent ke = keyEventFromNSEvent(event);
        _impl->controller->onKeyEvent(ke);
        return;
    }

    // Fallback: route through IME system
    [self interpretKeyEvents:@[event]];
}

#pragma mark - NSTextInputClient

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
    _markedText = nil;
    NSString* text = [string isKindOfClass:[NSAttributedString class]]
        ? [(NSAttributedString*)string string] : (NSString*)string;
    if (text.length > 0) {
        [self sendText:text];
    }
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange
     replacementRange:(NSRange)replacementRange {
    NSString* text = [string isKindOfClass:[NSAttributedString class]]
        ? [(NSAttributedString*)string string] : (NSString*)string;
    _markedText = (text.length > 0) ? [text copy] : nil;
    _markedSelectedRange = selectedRange;
    _impl->needsRender = true;
}

- (void)unmarkText {
    _markedText = nil;
    _impl->needsRender = true;
}

- (void)doCommandBySelector:(SEL)selector {
    // Called by interpretKeyEvents: for non-text commands (backspace, arrows, etc.)
    // When controller exists, route through it; otherwise handle locally.
    if (!_impl->controller) return;

    // Map selector to keycode and send as KeyEvent
    termcore::KeyEvent ke;
    ke.modifiers = termcore::ModNone;
    ke.isRepeat = false;

    if (selector == @selector(deleteBackward:)) {
        ke.keycode = 0xF70B;  // Backspace
    } else if (selector == @selector(deleteForward:)) {
        ke.keycode = 0xF70D;  // Delete
    } else if (selector == @selector(moveUp:)) {
        ke.keycode = 0xF700;  // Up
    } else if (selector == @selector(moveDown:)) {
        ke.keycode = 0xF701;  // Down
    } else if (selector == @selector(moveRight:)) {
        ke.keycode = 0xF703;  // Right
    } else if (selector == @selector(moveLeft:)) {
        ke.keycode = 0xF702;  // Left
    } else if (selector == @selector(insertNewline:)) {
        ke.keycode = 0xF709;  // Return
    } else if (selector == @selector(insertTab:)) {
        ke.keycode = 0xF708;  // Tab
    } else if (selector == @selector(cancelOperation:)) {
        ke.keycode = 0xF70A;  // Escape
    } else {
        return;  // Unknown command -- do nothing
    }

    _impl->controller->onKeyEvent(ke);
}

- (BOOL)hasMarkedText {
    return _markedText != nil && _markedText.length > 0;
}

- (NSRange)markedRange {
    if (_markedText && _markedText.length > 0) {
        return NSMakeRange(0, _markedText.length);
    }
    return NSMakeRange(NSNotFound, 0);
}

- (NSRange)selectedRange {
    return NSMakeRange(NSNotFound, 0);
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange {
    NSRect viewRect = NSMakeRect(0, 0, 100, 20);
    termcore::Screen* scr = _impl->controller ? _impl->controller->activeScreen() : nullptr;
    if (scr) {
        int cursorCol = scr->cursorCol();
        int cursorRow = scr->cursorRow();
        float scale = _metalLayer.contentsScale > 0 ? _metalLayer.contentsScale : 2.0f;
        float cellW = _cellWidth / scale;
        float cellH = _cellHeight / scale;
        viewRect = NSMakeRect(cursorCol * cellW, cursorRow * cellH, cellW, cellH);
    }
    return [self.window convertRectToScreen:[self convertRect:viewRect toView:nil]];
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range
                                              actualRange:(NSRangePointer)actualRange {
    return nil;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    return NSNotFound;
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText {
    return @[];
}

- (BOOL)performKeyEquivalent:(NSEvent*)event {
    // Debug screenshot: Cmd+Shift+S (keyCode 1 = 's')
    if ((event.modifierFlags & NSEventModifierFlagCommand) &&
        (event.modifierFlags & NSEventModifierFlagShift) &&
        event.keyCode == 1) {
        [self captureScreenshot];
        return YES;
    }
    // Let controller handle Cmd+key combos via keybinding lookup.
    if ((event.modifierFlags & NSEventModifierFlagCommand) && _impl->controller) {
        termcore::KeyEvent ke = keyEventFromNSEvent(event);
        // Check if keybinding exists before consuming the event
        termcore::KeyCombo combo{ke.keycode, ke.modifiers};
        auto action = _impl->controller->keybindings()->lookup(combo);
        if (action != termcore::Action::None) {
            _impl->controller->onKeyEvent(ke);
            return YES;
        }
        return [super performKeyEquivalent:event];
    }
    return NO;
}

#pragma mark - Clipboard (one-line delegates to controller)

- (void)copy:(id)sender {
    if (_impl->controller) {
        _impl->controller->onKeyEvent(termcore::KeyEvent{
            .keycode = 'c',
            .modifiers = termcore::ModSuper
        });
    }
}

- (void)paste:(id)sender {
    if (_impl->controller) {
        _impl->controller->onKeyEvent(termcore::KeyEvent{
            .keycode = 'v',
            .modifiers = termcore::ModSuper
        });
    }
}

#pragma mark - Search

- (void)openSearch {
    if (_searchActive) {
        [self.window makeFirstResponder:_searchField];
        return;
    }
    _searchActive = YES;

    CGFloat barHeight = 28.0;
    CGFloat barWidth = 300.0;
    CGFloat x = (self.bounds.size.width - barWidth) / 2.0;
    CGFloat y = self.bounds.size.height - barHeight - 4.0;
    NSRect fieldRect = NSMakeRect(x, y, barWidth, barHeight);

    _searchField = [[NSTextField alloc] initWithFrame:fieldRect];
    _searchField.placeholderString = @"Search...";
    _searchField.bezeled = YES;
    _searchField.bezelStyle = NSTextFieldRoundedBezel;
    _searchField.target = self;
    _searchField.action = @selector(searchFieldAction:);
    _searchField.autoresizingMask = NSViewMinXMargin | NSViewMaxXMargin | NSViewMinYMargin;
    [self addSubview:_searchField];
    [self.window makeFirstResponder:_searchField];
}

- (void)closeSearch {
    _searchActive = NO;
    if (_impl->controller) {
        _impl->controller->onSearchQuery("");
    }
    if (_searchField) {
        [_searchField removeFromSuperview];
        _searchField = nil;
    }
    [self.window makeFirstResponder:self];
    [self setNeedsRender];
}

- (void)searchFieldAction:(id)sender {
    NSString* query = _searchField.stringValue;
    if (!_impl->controller) return;
    if (query.length == 0) {
        _impl->controller->onSearchQuery("");
        [self setNeedsRender];
        return;
    }
    std::string q = [query UTF8String];
    _impl->controller->onSearchQuery(q);
    [self setNeedsRender];
}

#pragma mark - Mouse helpers

- (NSPoint)pixelPositionForEvent:(NSEvent*)event {
    NSPoint loc = [self convertPoint:event.locationInWindow fromView:nil];
    // NSView default: Y=0 at bottom. Terminal row 0 at top. Flip Y.
    CGFloat flippedY = self.bounds.size.height - loc.y;
    // Convert to drawable pixels
    NSSize ds = _metalLayer.drawableSize;
    NSSize bs = self.bounds.size;
    CGFloat scaleX = (bs.width > 0 && ds.width > 0) ? ds.width / bs.width : 2.0;
    CGFloat scaleY = (bs.height > 0 && ds.height > 0) ? ds.height / bs.height : 2.0;
    float padding = _impl->windowPadding;
    return NSMakePoint((loc.x - padding) * scaleX, (flippedY - padding) * scaleY);
}

#pragma mark - Mouse events

- (void)mouseDown:(NSEvent*)event {
    // Ensure we're first responder on click
    if (self.window.firstResponder != self) {
        [self.window makeFirstResponder:self];
    }

    if (!_impl->controller) return;

    NSPoint px = [self pixelPositionForEvent:event];
    int clickCount = (int)event.clickCount;

    termcore::InputMouseEvent me;
    me.x = (int)px.x;
    me.y = (int)px.y;
    me.modifiers = modsFromEvent(event);
    me.button = 0;  // left

    if (clickCount == 2) {
        me.type = termcore::InputMouseEvent::DoubleClick;
    } else {
        me.type = termcore::InputMouseEvent::Press;
    }

    _impl->controller->onMouseEvent(me);
}

- (void)mouseUp:(NSEvent*)event {
    if (!_impl->controller) return;

    NSPoint px = [self pixelPositionForEvent:event];

    termcore::InputMouseEvent me;
    me.type = termcore::InputMouseEvent::Release;
    me.x = (int)px.x;
    me.y = (int)px.y;
    me.modifiers = modsFromEvent(event);
    me.button = 0;

    _impl->controller->onMouseEvent(me);
}

- (void)mouseDragged:(NSEvent*)event {
    if (!_impl->controller) return;

    NSPoint px = [self pixelPositionForEvent:event];

    termcore::InputMouseEvent me;
    me.type = termcore::InputMouseEvent::Move;
    me.x = (int)px.x;
    me.y = (int)px.y;
    me.modifiers = modsFromEvent(event);
    me.button = 0;

    _impl->controller->onMouseEvent(me);
}

- (void)rightMouseDown:(NSEvent*)event {
    if (!_impl->controller) return;

    NSPoint px = [self pixelPositionForEvent:event];

    termcore::InputMouseEvent me;
    me.type = termcore::InputMouseEvent::Press;
    me.x = (int)px.x;
    me.y = (int)px.y;
    me.modifiers = modsFromEvent(event);
    me.button = 2;  // right

    _impl->controller->onMouseEvent(me);
}

- (void)rightMouseUp:(NSEvent*)event {
    if (!_impl->controller) return;

    NSPoint px = [self pixelPositionForEvent:event];

    termcore::InputMouseEvent me;
    me.type = termcore::InputMouseEvent::Release;
    me.x = (int)px.x;
    me.y = (int)px.y;
    me.modifiers = modsFromEvent(event);
    me.button = 2;

    _impl->controller->onMouseEvent(me);
}

#pragma mark - URL detection on mouse hover

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_trackingArea) {
        [self removeTrackingArea:_trackingArea];
    }
    _trackingArea = [[NSTrackingArea alloc]
        initWithRect:self.bounds
             options:(NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect)
               owner:self
            userInfo:nil];
    [self addTrackingArea:_trackingArea];
}

- (void)mouseMoved:(NSEvent*)event {
    if (!_impl->controller) return;

    NSPoint px = [self pixelPositionForEvent:event];
    bool cmdHeld = (event.modifierFlags & NSEventModifierFlagCommand) != 0;

    if (cmdHeld) {
        // Check URL detection from controller
        float cw = _impl->controller->cellWidth();
        float ch = _impl->controller->cellHeight();
        int col = (int)(px.x / cw);
        int row = (int)(px.y / ch);

        const auto& urls = _impl->controller->detectedUrls();
        for (const auto& u : urls) {
            if (u.row == row && col >= u.start_col && col < u.end_col) {
                _impl->renderer->setUrlHighlight(row, u.start_col, u.end_col);
                [[NSCursor pointingHandCursor] set];
                _impl->needsRender = true;
                return;
            }
        }
    }

    // Clear URL highlight
    _impl->renderer->setUrlHighlight(-1, -1, -1);
    [[NSCursor IBeamCursor] set];
    _impl->needsRender = true;
}

- (void)flagsChanged:(NSEvent*)event {
    // When Cmd is released, clear URL highlight
    if (!(event.modifierFlags & NSEventModifierFlagCommand)) {
        _impl->renderer->setUrlHighlight(-1, -1, -1);
        [[NSCursor IBeamCursor] set];
        _impl->needsRender = true;
    }
}

#pragma mark - Scroll wheel

- (void)scrollWheel:(NSEvent*)event {
    if (!_impl->controller) return;

    float dy = event.scrollingDeltaY;
    if (event.hasPreciseScrollingDeltas) dy /= _cellHeight;
    if (fabs(dy) < 0.1) return;

    int lines = std::max(1, (int)fabs(dy));
    NSPoint px = [self pixelPositionForEvent:event];

    termcore::InputMouseEvent me;
    me.type = (dy > 0) ? termcore::InputMouseEvent::ScrollUp
                       : termcore::InputMouseEvent::ScrollDown;
    me.x = (int)px.x;
    me.y = (int)px.y;
    me.modifiers = modsFromEvent(event);
    me.scrollLines = lines;

    _impl->controller->onMouseEvent(me);
}

#pragma mark - Debug Screenshot

- (void)captureScreenshot {
    FILE* logFile = fopen("/tmp/BreadTerminal_debug.log", "w");
    if (logFile) fprintf(logFile, "captureScreenshot called\n");

    @autoreleasepool {
        CGSize size = _metalLayer.drawableSize;
        if (size.width <= 0 || size.height <= 0) {
            NSLog(@"BreadTerminal: screenshot failed - no drawable size");
            return;
        }

        id<CAMetalDrawable> drawable = [_metalLayer nextDrawable];
        if (!drawable) {
            if (logFile) { fprintf(logFile, "no drawable for screenshot\n"); fclose(logFile); }
            return;
        }
        id<MTLTexture> tex = drawable.texture;
        int w = (int)tex.width;
        int h = (int)tex.height;
        std::vector<uint8_t> pixels(w * h * 4);
        [tex getBytes:pixels.data() bytesPerRow:w*4
               fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];

        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:NULL pixelsWide:w pixelsHigh:h
            bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
            colorSpaceName:NSCalibratedRGBColorSpace bytesPerRow:w*4 bitsPerPixel:32];
        memcpy(rep.bitmapData, pixels.data(), pixels.size());
        NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];

        NSString* path = [NSString stringWithFormat:@"/tmp/BreadTerminal_debug_%ld.png",
                          (long)[[NSDate date] timeIntervalSince1970]];
        [png writeToFile:path atomically:YES];
        NSLog(@"BreadTerminal: screenshot saved to %@", path);

        NSLog(@"BreadTerminal: === DEBUG STATE ===");
        NSLog(@"BreadTerminal: view bounds=%.0fx%.0f", self.bounds.size.width, self.bounds.size.height);
        NSLog(@"BreadTerminal: drawable=%.0fx%.0f scale=%.1f",
              _metalLayer.drawableSize.width, _metalLayer.drawableSize.height,
              _metalLayer.contentsScale);
        NSLog(@"BreadTerminal: cellW=%.1f cellH=%.1f rows=%d cols=%d",
              _cellWidth, _cellHeight, self.termRows, self.termCols);

        termcore::Screen* screen = _impl->controller ? _impl->controller->activeScreen() : nullptr;
        if (screen) {
            int glyphCells = 0;
            for (int r = 0; r < screen->rows(); r++)
                for (int c = 0; c < screen->cols(); c++)
                    if (screen->cellAt(r, c).codepoint != ' ' &&
                        screen->cellAt(r, c).codepoint != 0) glyphCells++;
            NSLog(@"BreadTerminal: screen has %d non-empty cells", glyphCells);

            NSString* line0 = [NSString stringWithUTF8String:
                               screen->getLineText(0).c_str()];
            NSLog(@"BreadTerminal: line[0] = '%@'", line0);
        }

        NSLog(@"BreadTerminal: === END DEBUG ===");
        if (logFile) { fprintf(logFile, "done\n"); fclose(logFile); }
    }
}

@end
