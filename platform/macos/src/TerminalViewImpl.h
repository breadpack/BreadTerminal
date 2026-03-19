#ifndef BREADTERMINAL_TERMINAL_VIEW_IMPL_H
#define BREADTERMINAL_TERMINAL_VIEW_IMPL_H

#import "TerminalView.h"

#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include "termcore/pty.h"
#include "termcore/keybinding.h"
#include "termcore/search.h"
#include "termcore/url_detector.h"
#include "termcore/paste_guard.h"
#include "termcore/mouse.h"
#include "termcore/mux.h"
#include "termcore/notification.h"
#include "termcore/agent.h"
#include "termcore/font/font_collection.h"
#include "termcore/font/font_shaper.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/glyph_cache.h"
#include "MetalTextRenderer.h"

#include <dispatch/dispatch.h>
#include <memory>
#include <mutex>
#include <CoreVideo/CVDisplayLink.h>
#import <QuartzCore/CADisplayLink.h>

/// Private C++ implementation details for TerminalView.
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
    std::unique_ptr<termcore::KeybindingManager> keybindings;
    std::unique_ptr<termcore::TerminalSearch> search;
    std::unique_ptr<termcore::UrlDetector> urlDetector;
    std::unique_ptr<termcore::PasteGuard> pasteGuard;
    std::unique_ptr<termcore::Mux> mux;
    std::unique_ptr<termcore::NotificationStore> notifications;
    std::unique_ptr<termcore::AgentTracker> agentTracker;
    __strong dispatch_source_t ptyReadSource = nullptr;

    // --- Display link / timer ---
    CADisplayLink* displayLink API_AVAILABLE(macos(14.0)) = nil;
    CVDisplayLinkRef cvDisplayLink = nullptr;  // fallback for < macOS 14
    bool useCADisplayLink = false;
    // renderTimer removed — replaced by CADisplayLink / CVDisplayLink

    // --- PTY serial queue + mutex ---
    dispatch_queue_t ptyQueue = nullptr;
    std::mutex screenMutex;

    // --- Idle downclock ---
    uint64_t lastActivityTime = 0;  // mach_absolute_time
    bool idleMode = false;
    NSTimer* idleTimer = nil;  // fires at reduced rate when idle

    bool needsRender = false;
    bool notifyOnCommandFinish = true;
    int windowPadding = 0;  // logical pixels, stored for grid calculation
    std::string currentThemeString;  // Stores the raw theme config value (may be adaptive)

    // Copy mode state (vi-style keyboard navigation)
    bool copyModeActive = false;
    int copyModeCursorRow = 0;    // Visible row (can be negative for scrollback)
    int copyModeCursorCol = 0;
    bool copyModeSelecting = false;
    bool copyModeLineSelect = false;
    int copyModeSelectStartRow = 0;
    int copyModeSelectStartCol = 0;
    bool copyModeSearchMode = false;  // '/' search input active
    bool copyModeWaitingG = false;    // Waiting for second 'g' in 'gg'

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
    NSPoint _selectionStart;
    NSPoint _selectionEnd;
    BOOL _selecting;
    BOOL _blockSelection;   // Alt+drag rectangular selection
    int _scrollOffset;
    BOOL _searchActive;
    NSTextField* _searchField;
    NSTrackingArea* _trackingArea;
    NSVisualEffectView* _visualEffectView;
    // IME composition state
    NSString* _markedText;
    NSRange _markedSelectedRange;
    // Copy mode indicator label
    NSTextField* _copyModeLabel;
    // Note: _termRows and _termCols are synthesized properties on TerminalView.
}

/// Internal helper: write raw bytes to the PTY.
- (void)writePty:(const char*)str;

/// Render a single frame (called by display link or idle timer).
- (void)renderFrame;

/// Accessors for socket API integration.
- (termcore::Mux&)mux;
- (termcore::NotificationStore&)notifications;
- (termcore::AgentTracker&)agentTracker;
- (termcore::Pty*)pty;

/// Post a macOS desktop notification for command completion.
- (void)postCommandFinishNotification:(double)duration;

@end

#endif // BREADTERMINAL_TERMINAL_VIEW_IMPL_H
