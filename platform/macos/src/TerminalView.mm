#import "TerminalView.h"

#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include "termcore/pty.h"
#include "termcore/keybinding.h"
#include "termcore/search.h"
#include "termcore/url_detector.h"
#include "termcore/font/font_collection.h"
#include "termcore/font/unicode_width.h"
#include "termcore/font/font_shaper.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/glyph_cache.h"
#include "MetalTextRenderer.h"
#include "CoreTextRasterizer.h"
#include "CoreTextDiscovery.h"
#include "TerminalViewImpl.h"
#include "termcore/theme_loader.h"

#import <UserNotifications/UserNotifications.h>
#include <dispatch/dispatch.h>
#include <mach/mach_time.h>
#include <memory>

// CVDisplayLink callback (fallback for < macOS 14)
static CVReturn displayLinkCallback(CVDisplayLinkRef /*displayLink*/,
                                     const CVTimeStamp* /*now*/,
                                     const CVTimeStamp* /*outputTime*/,
                                     CVOptionFlags /*flagsIn*/,
                                     CVOptionFlags* /*flagsOut*/,
                                     void* context) {
    TerminalView* view = (__bridge TerminalView*)context;
    dispatch_async(dispatch_get_main_queue(), ^{
        [view renderFrame];
    });
    return kCVReturnSuccess;
}

// Idle downclock constants
static const double kIdleTimeoutSeconds = 1.0;
static const NSTimeInterval kIdleCheckInterval = 0.1; // 10 fps when idle

// Convert mach_absolute_time to seconds
static double machTimeToSeconds(uint64_t elapsed) {
    static mach_timebase_info_data_t sTimebaseInfo = {0, 0};
    if (sTimebaseInfo.denom == 0) {
        mach_timebase_info(&sTimebaseInfo);
    }
    return (double)(elapsed * sTimebaseInfo.numer) / (double)(sTimebaseInfo.denom * 1000000000ULL);
}

@implementation TerminalView

#pragma mark - Initialisation

- (instancetype)initWithFrame:(NSRect)frame device:(id<MTLDevice>)device {
    self = [super initWithFrame:frame];
    if (!self) return nil;

    _device = device;
    _impl = new TerminalViewImpl();
    _markedText = nil;

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
    CGFloat scale = self.window.backingScaleFactor ?: 2.0;
    _impl->rasterizer->setScaleFactor(static_cast<float>(scale));
    _impl->discovery  = termcore::createCoreTextDiscovery();
    _impl->shaper     = std::make_unique<termcore::FontShaper>();
    _impl->fontCollection = std::make_unique<termcore::FontCollection>(
        *_impl->rasterizer, *_impl->discovery, *_impl->shaper);
    _impl->fontCollection->setPrimaryFont("Menlo", 16.0f);
    _impl->atlas = std::make_unique<termcore::GlyphAtlas>();
    _impl->cache = std::make_unique<termcore::GlyphCache>();

    // Renderer
    _impl->renderer = std::make_unique<termcore::MetalTextRenderer>(_device, _metalLayer);
    _impl->renderer->setFontStack(
        _impl->fontCollection.get(), _impl->cache.get(),
        _impl->atlas.get(), _impl->rasterizer.get());

    // Cell dimensions from font metrics (already in physical pixels)
    auto metrics = _impl->fontCollection->primaryMetrics();
    _cellWidth  = metrics.cell_width  > 0 ? metrics.cell_width  : 16.0f;
    _cellHeight = metrics.cell_height > 0 ? metrics.cell_height : 32.0f;

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

    // Paste guard (default config)
    _impl->pasteGuard = std::make_unique<termcore::PasteGuard>();

    // Mux, notifications, agent tracker (for socket API)
    _impl->mux = std::make_unique<termcore::Mux>();
    _impl->notifications = std::make_unique<termcore::NotificationStore>();
    _impl->agentTracker = std::make_unique<termcore::AgentTracker>();

    _searchActive = NO;

    // Set initial drawable size and viewport — both in physical pixels
    _metalLayer.drawableSize = NSMakeSize(frame.size.width * scale, frame.size.height * scale);
    _impl->renderer->resize(frame.size.width * scale, frame.size.height * scale);
    _impl->needsRender = true;

    // Create PTY serial queue
    _impl->ptyQueue = dispatch_queue_create("com.breadterminal.pty", DISPATCH_QUEUE_SERIAL);

    // Initialize idle tracking
    _impl->lastActivityTime = mach_absolute_time();
    _impl->idleMode = false;

    // Render timer will be started after view is added to window
    // (cannot use __weak self during init — may be nil)

    return self;
}

