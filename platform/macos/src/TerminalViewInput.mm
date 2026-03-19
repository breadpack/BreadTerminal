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
    { FILE* f = fopen("/dev/null", "a") /* debug disabled */;
      if (f) { fprintf(f, "keyDown: kc=%d ch='%s'\n", event.keyCode,
               [event.characters ?: @"" UTF8String]); fclose(f); } }

    // Debug screenshot: Cmd+Shift+S
    if ((event.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagShift)) ==
        (NSEventModifierFlagCommand | NSEventModifierFlagShift)) {
        NSString* chars = event.charactersIgnoringModifiers;
        if ([chars isEqualToString:@"s"] || [chars isEqualToString:@"S"]) {
            [self captureScreenshot];
            return;
        }
    }

    // If copy mode is active, intercept ALL keys (don't send to PTY)
    if (_impl->copyModeActive) {
        [self handleCopyModeKey:event];
        return;
    }

    // If search field is active and Escape is pressed, close search.
    if (_searchActive && event.keyCode == 53) {
        [self closeSearch];
        return;
    }

    // If IME composition is active, route ALL keys through IME
    // (backspace edits jamo, arrows/space/enter commit composition)
    if (_markedText != nil && _markedText.length > 0) {
        [self interpretKeyEvents:@[event]];
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

    // Existing special key handling (arrows, function keys, etc.)
    if ([self handleSpecialKey:event]) return;

    // Route through IME system for proper CJK/Korean composition.
    [self interpretKeyEvents:@[event]];
}

#pragma mark - NSTextInputClient

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
    _markedText = nil;
    NSString* text = [string isKindOfClass:[NSAttributedString class]]
        ? [(NSAttributedString*)string string] : (NSString*)string;
    { FILE* f = fopen("/dev/null", "a") /* debug disabled */;
      if (f) { fprintf(f, "insertText: '%s'\n", text ? [text UTF8String] : "nil"); fclose(f); } }
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
    { FILE* f = fopen("/dev/null", "a") /* debug disabled */;
      if (f) { fprintf(f, "setMarkedText: '%s'\n", _markedText ? [_markedText UTF8String] : "nil"); fclose(f); } }
}

- (void)unmarkText {
    _markedText = nil;
    _impl->needsRender = true;
}

