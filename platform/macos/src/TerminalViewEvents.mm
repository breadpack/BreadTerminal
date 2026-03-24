#import "TerminalView.h"
#import "TerminalViewImpl.h"

#include "termcore/terminal_controller.h"
#include "termcore/screen.h"
#include "termcore/font/unicode_width.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/glyph_cache.h"
#include "MetalTextRenderer.h"
#include "termcore/selection_manager.h"

#include <mach/mach_time.h>

@implementation TerminalView (Events)

#pragma mark - Focus Events

- (void)windowDidBecomeKey:(NSNotification*)notification {
    termcore::Screen* scr = _impl->controller ? _impl->controller->activeScreen() : nullptr;
    if (scr && scr->focusEvents()) {
        termcore::Pty* pty = _impl->controller->tabs()->activePty();
        if (pty) {
            const char* seq = "\033[I";
            pty->write(seq, 3);
        }
    }
    _impl->needsRender = true;
    [self markActivity];
}

- (void)windowDidResignKey:(NSNotification*)notification {
    termcore::Screen* scr = _impl->controller ? _impl->controller->activeScreen() : nullptr;
    if (scr && scr->focusEvents()) {
        termcore::Pty* pty = _impl->controller->tabs()->activePty();
        if (pty) {
            const char* seq = "\033[O";
            pty->write(seq, 3);
        }
    }
    _impl->needsRender = true;
}

#pragma mark - Rendering

- (void)setNeedsRender {
    _impl->needsRender = true;
    [self markActivity];
}

- (void)renderFrame {
    termcore::Screen* screen = _impl->controller ? _impl->controller->activeScreen() : nullptr;

    bool imeActive = _markedText != nil && _markedText.length > 0;
    bool cursorNeedsRedraw = !imeActive && screen && screen->cursorVisible();
    if (!_impl->needsRender && !cursorNeedsRedraw) return;
    _impl->needsRender = false;

    if (_impl->controller) {
        _impl->controller->flushPendingUrlScan();
        _impl->controller->clearNeedsRender();
    }

    // Force Retina drawable size every frame
    {
        CGFloat scale = self.window.backingScaleFactor > 0 ? self.window.backingScaleFactor : 2.0;
        NSSize sz = self.bounds.size;
        if (sz.width > 0 && sz.height > 0) {
            float pxW = sz.width * scale;
            float pxH = sz.height * scale;
            _metalLayer.drawableSize = NSMakeSize(pxW, pxH);
            _impl->renderer->resize(pxW, pxH);
        }
    }

    if (!screen) return;

    // Inject IME marked text into screen cells before render, restore after
    struct IMESavedCell { int row, col; termcore::TermCell cell; };
    std::vector<IMESavedCell> imeSaved;

    bool imeComposing = _markedText != nil && _markedText.length > 0;
    _impl->renderer->setIMEActive(imeComposing);

    if (imeComposing) {
        int curRow = screen->cursorRow();
        int curCol = screen->cursorCol();
        int cols = screen->cols();
        int rows = screen->rows();
        if (curRow >= 0 && curRow < rows) {
            int col = curCol;
            for (NSUInteger i = 0; i < _markedText.length; ++i) {
                if (col >= cols) break;

                unichar ch = [_markedText characterAtIndex:i];
                char32_t cp = (char32_t)ch;
                if (i + 1 < _markedText.length && CFStringIsSurrogateHighCharacter(ch)) {
                    unichar lo = [_markedText characterAtIndex:i + 1];
                    if (CFStringIsSurrogateLowCharacter(lo)) {
                        cp = CFStringGetLongCharacterForSurrogatePair(ch, lo);
                        ++i;
                    }
                }

                int w = termcore::codepoint_width(cp);
                if (w < 1) w = 1;
                if (col + w > cols) break;

                for (int c = col; c < col + w; ++c) {
                    const termcore::TermCell& orig = screen->cellAt(curRow, c);
                    imeSaved.push_back({curRow, c, orig});
                }

                termcore::TermCell& cell = screen->mutableCellAt(curRow, col);
                cell.codepoint = cp;
                cell.fg_color = screen->dynamicColors().background;
                cell.bg_color = screen->dynamicColors().foreground;
                cell.attributes = 0;
                cell.width = w;

                if (w == 2 && col + 1 < cols) {
                    termcore::TermCell& cont = screen->mutableCellAt(curRow, col + 1);
                    cont.codepoint = 0;
                    cont.fg_color = cell.fg_color;
                    cont.bg_color = cell.bg_color;
                    cont.attributes = 0;
                    cont.width = 0;
                }

                col += w;
            }
        }
    }

    // Update tab bar state from controller
    if (_impl->controller) {
        auto tabs = _impl->controller->tabBarInfo();
        const auto& config = _impl->controller->config();
        float cellH = _impl->controller->cellHeight();

        termcore::MetalTextRenderer::TabBarInfo tabInfo;
        tabInfo.visible = static_cast<int>(tabs.size()) > 1;

        // Tab bar uses same background as terminal area
        tabInfo.bg_color = config.background;
        tabInfo.active_bg_color = config.background;
        tabInfo.inactive_bg_color = config.background;
        tabInfo.fg_color = config.foreground;
        tabInfo.accent_color = config.palette[4] ? config.palette[4] : 0x007acc;
        tabInfo.process_icon_map = &config.tab_process_icons;

        // Preserve hover state from previous frame
        auto prevTabBar = _impl->renderer->getTabBar();
        tabInfo.hovered_tab = prevTabBar.hovered_tab;
        tabInfo.hover_close = prevTabBar.hover_close;
        tabInfo.hover_plus = prevTabBar.hover_plus;

        for (size_t i = 0; i < tabs.size(); ++i) {
            termcore::MetalTextRenderer::TabInfo ti;
            ti.title = tabs[i].title;
            ti.icon_name = tabs[i].icon_name;
            ti.process_name = tabs[i].process_name;
            ti.active = tabs[i].active;
            ti.has_unread = tabs[i].has_unread;
            ti.needs_attention = tabs[i].needs_attention;
            tabInfo.tabs.push_back(ti);
        }

        _impl->renderer->setTabBar(tabInfo);
    }

    // Pass selection state from controller to renderer
    {
        const auto& selMgr = _impl->controller->selection();
        termcore::SelectionState sel;
        sel.active = selMgr.hasSelection();
        sel.block = false;  // block selection is not yet supported in controller
        sel.start_row = selMgr.start().row;
        sel.start_col = selMgr.start().col;
        sel.end_row = selMgr.end().row;
        sel.end_col = selMgr.end().col;
        _impl->renderer->setSelection(sel);
    }

    _impl->renderer->render(*screen);

    // Clear dirty flags after successful render
    screen->clearDirty();

    // Restore original cells
    for (const auto& sc : imeSaved) {
        termcore::TermCell& cell = screen->mutableCellAt(sc.row, sc.col);
        cell = sc.cell;
    }
}

