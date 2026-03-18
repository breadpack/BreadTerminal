#import "TerminalViewImpl.h"

#include "termcore/keybinding.h"
#include "termcore/search.h"
#include "termcore/url_detector.h"
#include "termcore/mouse.h"
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

#pragma mark - Input category

@implementation TerminalView (Input)

#pragma mark - Keyboard

- (void)keyDown:(NSEvent*)event {
    // If search field is active and Escape is pressed, close search.
    if (_searchActive && event.keyCode == 53) {
        [self closeSearch];
        return;
    }

    // Try keybinding lookup first.
    uint32_t keycode = keycodeFromEvent(event);
    uint8_t mods = modsFromEvent(event);
    termcore::KeyCombo combo{keycode, mods};
    auto action = _impl->keybindings->lookup(combo);

    if (action != termcore::Action::None) {
        [self handleAction:action];
        return;
    }

    // Existing special key handling.
    if ([self handleSpecialKey:event]) return;

    // Normal text input.
    NSString* chars = event.characters;
    if (chars.length > 0) [self sendText:chars];
}

- (BOOL)handleSpecialKey:(NSEvent*)event {
    unsigned short kc = event.keyCode;
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
    if (kc == 122) { [self writePty:"\x1bOP"];  return YES; }
    if (kc == 120) { [self writePty:"\x1bOQ"];  return YES; }
    if (kc == 99)  { [self writePty:"\x1bOR"];  return YES; }
    if (kc == 118) { [self writePty:"\x1bOS"];  return YES; }
    if (kc == 115) { [self writePty:"\x1b[H"]; return YES; }
    if (kc == 119) { [self writePty:"\x1b[F"]; return YES; }
    if (kc == 116) { [self writePty:"\x1b[5~"]; return YES; }
    if (kc == 121) { [self writePty:"\x1b[6~"]; return YES; }
    if (kc == 117) { [self writePty:"\x1b[3~"]; return YES; }
    return NO;
}

- (BOOL)performKeyEquivalent:(NSEvent*)event {
    // Let keybinding manager handle Cmd+key combos.
    if (event.modifierFlags & NSEventModifierFlagCommand) {
        uint32_t keycode = keycodeFromEvent(event);
        uint8_t mods = modsFromEvent(event);
        termcore::KeyCombo combo{keycode, mods};
        auto action = _impl->keybindings->lookup(combo);
        if (action != termcore::Action::None) {
            [self handleAction:action];
            return YES;
        }
        return [super performKeyEquivalent:event];
    }
    return NO;
}

- (void)handleAction:(termcore::Action)action {
    switch (action) {
    case termcore::Action::Copy:
        [self copy:nil];
        break;
    case termcore::Action::Paste:
        [self paste:nil];
        break;
    case termcore::Action::SearchOpen:
        [self openSearch];
        break;
    case termcore::Action::SearchClose:
        [self closeSearch];
        break;
    case termcore::Action::SearchNext:
        if (_impl->search->isActive()) {
            _impl->search->next();
            [self setNeedsRender];
        }
        break;
    case termcore::Action::SearchPrev:
        if (_impl->search->isActive()) {
            _impl->search->prev();
            [self setNeedsRender];
        }
        break;
    case termcore::Action::FontIncrease:
    case termcore::Action::FontDecrease:
    case termcore::Action::FontReset:
        // Font size changes: not yet implemented
        break;
    case termcore::Action::ClearScrollback:
        // Clear scrollback: not yet implemented
        break;
    case termcore::Action::ToggleFullscreen:
        [self.window toggleFullScreen:nil];
        break;
    default:
        break;
    }
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
    _impl->search->clear();
    if (_searchField) {
        [_searchField removeFromSuperview];
        _searchField = nil;
    }
    [self.window makeFirstResponder:self];
    [self setNeedsRender];
}

- (void)searchFieldAction:(id)sender {
    NSString* query = _searchField.stringValue;
    if (query.length == 0) {
        _impl->search->clear();
        [self setNeedsRender];
        return;
    }
    std::string q = [query UTF8String];
    _impl->search->search(*_impl->screen, q);
    [self setNeedsRender];
}

#pragma mark - Mouse events

- (NSPoint)cellPositionForEvent:(NSEvent*)event {
    NSPoint loc = [self convertPoint:event.locationInWindow fromView:nil];
    float flippedY = self.bounds.size.height - loc.y;
    int col = std::max(0, std::min((int)(loc.x / _cellWidth), self.termCols - 1));
    int row = std::max(0, std::min((int)(flippedY / _cellHeight), self.termRows - 1));
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
    // Cmd+click to open URL
    if (event.modifierFlags & NSEventModifierFlagCommand) {
        NSPoint cell = [self cellPositionForEvent:event];
        int row = (int)cell.y;
        int col = (int)cell.x;
        auto urls = _impl->urlDetector->detectInScreen(*_impl->screen);
        std::string url = _impl->urlDetector->urlAt(urls, row, col);
        if (!url.empty()) {
            NSURL* nsUrl = [NSURL URLWithString:
                [NSString stringWithUTF8String:url.c_str()]];
            if (nsUrl) [[NSWorkspace sharedWorkspace] openURL:nsUrl];
            return;
        }
    }

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
    NSPoint cell = [self cellPositionForEvent:event];
    int row = (int)cell.y;
    int col = (int)cell.x;

    auto urls = _impl->urlDetector->detectInScreen(*_impl->screen);
    std::string url = _impl->urlDetector->urlAt(urls, row, col);

    if (!url.empty()) {
        [[NSCursor pointingHandCursor] set];
    } else {
        [[NSCursor IBeamCursor] set];
    }
}

#pragma mark - Scroll wheel

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
