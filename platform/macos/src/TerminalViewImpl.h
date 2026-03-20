#ifndef BREADTERMINAL_TERMINAL_VIEW_IMPL_H
#define BREADTERMINAL_TERMINAL_VIEW_IMPL_H

#import "TerminalView.h"

#include "termcore/terminal_controller.h"
#include "termcore/screen.h"
#include "termcore/pty.h"
#include "termcore/notification.h"
#include "termcore/agent.h"
#include "termcore/font/font_collection.h"
#include "termcore/font/font_shaper.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/glyph_cache.h"
#include "MetalTextRenderer.h"
#include "MacPlatformHost.h"

#include <dispatch/dispatch.h>
#include <memory>
#include <mutex>
#include <CoreVideo/CVDisplayLink.h>
#import <QuartzCore/CADisplayLink.h>

/// Private C++ implementation details for TerminalView.
struct TerminalViewImpl {
    // Core controller — owns all terminal state (screen, pty, keybindings, search, etc.)
    std::unique_ptr<termcore::TerminalController> controller;

    // Platform host — bridges controller callbacks to Cocoa APIs
    std::unique_ptr<MacPlatformHost> platformHost;

    // Font rasterization stack (platform-owned, shared with controller)
    std::unique_ptr<termcore::IFontRasterizer> rasterizer;
    std::unique_ptr<termcore::IFontDiscovery> discovery;
    std::unique_ptr<termcore::FontShaper> shaper;
    std::unique_ptr<termcore::FontCollection> fontCollection;
    std::unique_ptr<termcore::GlyphAtlas> atlas;
    std::unique_ptr<termcore::GlyphCache> cache;

    // Metal renderer
    std::unique_ptr<termcore::MetalTextRenderer> renderer;

    // Socket API support (still owned by platform)
    std::unique_ptr<termcore::NotificationStore> notifications;
    std::unique_ptr<termcore::AgentTracker> agentTracker;

    __strong dispatch_source_t ptyReadSource = nullptr;

    // --- Display link / timer ---
    CADisplayLink* displayLink API_AVAILABLE(macos(14.0)) = nil;
    CVDisplayLinkRef cvDisplayLink = nullptr;  // fallback for < macOS 14
    bool useCADisplayLink = false;

    // --- PTY serial queue + mutex ---
    dispatch_queue_t ptyQueue = nullptr;
    std::mutex screenMutex;

    // --- Idle downclock ---
    uint64_t lastActivityTime = 0;  // mach_absolute_time
    bool idleMode = false;
    NSTimer* idleTimer = nil;  // fires at reduced rate when idle

    bool needsRender = true;  // Start with initial render needed
    int windowPadding = 0;  // logical pixels, stored for grid calculation
    std::string currentThemeString;  // Stores the raw theme config value (may be adaptive)

    ~TerminalViewImpl() {
        if (ptyReadSource) {
            dispatch_source_cancel(ptyReadSource);
            ptyReadSource = nullptr;
        }

        // Stop display link
        if (@available(macOS 14.0, *)) {
            if (useCADisplayLink && displayLink) {
                [displayLink invalidate];
                displayLink = nil;
            }
        }
        if (cvDisplayLink) {
            CVDisplayLinkStop(cvDisplayLink);
            CVDisplayLinkRelease(cvDisplayLink);
            cvDisplayLink = nullptr;
        }

        [idleTimer invalidate];
        idleTimer = nil;
    }
};

/// Expose ivars so that the category in TerminalViewInput.mm can access them.
@interface TerminalView () {
@public
    TerminalViewImpl* _impl;
    id<MTLDevice> _device;
    CAMetalLayer* _metalLayer;
    float _cellWidth;
    float _cellHeight;
    NSTrackingArea* _trackingArea;
    NSVisualEffectView* _visualEffectView;
    // IME composition state
    NSString* _markedText;
    NSRange _markedSelectedRange;
    // Search UI
    BOOL _searchActive;
    NSTextField* _searchField;
    // Note: _termRows and _termCols are synthesized properties on TerminalView.
}

/// Render a single frame (called by display link or idle timer).
- (void)renderFrame;

/// Callback from MacPlatformHost when cell size changes.
- (void)onCellSizeChanged:(float)cellW height:(float)cellH;

/// Accessors for socket API integration.
- (termcore::NotificationStore&)notifications;
- (termcore::AgentTracker&)agentTracker;

@end

#endif // BREADTERMINAL_TERMINAL_VIEW_IMPL_H
