#import "TerminalView.h"

#include "termcore/terminal_controller.h"
#include "termcore/screen.h"
#include "termcore/font/font_collection.h"
#include "termcore/font/font_shaper.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/glyph_cache.h"
#include "MetalTextRenderer.h"
#include "CoreTextRasterizer.h"
#include "CoreTextDiscovery.h"
#include "TerminalViewImpl.h"
#include "MacPlatformHost.h"
#include "termcore/theme_loader.h"

#import <UserNotifications/UserNotifications.h>
#include <nlohmann/json.hpp>
#include <dispatch/dispatch.h>
#include <mach/mach_time.h>
#include <memory>

@implementation TerminalView

#pragma mark - Initialisation

- (instancetype)initWithFrame:(NSRect)frame device:(id<MTLDevice>)device {
    self = [super initWithFrame:frame];
    if (!self) return nil;

    _device = device;
    _impl = new TerminalViewImpl();
    _markedText = nil;

    // Metal layer -- manually managed (NOT via makeBackingLayer)
    // We need direct control of drawableSize for Retina support
    self.wantsLayer = YES;  // Creates default backing layer
    _metalLayer = [CAMetalLayer layer];
    _metalLayer.device = _device;
    _metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    _metalLayer.framebufferOnly = YES;
    _metalLayer.contentsScale = 2.0;
    _metalLayer.frame = self.bounds;
    _metalLayer.drawableSize = NSMakeSize(self.bounds.size.width * 2,
                                           self.bounds.size.height * 2);
    [self.layer addSublayer:_metalLayer];

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

    // Calculate initial grid size (before controller creation)
    auto [rows, cols] = [self calculateGridSize];
    _termRows = rows;
    _termCols = cols;

    // Notifications, agent tracker (for socket API)
    _impl->notifications = std::make_unique<termcore::NotificationStore>();
    _impl->agentTracker = std::make_unique<termcore::AgentTracker>();

    _searchActive = NO;

    // Drawable size and viewport will be synced in first renderFrame
    _impl->needsRender = true;

    // Create PTY serial queue
    _impl->ptyQueue = dispatch_queue_create("com.breadterminal.pty", DISPATCH_QUEUE_SERIAL);

    // Initialize idle tracking
    _impl->lastActivityTime = mach_absolute_time();
    _impl->idleMode = false;

    // Controller and PlatformHost are created in applyConfig / startShell
    // after the view is added to a window (we need a window reference).

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
        CGFloat scale = self.window.backingScaleFactor;
        _metalLayer.contentsScale = scale;
        // Force correct drawable size for Retina
        NSSize sz = self.bounds.size;
        if (sz.width > 0 && sz.height > 0) {
            _metalLayer.drawableSize = NSMakeSize(sz.width * scale, sz.height * scale);
            _impl->renderer->resize(sz.width * scale, sz.height * scale);
        }
        [self updateGridSize];
        _impl->needsRender = true;

        // Update platform host's window reference if controller exists
        if (_impl->platformHost) {
            _impl->platformHost->setWindow(self.window);
        }
    }
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
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

    // Store the raw theme string for adaptive theme switching
    if (!config.theme.empty()) {
        _impl->currentThemeString = config.theme;
    }

    // Font ligatures
    _impl->renderer->setFontLigatures(config.font_ligatures);

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

    // Cursor blink interval
    _impl->renderer->setCursorBlinkInterval(config.cursor_blink_interval);

    // Create TerminalController if not yet created
    if (!_impl->controller) {
        _impl->platformHost = std::make_unique<MacPlatformHost>(
            self, self.window);
        _impl->controller = std::make_unique<termcore::TerminalController>(
            _impl->platformHost.get(),
            config,
            _impl->fontCollection.get());
    } else {
        // Update controller with new config
        _impl->controller->onConfigChanged(config);
    }

    // Recalculate grid with potentially new font metrics
    [self updateGridSize];
}

- (void)applyThemeByName:(const std::string&)themeName {
    auto theme = termcore::findTheme(themeName);
    if (!theme) return;

    // Build a temporary config with the theme colors applied
    termcore::Config tempConfig;
    termcore::applyTheme(tempConfig, *theme);

    // Update the screen's dynamic colors via controller
    termcore::Screen* scr = _impl->controller ? _impl->controller->activeScreen() : nullptr;
    if (scr) {
        scr->initDynamicColors(tempConfig);
    }

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
    const auto& material = config.background_blur_material;
    bool wantBlur = (material != "none");
    bool needsTransparency = (opacity < 1.0f) || wantBlur;

    if (needsTransparency) {
        _metalLayer.framebufferOnly = NO;
        _metalLayer.opaque = NO;
    } else {
        _metalLayer.framebufferOnly = YES;
        _metalLayer.opaque = YES;
    }

    _impl->renderer->setBackgroundOpacity(opacity);

    if (wantBlur) {
        if (!_visualEffectView) {
            _visualEffectView = [[NSVisualEffectView alloc] initWithFrame:self.bounds];
            _visualEffectView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
            _visualEffectView.blendingMode = NSVisualEffectBlendingModeBehindWindow;
            _visualEffectView.state = NSVisualEffectStateActive;
            [self addSubview:_visualEffectView positioned:NSWindowBelow relativeTo:nil];
        }

        if (material == "hud_window") {
            _visualEffectView.material = NSVisualEffectMaterialHUDWindow;
        } else if (material == "sheet") {
            _visualEffectView.material = NSVisualEffectMaterialSheet;
        } else {
            _visualEffectView.material = NSVisualEffectMaterialUnderWindowBackground;
        }
    } else {
        if (_visualEffectView) {
            [_visualEffectView removeFromSuperview];
            _visualEffectView = nil;
        }
    }
}