- (void)doCommandBySelector:(SEL)selector {
    // Called by interpretKeyEvents: for non-text commands (backspace, arrows, etc.)
    // When IME is not composing, translate to terminal escape sequences.
    if (!_impl->pty) return;
    if (selector == @selector(deleteBackward:)) {
        [self writePty:"\x7f"];
    } else if (selector == @selector(deleteForward:)) {
        [self writePty:"\x1b[3~"];
    } else if (selector == @selector(moveUp:)) {
        bool appCur = _impl->screen && _impl->screen->appCursorKeys();
        [self writePty:appCur ? "\x1bOA" : "\x1b[A"];
    } else if (selector == @selector(moveDown:)) {
        bool appCur = _impl->screen && _impl->screen->appCursorKeys();
        [self writePty:appCur ? "\x1bOB" : "\x1b[B"];
    } else if (selector == @selector(moveRight:)) {
        bool appCur = _impl->screen && _impl->screen->appCursorKeys();
        [self writePty:appCur ? "\x1bOC" : "\x1b[C"];
    } else if (selector == @selector(moveLeft:)) {
        bool appCur = _impl->screen && _impl->screen->appCursorKeys();
        [self writePty:appCur ? "\x1bOD" : "\x1b[D"];
    } else if (selector == @selector(insertNewline:)) {
        [self writePty:"\r"];
    } else if (selector == @selector(insertTab:)) {
        [self writePty:"\t"];
    } else if (selector == @selector(cancelOperation:)) {
        [self writePty:"\x1b"];
    } else {
        // Unknown command — do nothing (don't beep)
    }
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
    // Return the cursor position for IME candidate window placement.
    // Convert terminal cursor position to screen coordinates.
    NSRect viewRect = NSMakeRect(0, 0, 100, 20);  // Default fallback
    if (_impl->screen) {
        int cursorCol = _impl->screen->cursorCol();
        int cursorRow = _impl->screen->cursorRow();
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

- (BOOL)handleSpecialKey:(NSEvent*)event {
    if (!_impl->pty) return NO;
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
    // Debug screenshot: Cmd+Shift+S (keyCode 1 = 's')
    if ((event.modifierFlags & NSEventModifierFlagCommand) &&
        (event.modifierFlags & NSEventModifierFlagShift) &&
        event.keyCode == 1) {
        [self captureScreenshot];
        return YES;
    }
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
    case termcore::Action::EnterCopyMode:
        [self enterCopyMode];
        break;
    case termcore::Action::ReloadConfig:
        // Notify AppDelegate to trigger config reload via the watcher
        [[NSNotificationCenter defaultCenter]
            postNotificationName:@"BreadTerminalReloadConfig" object:nil];
        break;
    case termcore::Action::JumpPromptUp: {
        if (!_impl->screen) break;
        // Current top visible absolute row
        int scrollbackSize = (int)_impl->screen->scrollbackSize();
        int topAbsRow = scrollbackSize - _scrollOffset;
        int target = _impl->screen->previousPromptRow(topAbsRow);
        if (target >= 0) {
            // Convert absolute row to scroll offset
            _scrollOffset = std::max(0, scrollbackSize - target);
            _scrollOffset = std::min(_scrollOffset, scrollbackSize);
            [self setNeedsRender];
        }
        break;
    }
    case termcore::Action::JumpPromptDown: {
        if (!_impl->screen) break;
        int scrollbackSize = (int)_impl->screen->scrollbackSize();
        int topAbsRow = scrollbackSize - _scrollOffset;
        int target = _impl->screen->nextPromptRow(topAbsRow);
        if (target >= 0) {
            _scrollOffset = std::max(0, scrollbackSize - target);
            _scrollOffset = std::min(_scrollOffset, scrollbackSize);
            [self setNeedsRender];
        } else {
            // No next prompt, scroll to bottom
            _scrollOffset = 0;
            [self setNeedsRender];
        }
        break;
    }
    default:
        break;
    }
}

#pragma mark - Debug Screenshot

- (void)captureScreenshot {
    // Write debug log to file (NSLog may not appear in system log)
    FILE* logFile = fopen("/tmp/BreadTerminal_debug.log", "w");
    if (logFile) fprintf(logFile, "captureScreenshot called\n");

    @autoreleasepool {
        CGSize size = _metalLayer.drawableSize;
        if (size.width <= 0 || size.height <= 0) {
            NSLog(@"BreadTerminal: screenshot failed - no drawable size");
            return;
        }

        // Capture Metal layer content by reading back from GPU
        id<CAMetalDrawable> drawable = [_metalLayer nextDrawable];
        if (!drawable) {
            fprintf(logFile, "no drawable for screenshot\n");
            fclose(logFile);
            return;
        }
        id<MTLTexture> tex = drawable.texture;
        int w = (int)tex.width;
        int h = (int)tex.height;
        std::vector<uint8_t> pixels(w * h * 4);
        [tex getBytes:pixels.data() bytesPerRow:w*4
               fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
        // Convert to NSImage
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

        // Also dump detailed render state
        NSLog(@"BreadTerminal: === DEBUG STATE ===");
        NSLog(@"BreadTerminal: view bounds=%.0fx%.0f", self.bounds.size.width, self.bounds.size.height);
        NSLog(@"BreadTerminal: drawable=%.0fx%.0f scale=%.1f",
              _metalLayer.drawableSize.width, _metalLayer.drawableSize.height,
              _metalLayer.contentsScale);
        NSLog(@"BreadTerminal: cellW=%.1f cellH=%.1f rows=%d cols=%d",
              _cellWidth, _cellHeight, self.termRows, self.termCols);

        if (_impl->screen) {
            int glyphCells = 0;
            for (int r = 0; r < _impl->screen->rows(); r++)
                for (int c = 0; c < _impl->screen->cols(); c++)
                    if (_impl->screen->cellAt(r, c).codepoint != ' ' &&
                        _impl->screen->cellAt(r, c).codepoint != 0) glyphCells++;
            NSLog(@"BreadTerminal: screen has %d non-empty cells", glyphCells);

            // First row content
            NSString* line0 = [NSString stringWithUTF8String:
                               _impl->screen->getLineText(0).c_str()];
            NSLog(@"BreadTerminal: line[0] = '%@'", line0);
        }

        if (_impl->atlas) {
            auto* page = _impl->atlas->getPage(termcore::AtlasFormat::R8);
            NSLog(@"BreadTerminal: atlas R8 page=%p", (void*)page);
            if (page) {
                NSLog(@"BreadTerminal: atlas R8 size=%dx%d dirty=%d",
                      page->width(), page->height(), page->isDirty());
                // Save atlas to PNG for visual inspection
                NSBitmapImageRep* atlasRep = [[NSBitmapImageRep alloc]
                    initWithBitmapDataPlanes:NULL
                    pixelsWide:page->width()
                    pixelsHigh:page->height()
                    bitsPerSample:8 samplesPerPixel:1
                    hasAlpha:NO isPlanar:NO
                    colorSpaceName:NSCalibratedWhiteColorSpace
                    bytesPerRow:page->width()
                    bitsPerPixel:8];
                memcpy(atlasRep.bitmapData, page->data(), page->width() * page->height());
                NSData* atlasPng = [atlasRep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
                NSString* atlasPath = [NSString stringWithFormat:@"/tmp/BreadTerminal_atlas_%ld.png",
                                       (long)[[NSDate date] timeIntervalSince1970]];
                [atlasPng writeToFile:atlasPath atomically:YES];
                NSLog(@"BreadTerminal: atlas saved to %@", atlasPath);
            }
        }
        // Dump first few cell instances UV data
        if (_impl->renderer) {
            // Access renderer's cell instances isn't possible from here,
            // so dump screen cell + glyph info manually
            auto& fc = *_impl->fontCollection;
            auto& gc = *_impl->cache;
            auto& ga = *_impl->atlas;
            auto* rast = _impl->rasterizer.get();
            float fontSize = fc.fontSize();

            for (int col = 0; col < std::min(10, _impl->screen->cols()); col++) {
                auto& cell = _impl->screen->cellAt(0, col);
                if (cell.codepoint == ' ' || cell.codepoint == 0) continue;
                auto faceId = fc.resolveFace(cell.codepoint);
                auto rastFace = fc.rasterizerFaceId(faceId);
                uint32_t gi = rast->getGlyphIndex(rastFace, cell.codepoint);
                if (gi == 0) continue;
                termcore::GlyphKey key{rastFace, gi, {0,0}};
                auto info = gc.get(key);
                if (info) {
                    fprintf(logFile, "cell[0,%d] cp=U+%04X uv=(%d,%d) size=(%d,%d) bearing=(%d,%d)\n",
                            col, cell.codepoint,
                            info->region.x, info->region.y,
                            info->region.width, info->region.height,
                            info->region.bearing_x, info->region.bearing_y);
                }
            }

            auto* r8Page = ga.getPage(termcore::AtlasFormat::R8);
            if (r8Page) {
                fprintf(logFile, "atlas: %dx%d\n", r8Page->width(), r8Page->height());
            }

            auto metrics = fc.primaryMetrics();
            fprintf(logFile, "viewport: %.0fx%.0f cellSize: %.1fx%.1f ascent: %.1f descent: %.1f\n",
                    _metalLayer.drawableSize.width, _metalLayer.drawableSize.height,
                    metrics.cell_width, metrics.cell_height,
                    metrics.ascent, metrics.descent);
        }

        NSLog(@"BreadTerminal: === END DEBUG ===");
        if (logFile) { fprintf(logFile, "done\n"); fclose(logFile); }
    }
}

#pragma mark - Clipboard

- (void)paste:(id)sender {
    NSString* text = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    if (!text || text.length == 0 || !_impl->pty) return;

    BOOL bracketed = _impl->screen && _impl->screen->bracketedPaste();
    std::string utf8Str = [text UTF8String];

    auto analysis = _impl->pasteGuard->analyze(utf8Str, bracketed);
    if (analysis.danger == termcore::PasteDanger::Safe) {
        [self executePaste:text bracketed:bracketed];
    } else {
        [self confirmPaste:text analysis:analysis bracketed:bracketed];
    }
}

- (void)copy:(id)sender {
    if (!_impl->screen || !_selecting) return;
    int sr = (int)_selectionStart.y, er = (int)_selectionEnd.y;
    int sc = (int)_selectionStart.x, ec = (int)_selectionEnd.x;

    if (_blockSelection) {
        // Block (rectangular) selection: each row uses same column range
        int minRow = std::min(sr, er);
        int maxRow = std::max(sr, er);
        int minCol = std::min(sc, ec);
        int maxCol = std::max(sc, ec);
        NSMutableString* result = [NSMutableString string];
        for (int r = minRow; r <= maxRow; r++) {
            auto lt = _impl->screen->getLineText(r);
            NSString* line = [NSString stringWithUTF8String:lt.c_str()];
            if (!line) continue;
            NSUInteger from = std::min((NSUInteger)minCol, line.length);
            NSUInteger to = std::min((NSUInteger)(maxCol + 1), line.length);
            if (to > from) {
                [result appendString:[line substringWithRange:NSMakeRange(from, to - from)]];
            }
            if (r < maxRow) [result appendString:@"\n"];
        }
        if (result.length > 0) {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];
            [pb setString:result forType:NSPasteboardTypeString];
        }
        return;
    }

    // Line-based selection
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

#pragma mark - Word boundary detection

static bool isWordChar(char32_t cp) {
    return cp > ' ' && cp != '"' && cp != '\'' && cp != '(' && cp != ')'
        && cp != '[' && cp != ']' && cp != '{' && cp != '}'
        && cp != '<' && cp != '>' && cp != '`' && cp != '|'
        && cp != ';' && cp != '&';
}

static bool isCopyModeWordChar(char32_t cp) {
    return isWordChar(cp);
}

#pragma mark - Mouse events

- (NSPoint)cellPositionForEvent:(NSEvent*)event {
    NSPoint loc = [self convertPoint:event.locationInWindow fromView:nil];
    float flippedY = self.bounds.size.height - loc.y;
    // loc is in points; _cellWidth/_cellHeight are in physical pixels.
    // Scale points to physical pixels before dividing by cell dimensions.
    CGFloat scale = _metalLayer.contentsScale > 0 ? _metalLayer.contentsScale : 2.0;
    float padding = _impl->windowPadding;  // logical pixels
    int col = std::max(0, std::min((int)((loc.x - padding) * scale / _cellWidth), self.termCols - 1));
    int row = std::max(0, std::min((int)((flippedY - padding) * scale / _cellHeight), self.termRows - 1));
    return NSMakePoint(col, row);
}

- (void)selectWordAtRow:(int)row col:(int)col {
    if (!_impl->screen) return;
    int cols = _impl->screen->cols();

    // Find left boundary
    int left = col;
    while (left > 0) {
        char32_t cp = _impl->screen->cellAt(row, left - 1).codepoint;
        if (!isWordChar(cp)) break;
        left--;
    }

    // Find right boundary
    int right = col;
    while (right < cols - 1) {
        char32_t cp = _impl->screen->cellAt(row, right + 1).codepoint;
        if (!isWordChar(cp)) break;
        right++;
    }

    _selectionStart = NSMakePoint(left, row);
    _selectionEnd = NSMakePoint(right, row);
    _selecting = YES;
    _blockSelection = NO;
    _impl->needsRender = true;
}

- (void)selectLineAtRow:(int)row {
    if (!_impl->screen) return;
    int cols = _impl->screen->cols();

    // Find last non-space column
    int lastCol = cols - 1;
    while (lastCol > 0) {
        char32_t cp = _impl->screen->cellAt(row, lastCol).codepoint;
        if (cp != ' ' && cp != 0) break;
        lastCol--;
    }

    _selectionStart = NSMakePoint(0, row);
    _selectionEnd = NSMakePoint(lastCol, row);
    _selecting = YES;
    _blockSelection = NO;
    _impl->needsRender = true;
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
    // Ensure we're first responder on click
    if (self.window.firstResponder != self) {
        [self.window makeFirstResponder:self];
    }

    // Cmd+triple-click: select command output region
    if ((event.modifierFlags & NSEventModifierFlagCommand) &&
        event.clickCount == 3 && _impl->screen) {
        NSPoint cell = [self cellPositionForEvent:event];
        int visibleRow = (int)cell.y;
        int scrollbackSize = (int)_impl->screen->scrollbackSize();
        int absoluteRow = scrollbackSize - _scrollOffset + visibleRow;
        auto [startRow, endRow] = _impl->screen->outputRegionAt(absoluteRow);
        if (startRow >= 0) {
            // Convert absolute rows back to visible rows
            int visStart = startRow - (scrollbackSize - _scrollOffset);
            int visEnd   = endRow   - (scrollbackSize - _scrollOffset);
            _selectionStart = NSMakePoint(0, visStart);
            _selectionEnd   = NSMakePoint(_impl->screen->cols() - 1, visEnd - 1);
            _selecting = YES;
            _blockSelection = NO;
            [self setNeedsRender];
            return;
        }
    }

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

    NSPoint cell = [self cellPositionForEvent:event];
    int clickCount = (int)event.clickCount;

    // Triple-click: select entire line
    if (clickCount == 3) {
        [self selectLineAtRow:(int)cell.y];
        return;
    }

    // Double-click: select word at cursor
    if (clickCount == 2) {
        [self selectWordAtRow:(int)cell.y col:(int)cell.x];
        return;
    }

    // Single click: start normal or block selection
    _selectionStart = cell;
    _selectionEnd = cell;
    _selecting = YES;
    _blockSelection = (event.modifierFlags & NSEventModifierFlagOption) != 0;
    _impl->needsRender = true;
}

- (void)mouseUp:(NSEvent*)event {
    if ([self sendMouseEvent:1 button:3 event:event]) return;
    if (_selecting) _selectionEnd = [self cellPositionForEvent:event];
}

- (void)mouseDragged:(NSEvent*)event {
    if ([self sendMouseEvent:2 button:0 event:event]) return;
    if (_selecting) {
        _selectionEnd = [self cellPositionForEvent:event];
        // Allow toggling block selection during drag if Alt is pressed/released
        _blockSelection = (event.modifierFlags & NSEventModifierFlagOption) != 0;
        _impl->needsRender = true;
    }
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

    bool cmdHeld = (event.modifierFlags & NSEventModifierFlagCommand) != 0;

    if (cmdHeld && _impl->screen) {
        auto urls = _impl->urlDetector->detectInScreen(*_impl->screen);
        std::string url = _impl->urlDetector->urlAt(urls, row, col);

        if (!url.empty()) {
            // Find the matching URL range for underline highlighting
            for (const auto& u : urls) {
                if (u.row == row && col >= u.start_col && col < u.end_col) {
                    _impl->renderer->setUrlHighlight(row, u.start_col, u.end_col);
                    break;
                }
            }
            [[NSCursor pointingHandCursor] set];
            _impl->needsRender = true;
            return;
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

#pragma mark - Copy mode

- (void)enterCopyMode {
    if (_impl->copyModeActive || !_impl->screen) return;

    _impl->copyModeActive = true;
    _impl->copyModeCursorRow = _impl->screen->cursorRow();
    _impl->copyModeCursorCol = _impl->screen->cursorCol();
    _impl->copyModeSelecting = false;
    _impl->copyModeLineSelect = false;
    _impl->copyModeSearchMode = false;
    _impl->copyModeWaitingG = false;

    // Show "-- COPY --" indicator at the bottom of the terminal
    CGFloat labelHeight = 22.0;
    CGFloat labelWidth = 120.0;
    CGFloat x = (self.bounds.size.width - labelWidth) / 2.0;
    NSRect labelRect = NSMakeRect(x, 4.0, labelWidth, labelHeight);

    _copyModeLabel = [[NSTextField alloc] initWithFrame:labelRect];
    _copyModeLabel.stringValue = @"-- COPY --";
    _copyModeLabel.editable = NO;
    _copyModeLabel.bordered = NO;
    _copyModeLabel.selectable = NO;
    _copyModeLabel.drawsBackground = YES;
    _copyModeLabel.backgroundColor = [NSColor colorWithRed:0.8 green:0.7 blue:0.0 alpha:0.9];
    _copyModeLabel.textColor = [NSColor blackColor];
    _copyModeLabel.font = [NSFont boldSystemFontOfSize:12.0];
    _copyModeLabel.alignment = NSTextAlignmentCenter;
    _copyModeLabel.autoresizingMask = NSViewMinXMargin | NSViewMaxXMargin | NSViewMaxYMargin;
    [self addSubview:_copyModeLabel];

    _impl->needsRender = true;
}

- (void)exitCopyMode {
    if (!_impl->copyModeActive) return;

    _impl->copyModeActive = false;
    _impl->copyModeSelecting = false;
    _impl->copyModeLineSelect = false;
    _impl->copyModeSearchMode = false;
    _impl->copyModeWaitingG = false;

    // Remove selection
    _selecting = NO;

    // Remove indicator label
    if (_copyModeLabel) {
        [_copyModeLabel removeFromSuperview];
        _copyModeLabel = nil;
    }

    _impl->needsRender = true;
}

/// Ensure the copy mode cursor is visible by adjusting scroll offset.
- (void)copyModeEnsureCursorVisible {
    if (!_impl->screen) return;
    int rows = _impl->screen->rows();
    int scrollbackSize = (int)_impl->screen->scrollbackSize();

    // copyModeCursorRow is a visible row (0..rows-1 = visible, negative = above visible)
    // Convert to absolute row: absRow = scrollbackSize - _scrollOffset + visibleRow
    int visRow = _impl->copyModeCursorRow;

    if (visRow < 0) {
        // Cursor is above visible area, scroll up
        _scrollOffset = std::min(_scrollOffset - visRow, scrollbackSize);
        _impl->copyModeCursorRow = 0;
    } else if (visRow >= rows) {
        // Cursor is below visible area, scroll down
        int excess = visRow - (rows - 1);
        _scrollOffset = std::max(0, _scrollOffset - excess);
        _impl->copyModeCursorRow = rows - 1;
    }
}

/// Update the selection state based on copy mode cursor position.
- (void)copyModeUpdateSelection {
    if (!_impl->copyModeSelecting) return;

    if (_impl->copyModeLineSelect) {
        // Line selection: full rows between start and current
        int cols = _impl->screen ? _impl->screen->cols() : 80;
        _selectionStart = NSMakePoint(0, _impl->copyModeSelectStartRow);
        _selectionEnd = NSMakePoint(cols - 1, _impl->copyModeCursorRow);
    } else {
        _selectionStart = NSMakePoint(_impl->copyModeSelectStartCol,
                                       _impl->copyModeSelectStartRow);
        _selectionEnd = NSMakePoint(_impl->copyModeCursorCol,
                                     _impl->copyModeCursorRow);
    }
    _selecting = YES;
    _blockSelection = NO;
    _impl->needsRender = true;
}

/// Get the codepoint at a given (visible) row and column, handling scrollback.
- (char32_t)copyModeCellAt:(int)row col:(int)col {
    if (!_impl->screen) return ' ';
    int rows = _impl->screen->rows();
    int cols = _impl->screen->cols();
    if (col < 0 || col >= cols) return ' ';
    // The visible row is in range [0, rows-1]. With scrollback offset,
    // the actual screen row maps directly if row is in [0, rows-1].
    if (row >= 0 && row < rows) {
        return _impl->screen->cellAt(row, col).codepoint;
    }
    // For scrollback rows (when _scrollOffset > 0 and row maps to scrollback),
    // we can use getScrollbackLineText, but cellAt only works for visible rows.
    // In copy mode, cursor stays in visible range after ensureCursorVisible.
    return ' ';
}

- (BOOL)handleCopyModeKey:(NSEvent*)event {
    if (!_impl->copyModeActive || !_impl->screen) return NO;

    // If search mode is active, route to search
    if (_impl->copyModeSearchMode) {
        if (event.keyCode == 53) { // Escape - cancel search
            _impl->copyModeSearchMode = false;
            [self closeSearch];
            return YES;
        }
        if (event.keyCode == 36 || event.keyCode == 76) { // Enter - accept search
            _impl->copyModeSearchMode = false;
            // Jump cursor to current search match if any
            if (_impl->search->isActive() && _impl->search->matchCount() > 0) {
                auto* match = _impl->search->currentMatch();
                if (match) {
                    _impl->copyModeCursorRow = match->row;
                    _impl->copyModeCursorCol = match->start_col;
                    [self copyModeEnsureCursorVisible];
                    [self copyModeUpdateSelection];
                }
            }
            // Keep search highlights visible but return focus to copy mode
            if (_searchField) {
                [self.window makeFirstResponder:self];
            }
            return YES;
        }
        // Let the search field handle the key
        return NO;
    }

    NSString* chars = event.charactersIgnoringModifiers;
    if (!chars || chars.length == 0) return YES;
    unichar ch = [chars characterAtIndex:0];
    uint8_t mods = modsFromEvent(event);

    int rows = _impl->screen->rows();
    int cols = _impl->screen->cols();
    int scrollbackSize = (int)_impl->screen->scrollbackSize();

    // Handle 'gg' sequence
    if (_impl->copyModeWaitingG) {
        _impl->copyModeWaitingG = false;
        if (ch == 'g') {
            // gg: go to top of scrollback
            _scrollOffset = scrollbackSize;
            _impl->copyModeCursorRow = 0;
            _impl->copyModeCursorCol = 0;
            [self copyModeUpdateSelection];
            _impl->needsRender = true;
            return YES;
        }
        // Not 'g', fall through to handle the new key normally
    }

    switch (ch) {
        // --- Navigation ---
        case 'h': // left
            _impl->copyModeCursorCol = std::max(0, _impl->copyModeCursorCol - 1);
            break;

        case 'j': // down
            _impl->copyModeCursorRow++;
            [self copyModeEnsureCursorVisible];
            break;

        case 'k': // up
            _impl->copyModeCursorRow--;
            [self copyModeEnsureCursorVisible];
            break;

        case 'l': // right
            _impl->copyModeCursorCol = std::min(cols - 1, _impl->copyModeCursorCol + 1);
            break;

        case 'w': { // next word
            int r = _impl->copyModeCursorRow;
            int c = _impl->copyModeCursorCol;
            // Skip current word
            while (c < cols - 1 && isCopyModeWordChar([self copyModeCellAt:r col:c])) c++;
            // Skip whitespace
            while (c < cols - 1 && !isCopyModeWordChar([self copyModeCellAt:r col:c])) c++;
            if (c >= cols - 1 && r < rows - 1) {
                // Wrap to next line
                r++;
                c = 0;
                while (c < cols - 1 && !isCopyModeWordChar([self copyModeCellAt:r col:c])) c++;
            }
            _impl->copyModeCursorRow = r;
            _impl->copyModeCursorCol = c;
            [self copyModeEnsureCursorVisible];
            break;
        }

        case 'b': { // previous word
            int r = _impl->copyModeCursorRow;
            int c = _impl->copyModeCursorCol;
            // Move back one
            if (c > 0) c--;
            // Skip whitespace
            while (c > 0 && !isCopyModeWordChar([self copyModeCellAt:r col:c])) c--;
            if (c == 0 && !isCopyModeWordChar([self copyModeCellAt:r col:c]) && r > 0) {
                r--;
                c = cols - 1;
                while (c > 0 && !isCopyModeWordChar([self copyModeCellAt:r col:c])) c--;
            }
            // Go to start of word
            while (c > 0 && isCopyModeWordChar([self copyModeCellAt:r col:c - 1])) c--;
            _impl->copyModeCursorRow = r;
            _impl->copyModeCursorCol = c;
            [self copyModeEnsureCursorVisible];
            break;
        }

        case '0': // line start
            _impl->copyModeCursorCol = 0;
            break;

        case '$': // line end
            _impl->copyModeCursorCol = cols - 1;
            // Find last non-space
            while (_impl->copyModeCursorCol > 0) {
                char32_t cp = [self copyModeCellAt:_impl->copyModeCursorRow
                                               col:_impl->copyModeCursorCol];
                if (cp != ' ' && cp != 0) break;
                _impl->copyModeCursorCol--;
            }
            break;

        case 'g': // first 'g' of 'gg'
            _impl->copyModeWaitingG = true;
            return YES; // Don't update selection yet

        case 'G': // bottom of scrollback (current content)
            _scrollOffset = 0;
            _impl->copyModeCursorRow = rows - 1;
            _impl->copyModeCursorCol = 0;
            break;

        case 'd': // Ctrl+d: half page down
            if (mods & termcore::ModCtrl) {
                int halfPage = rows / 2;
                _impl->copyModeCursorRow += halfPage;
                [self copyModeEnsureCursorVisible];
            }
            break;

        case 'u': // Ctrl+u: half page up
            if (mods & termcore::ModCtrl) {
                int halfPage = rows / 2;
                _impl->copyModeCursorRow -= halfPage;
                [self copyModeEnsureCursorVisible];
            }
            break;

        case '/': // Search mode
            _impl->copyModeSearchMode = true;
            [self openSearch];
            return YES;

        case 'n': // next search match
            if (_impl->search->isActive()) {
                _impl->search->next();
                if (_impl->search->matchCount() > 0) {
                    auto* match = _impl->search->currentMatch();
                    if (match) {
                        _impl->copyModeCursorRow = match->row;
                        _impl->copyModeCursorCol = match->start_col;
                        [self copyModeEnsureCursorVisible];
                    }
                }
            }
            break;

        case 'N': // previous search match
            if (_impl->search->isActive()) {
                _impl->search->prev();
                if (_impl->search->matchCount() > 0) {
                    auto* match = _impl->search->currentMatch();
                    if (match) {
                        _impl->copyModeCursorRow = match->row;
                        _impl->copyModeCursorCol = match->start_col;
                        [self copyModeEnsureCursorVisible];
                    }
                }
            }
            break;

        case 'v': // visual selection
            if (!_impl->copyModeSelecting) {
                _impl->copyModeSelecting = true;
                _impl->copyModeLineSelect = false;
                _impl->copyModeSelectStartRow = _impl->copyModeCursorRow;
                _impl->copyModeSelectStartCol = _impl->copyModeCursorCol;
                // Update label
                if (_copyModeLabel) _copyModeLabel.stringValue = @"-- VISUAL --";
            } else {
                // Toggle off
                _impl->copyModeSelecting = false;
                _selecting = NO;
                if (_copyModeLabel) _copyModeLabel.stringValue = @"-- COPY --";
            }
            break;

        case 'V': // visual line selection
            if (!_impl->copyModeSelecting || !_impl->copyModeLineSelect) {
                _impl->copyModeSelecting = true;
                _impl->copyModeLineSelect = true;
                _impl->copyModeSelectStartRow = _impl->copyModeCursorRow;
                _impl->copyModeSelectStartCol = 0;
                if (_copyModeLabel) _copyModeLabel.stringValue = @"-- VISUAL LINE --";
            } else {
                _impl->copyModeSelecting = false;
                _impl->copyModeLineSelect = false;
                _selecting = NO;
                if (_copyModeLabel) _copyModeLabel.stringValue = @"-- COPY --";
            }
            break;

        case 'y': // yank
            if (_impl->copyModeSelecting) {
                [self copyModeYank];
                [self exitCopyMode];
                return YES;
            }
            break;

        default:
            // Check for Escape (keyCode 53) or 'q'
            if (event.keyCode == 53 || ch == 'q') {
                [self exitCopyMode];
                return YES;
            }
            return YES; // Consume all keys in copy mode
    }

    [self copyModeUpdateSelection];
    _impl->needsRender = true;
    return YES;
}

- (void)copyModeYank {
    if (!_impl->screen || !_selecting) return;

    // Delegate to the existing copy: method which reads _selectionStart/_selectionEnd
    [self copy:nil];
}

@end