- (void)markActivity {}

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    if (self.window && !_impl->idleTimer) {
        // Start 60fps render timer now that view is in a window
        __weak TerminalView* weakSelf = self;
        _impl->idleTimer = [NSTimer scheduledTimerWithTimeInterval:1.0/60.0
                                                            repeats:YES
                                                              block:^(NSTimer* timer) {
            TerminalView* s = weakSelf;
            if (!s) { [timer invalidate]; return; }
            [s renderFrame];
        }];
        _metalLayer.contentsScale = self.window.backingScaleFactor;
    }
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    // TerminalViewImpl destructor handles ptyReadSource cancel and display link cleanup
    delete _impl;
}

- (void)applyConfig:(const termcore::Config&)config {
    // Font
    if (!config.font_family.empty()) {
        _impl->fontCollection->setPrimaryFont(config.font_family, config.font_size);
        auto metrics = _impl->fontCollection->primaryMetrics();
        _cellWidth  = metrics.cell_width  > 0 ? metrics.cell_width  : 16.0f;
        _cellHeight = metrics.cell_height > 0 ? metrics.cell_height : 32.0f;
    }

    // Keybindings from config
    if (!config.keybindings.empty()) {
        std::vector<std::pair<std::string, std::string>> bindings;
        for (const auto& kb : config.keybindings) {
            bindings.emplace_back(kb.trigger, kb.action);
        }
        _impl->keybindings->loadFromConfig(bindings);
    }

    // Paste guard
    {
        termcore::PasteGuard::Config pgCfg;
        if (config.clipboard_paste_protection == "always")
            pgCfg.mode = termcore::PasteGuard::Config::Mode::Always;
        else if (config.clipboard_paste_protection == "never")
            pgCfg.mode = termcore::PasteGuard::Config::Mode::Never;
        else
            pgCfg.mode = termcore::PasteGuard::Config::Mode::Multiline;
        pgCfg.trust_bracketed = config.clipboard_paste_bracketed_safe;
        _impl->pasteGuard = std::make_unique<termcore::PasteGuard>(pgCfg);
    }

    // Scrollback limit
    if (config.scrollback_limit > 0) {
        _impl->screen->setMaxScrollback(static_cast<size_t>(config.scrollback_limit));
    }

    // Cursor blink interval
    _impl->renderer->setCursorBlinkInterval(config.cursor_blink_interval);

    // Command completion notification settings
    _impl->notifyOnCommandFinish = config.notify_on_command_finish;
    _impl->screen->setNotifyAfterSeconds(config.notify_after_seconds);

    // Store the raw theme string for adaptive theme switching
    if (!config.theme.empty()) {
        _impl->currentThemeString = config.theme;
    }

    // Initialize screen dynamic colors from the (already theme-resolved) config
    _impl->screen->initDynamicColors(config);

    // Background transparency & blur
    [self applyTransparencyConfig:config];

    // Grid padding
    _impl->windowPadding = config.window_padding;
    {
        CGFloat padScale = _metalLayer.contentsScale > 0 ? _metalLayer.contentsScale : 2.0;
        _impl->renderer->setGridPadding(config.window_padding * padScale);
    }

    // Minimum contrast
    _impl->renderer->setMinimumContrast(config.minimum_contrast);

    // Recalculate grid with potentially new font metrics
    [self updateGridSize];
}

- (void)applyThemeByName:(const std::string&)themeName {
    auto theme = termcore::findTheme(themeName);
    if (!theme) return;

    // Build a temporary config with the theme colors applied
    termcore::Config tempConfig;
    termcore::applyTheme(tempConfig, *theme);

    // Update the screen's dynamic colors
    _impl->screen->initDynamicColors(tempConfig);

    // Update transparency config (background color may have changed)
    [self applyTransparencyConfig:tempConfig];

    // Trigger re-render
    _impl->needsRender = true;
}

- (void)viewDidChangeEffectiveAppearance {
    [super viewDidChangeEffectiveAppearance];

    // Only react if we have an adaptive theme configured
    if (_impl->currentThemeString.empty() ||
        !termcore::isAdaptiveTheme(_impl->currentThemeString)) {
        return;
    }

    // Detect current appearance
    BOOL isDark = YES;
    if (@available(macOS 10.14, *)) {
        NSAppearanceName match = [self.effectiveAppearance
            bestMatchFromAppearancesWithNames:@[NSAppearanceNameDarkAqua, NSAppearanceNameAqua]];
        isDark = [match isEqualToString:NSAppearanceNameDarkAqua];
    }

    // Resolve and apply the appropriate theme variant
    std::string resolved = termcore::resolveThemeForAppearance(_impl->currentThemeString, isDark);
    [self applyThemeByName:resolved];
}