#pragma mark - NSView overrides

- (void)layout {
    [super layout];
    _metalLayer.frame = self.bounds;
}

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)becomeFirstResponder  { return YES; }

- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    CGFloat scale = self.window.backingScaleFactor > 0 ? self.window.backingScaleFactor : 2.0;
    _metalLayer.frame = self.bounds;
    _metalLayer.drawableSize = NSMakeSize(newSize.width * scale, newSize.height * scale);
    _impl->renderer->resize(newSize.width * scale, newSize.height * scale);
    [self updateGridSize];
    _impl->needsRender = true;
}

- (void)viewDidEndLiveResize {
    [super viewDidEndLiveResize];
    [self updateGridSize];
}

#pragma mark - Grid helpers

- (std::pair<int,int>)calculateGridSize {
    float cw = _cellWidth  > 0 ? _cellWidth  : 16;
    float ch = _cellHeight > 0 ? _cellHeight : 32;
    float gridScale = 2.0f;
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

    // Delegate resize to controller (it handles screen + pty resize)
    if (_impl->controller) {
        float gridScale = self.window.backingScaleFactor > 0
                            ? (float)self.window.backingScaleFactor : 2.0f;
        float w = self.bounds.size.width * gridScale;
        float h = self.bounds.size.height * gridScale;
        _impl->controller->onResize(static_cast<int>(w), static_cast<int>(h));
        _impl->renderer->resize(w, h);
    }
    _impl->needsRender = true;
}

#pragma mark - Shell / PTY

- (void)startShell {
    // Ensure controller is created
    if (!_impl->controller) {
        NSLog(@"BreadTerminal: controller not created before startShell - call applyConfig first");
        return;
    }

    // Initialize terminal (creates Mux, TabController, spawns PTY via platform host)
    _impl->controller->initTerminal();

    // Wire agent auto-detection to provider registry for install notifications
    _impl->agentTracker->setProviderRegistry(&_impl->controller->providerRegistry());
    _impl->agentTracker->setNotificationStore(_impl->notifications.get());

    // Wire response callback so Screen can write back to PTY (DA, DSR, DECRPM, etc.)
    __weak TerminalView* weakSelf = self;
    termcore::Screen* scr = _impl->controller->activeScreen();
    if (scr) {
        scr->setResponseCallback([weakSelf](const std::string& response) {
            dispatch_async(dispatch_get_main_queue(), ^{
                TerminalView* strongSelf = weakSelf;
                if (!strongSelf || !strongSelf->_impl->controller) return;
                termcore::Pty* pty = strongSelf->_impl->controller->tabs()->activePty();
                if (pty) pty->write(response.data(), response.size());
            });
        });

        // Wire OSC 7770 callback: route in-band events to notification/agent system
        auto* notifStore = _impl->notifications.get();
        auto* tracker = _impl->agentTracker.get();
        scr->setOscHookCallback([notifStore, tracker](const std::string& json_str) {
            auto j = nlohmann::json::parse(json_str, nullptr, false);
            if (j.is_discarded() || !j.is_object()) return;
            auto eventType = j.value("event", "");
            if (eventType == "Notification") {
                auto title = j.value("title", "");
                auto body = j.value("body", "");
                auto urgency_str = j.value("urgency", "normal");
                auto urgency = (urgency_str == "critical") ? termcore::NotificationUrgency::Critical
                             : (urgency_str == "low")      ? termcore::NotificationUrgency::Low
                                                            : termcore::NotificationUrgency::Normal;
                notifStore->add(0, termcore::NotificationSource::Agent, urgency, title, body);
            } else if (eventType == "StateChange") {
                auto state_str = j.value("state", "");
                auto state = termcore::AgentTracker::stringToState(state_str);
                auto* info = tracker->getAgent(0);
                if (info) {
                    tracker->reportState(0, info->type, state);
                }
            }
        });
    }

    // Register for focus event notifications (CSI I / CSI O)
    [[NSNotificationCenter defaultCenter] addObserver:self
        selector:@selector(windowDidBecomeKey:)
        name:NSWindowDidBecomeKeyNotification object:self.window];
    [[NSNotificationCenter defaultCenter] addObserver:self
        selector:@selector(windowDidResignKey:)
        name:NSWindowDidResignKeyNotification object:self.window];

    // Dispatch PTY reads on main queue via dispatch source
    termcore::Pty* pty = _impl->controller->tabs()->activePty();
    if (pty) {
        int fd = pty->fd();
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
}

- (void)readPtyDataOnMainQueue {
    if (!_impl->controller) return;

    // Let controller poll all PTYs (reads data and feeds parsers)
    _impl->controller->pollPty();

    if (_impl->controller->needsRender()) {
        _impl->needsRender = true;

        // Update window title from active screen
        termcore::Screen* scr = _impl->controller->activeScreen();
        if (scr) {
            std::string title = scr->title();
            if (!title.empty()) {
                NSString* t = [NSString stringWithUTF8String:title.c_str()];
                if (t && ![self.window.title isEqualToString:t])
                    self.window.title = t;
            }
        }
    }
}

#pragma mark - Text I/O

- (void)sendText:(NSString*)text {
    if (!_impl->controller) return;
    const char* utf8 = [text UTF8String];
    std::string str(utf8, [text lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
    _impl->controller->onCharInput(str);
}

@end