#pragma mark - Config hot reload

- (void)applyConfigDelta:(const termcore::Config&)config
                   dirty:(termcore::ConfigDirtyFlags)dirty {
    using namespace termcore;

    if (hasFlag(dirty, ConfigDirtyFlags::Font)) {
        if (!config.font_family.empty()) {
            _impl->fontCollection->setPrimaryFont(config.font_family, config.font_size);
            auto metrics = _impl->fontCollection->primaryMetrics();
            _cellWidth  = metrics.cell_width  > 0 ? metrics.cell_width  : 16.0f;
            _cellHeight = metrics.cell_height > 0 ? metrics.cell_height : 32.0f;
            // Invalidate glyph cache since font changed
            _impl->cache = std::make_unique<GlyphCache>();
            _impl->atlas = std::make_unique<GlyphAtlas>();
            _impl->renderer->setFontStack(
                _impl->fontCollection.get(), _impl->cache.get(),
                _impl->atlas.get(), _impl->rasterizer.get());
            [self updateGridSize];
        }
    }

    if (hasFlag(dirty, ConfigDirtyFlags::Colors) || hasFlag(dirty, ConfigDirtyFlags::Theme)) {
        if (!config.theme.empty()) {
            _impl->currentThemeString = config.theme;
        }
        // Update via controller
        if (_impl->controller) {
            _impl->controller->onConfigChanged(config);
        }
        [self applyTransparencyConfig:config];
    }

    if (hasFlag(dirty, ConfigDirtyFlags::Keybindings)) {
        // Controller handles keybinding updates
        if (_impl->controller) {
            _impl->controller->onConfigChanged(config);
        }
    }

    [self setNeedsRender];
}

#pragma mark - Font size callback from MacPlatformHost

- (void)onCellSizeChanged:(float)cellW height:(float)cellH {
    _cellWidth = cellW;
    _cellHeight = cellH;

    // Invalidate glyph cache since font changed
    _impl->cache = std::make_unique<termcore::GlyphCache>();
    _impl->atlas = std::make_unique<termcore::GlyphAtlas>();
    _impl->renderer->setFontStack(
        _impl->fontCollection.get(), _impl->cache.get(),
        _impl->atlas.get(), _impl->rasterizer.get());

    [self updateGridSize];
    _impl->needsRender = true;
}

#pragma mark - Socket API accessors

- (termcore::TerminalController*)controller {
    return _impl->controller.get();
}

- (termcore::NotificationStore&)notifications { return *_impl->notifications; }
- (termcore::AgentTracker&)agentTracker { return *_impl->agentTracker; }

#pragma mark - Config error display

- (void)showConfigError:(NSString*)message {
    static const NSInteger kConfigErrorBannerTag = 9001;
    for (NSView* subview in [self.subviews copy]) {
        if (subview.tag == kConfigErrorBannerTag) {
            [subview removeFromSuperview];
        }
    }

    CGFloat bannerHeight = 28.0;
    NSRect bannerRect = NSMakeRect(0,
                                    self.bounds.size.height - bannerHeight,
                                    self.bounds.size.width,
                                    bannerHeight);

    NSTextField* errorLabel = [[NSTextField alloc] initWithFrame:bannerRect];
    errorLabel.tag = kConfigErrorBannerTag;
    errorLabel.stringValue = [NSString stringWithFormat:@" Config error: %@", message];
    errorLabel.editable = NO;
    errorLabel.bordered = NO;
    errorLabel.selectable = NO;
    errorLabel.drawsBackground = YES;
    errorLabel.backgroundColor = [NSColor colorWithRed:0.8 green:0.1 blue:0.1 alpha:0.9];
    errorLabel.textColor = [NSColor whiteColor];
    errorLabel.font = [NSFont systemFontOfSize:12.0 weight:NSFontWeightMedium];
    errorLabel.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;
    [self addSubview:errorLabel];

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC),
                   dispatch_get_main_queue(), ^{
        [errorLabel removeFromSuperview];
    });
}

@end