- (void)applyTransparencyConfig:(const termcore::Config&)config {
    float opacity = config.background_opacity;
    int blur = config.background_blur;
    bool needsTransparency = (opacity < 1.0f) || (blur > 0);

    if (needsTransparency) {
        _metalLayer.framebufferOnly = NO;
        _metalLayer.opaque = NO;
    } else {
        _metalLayer.framebufferOnly = YES;
        _metalLayer.opaque = YES;
    }

    _impl->renderer->setBackgroundOpacity(opacity);

    if (blur > 0) {
        if (!_visualEffectView) {
            _visualEffectView = [[NSVisualEffectView alloc] initWithFrame:self.bounds];
            _visualEffectView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
            _visualEffectView.blendingMode = NSVisualEffectBlendingModeBehindWindow;
            _visualEffectView.state = NSVisualEffectStateActive;
            [self addSubview:_visualEffectView positioned:NSWindowBelow relativeTo:nil];
        }

        // Map blur level to material
        switch (blur) {
            case 1:
                _visualEffectView.material = NSVisualEffectMaterialHUDWindow;
                break;
            case 2:
                _visualEffectView.material = NSVisualEffectMaterialSheet;
                break;
            case 3:
            default:
                _visualEffectView.material = NSVisualEffectMaterialUnderWindowBackground;
                break;
        }
    } else {
        // Remove blur view if not needed
        if (_visualEffectView) {
            [_visualEffectView removeFromSuperview];
            _visualEffectView = nil;
        }
    }
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

// viewDidMoveToWindow is implemented above (line 144)

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
    // Cell dimensions are in physical pixels; convert view bounds to pixels
    float cw = _cellWidth  > 0 ? _cellWidth  : 16;
    float ch = _cellHeight > 0 ? _cellHeight : 32;
    float gridScale = _metalLayer.contentsScale > 0 ? _metalLayer.contentsScale : 2.0f;
    float paddingPx = _impl->windowPadding * gridScale;
    float viewWidthPx  = self.bounds.size.width  * gridScale - paddingPx * 2;
    float viewHeightPx = self.bounds.size.height * gridScale - paddingPx * 2;
    int cols = std::max(1, (int)(viewWidthPx  / cw));
    int rows = std::max(1, (int)(viewHeightPx / ch));
    return {rows, cols};
}

- (void)updateGridSize {
    auto [rows, cols] = [self calculateGridSize];
    if (rows == _termRows && cols == _termCols) return;
    _termRows = rows;
    _termCols = cols;
    _impl->screen->resize(rows, cols);
    if (_impl->pty && _impl->pty->isAlive()) _impl->pty->resize(rows, cols);
    // Viewport in physical pixels (matches drawableSize)
    float gridScale = _metalLayer.contentsScale > 0 ? _metalLayer.contentsScale : 2.0f;
    float w = self.bounds.size.width * gridScale;
    float h = self.bounds.size.height * gridScale;
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

    // Wire response callback so Screen can write back to PTY (DA, DSR, DECRPM, etc.)
    __weak TerminalView* weakSelf = self;
    _impl->screen->setResponseCallback([weakSelf](const std::string& response) {
        dispatch_async(dispatch_get_main_queue(), ^{
            TerminalView* strongSelf = weakSelf;
            if (!strongSelf || !strongSelf->_impl->pty) return;
            strongSelf->_impl->pty->write(response.data(), response.size());
        });
    });

    // Wire command finish callback for desktop notifications
    _impl->screen->setCommandFinishCallback([weakSelf](double duration_seconds) {
        dispatch_async(dispatch_get_main_queue(), ^{
            TerminalView* strongSelf = weakSelf;
            if (!strongSelf) return;

            // Check if notifications are enabled
            if (!strongSelf->_impl->notifyOnCommandFinish) return;

            // Only notify if the window is NOT the key window (app not focused)
            NSWindow* window = strongSelf.window;
            if (window && [window isKeyWindow] && [NSApp isActive]) return;

            [strongSelf postCommandFinishNotification:duration_seconds];
        });
    });

    // Register for focus event notifications (CSI I / CSI O)
    [[NSNotificationCenter defaultCenter] addObserver:self
        selector:@selector(windowDidBecomeKey:)
        name:NSWindowDidBecomeKeyNotification object:self.window];
    [[NSNotificationCenter defaultCenter] addObserver:self
        selector:@selector(windowDidResignKey:)
        name:NSWindowDidResignKeyNotification object:self.window];

    // Dispatch PTY reads on a dedicated serial queue
    int fd = _impl->pty->fd();
    dispatch_source_t src = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_READ, fd, 0, dispatch_get_main_queue());
    dispatch_source_set_event_handler(src, ^{
        TerminalView* s = weakSelf;
        if (s) [s readPtyDataOnMainQueue];
    });
    dispatch_source_set_cancel_handler(src, ^{});
    _impl->ptyReadSource = src;
    dispatch_resume(src);
}

- (void)readPtyDataOnMainQueue {
    // Called on main queue — read from PTY and feed parser directly
    char buf[65536];
    for (;;) {
        int n = _impl->pty->read(buf, sizeof(buf));
        if (n > 0) {
            _impl->parser->feed(buf, static_cast<size_t>(n));
        } else if (n < 0) {
            if (_impl->ptyReadSource) {
                dispatch_source_cancel(_impl->ptyReadSource);
                _impl->ptyReadSource = nullptr;
            }
            return;
        } else {
            break;
        }
    }

    _impl->needsRender = true;

    // Update window title from OSC sequences
    if (_impl->screen) {
        std::string title = _impl->screen->title();
        if (!title.empty()) {
            NSString* t = [NSString stringWithUTF8String:title.c_str()];
            if (t && ![self.window.title isEqualToString:t])
                self.window.title = t;
        }
    }

    // Exit copy mode on new PTY output
    if (_impl->copyModeActive) {
        [self exitCopyMode];
    }
}

#pragma mark - Text I/O

- (void)sendText:(NSString*)text {
    { FILE* f = fopen("/dev/null", "a") /* debug disabled */;
      if (f) { fprintf(f, "sendText: '%s'\n", [text UTF8String]); fclose(f); } }
    if (!_impl->pty) return;
    const char* utf8 = [text UTF8String];
    _impl->pty->write(utf8, [text lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
}

- (void)writePty:(const char*)str {
    { FILE* f = fopen("/dev/null", "a") /* debug disabled */;
      if (f) { fprintf(f, "writePty: '%s'\n", str); fclose(f); } }
    if (_impl->pty) _impl->pty->write(str, strlen(str));
}

#pragma mark - Focus Events

- (void)windowDidBecomeKey:(NSNotification*)notification {
    if (_impl->screen && _impl->screen->focusEvents() && _impl->pty) {
        const char* seq = "\033[I";
        _impl->pty->write(seq, 3);
    }
    // Trigger render on focus gain (e.g. cursor blink state)
    _impl->needsRender = true;
    [self markActivity];
}

- (void)windowDidResignKey:(NSNotification*)notification {
    if (_impl->screen && _impl->screen->focusEvents() && _impl->pty) {
        const char* seq = "\033[O";
        _impl->pty->write(seq, 3);
    }
    _impl->needsRender = true;
}

#pragma mark - Rendering

- (void)setNeedsRender {
    _impl->needsRender = true;
    [self markActivity];
}

- (void)renderFrame {
    // Simple render guard: always render when cursor visible (for blink) or content changed
    bool imeActive = _markedText != nil && _markedText.length > 0;
    bool cursorNeedsRedraw = !imeActive && _impl->screen && _impl->screen->cursorVisible();
    if (!_impl->needsRender && !cursorNeedsRedraw) return;
    _impl->needsRender = false;

    // Ensure drawable size is valid
    if (_metalLayer.drawableSize.width <= 0 || _metalLayer.drawableSize.height <= 0) {
        CGFloat drawScale = self.window.backingScaleFactor ?: 2.0;
        NSSize sz = self.bounds.size;
        if (sz.width > 0 && sz.height > 0) {
            float pxW = sz.width * drawScale;
            float pxH = sz.height * drawScale;
            _metalLayer.drawableSize = NSMakeSize(pxW, pxH);
            _impl->renderer->resize(pxW, pxH);
        }
    }

    // No mutex needed — PTY read and render both on main queue

    // Inject IME marked text into screen cells before render, restore after
    struct IMESavedCell { int row, col; termcore::TermCell cell; };
    std::vector<IMESavedCell> imeSaved;

    bool imeComposing = _markedText != nil && _markedText.length > 0;
    _impl->renderer->setIMEActive(imeComposing);

    if (imeComposing && _impl->screen) {
        int curRow = _impl->screen->cursorRow();
        int curCol = _impl->screen->cursorCol();
        int cols = _impl->screen->cols();
        int rows = _impl->screen->rows();
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

                // Save original cells (main + continuation for wide chars)
                for (int c = col; c < col + w; ++c) {
                    const termcore::TermCell& orig = _impl->screen->cellAt(curRow, c);
                    imeSaved.push_back({curRow, c, orig});
                }

                // Write main cell via safe mutable accessor
                termcore::TermCell& cell = _impl->screen->mutableCellAt(curRow, col);
                cell.codepoint = cp;
                cell.fg_color = _impl->screen->dynamicColors().background;
                cell.bg_color = _impl->screen->dynamicColors().foreground;
                cell.attributes = 0;
                cell.width = w;

                // Write continuation cell for wide chars
                if (w == 2 && col + 1 < cols) {
                    termcore::TermCell& cont = _impl->screen->mutableCellAt(curRow, col + 1);
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

    // Pass selection state to renderer
    {
        termcore::SelectionState sel;
        sel.active = _selecting;
        sel.block = _blockSelection;
        sel.start_row = (int)_selectionStart.y;
        sel.start_col = (int)_selectionStart.x;
        sel.end_row = (int)_selectionEnd.y;
        sel.end_col = (int)_selectionEnd.x;
        _impl->renderer->setSelection(sel);
    }

    _impl->renderer->render(*_impl->screen);

    // Clear dirty flags after successful render
    if (_impl->screen) {
        _impl->screen->clearDirty();
    }

    // Restore original cells
    for (const auto& sc : imeSaved) {
        termcore::TermCell& cell = _impl->screen->mutableCellAt(sc.row, sc.col);
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
        // Update stored theme string for adaptive theme switching
        if (!config.theme.empty()) {
            _impl->currentThemeString = config.theme;
        }

        // Update screen dynamic colors from the new config
        _impl->screen->initDynamicColors(config);

        // Re-apply transparency/background settings
        [self applyTransparencyConfig:config];
    }

    if (hasFlag(dirty, ConfigDirtyFlags::Keybindings)) {
        if (!config.keybindings.empty()) {
            std::vector<std::pair<std::string, std::string>> bindings;
            for (const auto& kb : config.keybindings) {
                bindings.emplace_back(kb.trigger, kb.action);
            }
            _impl->keybindings->loadFromConfig(bindings);
        }
    }

    if (hasFlag(dirty, ConfigDirtyFlags::Notification)) {
        _impl->notifyOnCommandFinish = config.notify_on_command_finish;
        _impl->screen->setNotifyAfterSeconds(config.notify_after_seconds);
    }

    [self setNeedsRender];
}

#pragma mark - Command finish notification

- (void)postCommandFinishNotification:(double)duration {
    UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];

    // Request permission if needed (no-op if already granted)
    [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                          completionHandler:^(BOOL granted, NSError* _Nullable error) {
        if (!granted) return;

        UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
        content.title = @"Command finished";
        content.body = [NSString stringWithFormat:@"Completed in %.1fs", duration];
        content.sound = [UNNotificationSound defaultSound];

        UNNotificationRequest* request =
            [UNNotificationRequest requestWithIdentifier:[[NSUUID UUID] UUIDString]
                                                 content:content
                                                 trigger:nil];
        [center addNotificationRequest:request withCompletionHandler:^(NSError* _Nullable err) {
            if (err) {
                NSLog(@"BreadTerminal: notification error: %@", err.localizedDescription);
            }
        }];
    }];
}

#pragma mark - Socket API accessors

- (termcore::Mux&)mux { return *_impl->mux; }
- (termcore::NotificationStore&)notifications { return *_impl->notifications; }
- (termcore::AgentTracker&)agentTracker { return *_impl->agentTracker; }
- (termcore::Pty*)pty { return _impl->pty.get(); }

#pragma mark - Config error display

- (void)showConfigError:(NSString*)message {
    // Remove any existing error banner before creating a new one
    static const NSInteger kConfigErrorBannerTag = 9001;
    for (NSView* subview in [self.subviews copy]) {
        if (subview.tag == kConfigErrorBannerTag) {
            [subview removeFromSuperview];
        }
    }

    // Create error banner at top of view
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

    // Auto-dismiss after 5 seconds
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC),
                   dispatch_get_main_queue(), ^{
        [errorLabel removeFromSuperview];
    });
}

@end
