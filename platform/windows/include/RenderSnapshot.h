#ifndef BREAD_RENDER_SNAPSHOT_H
#define BREAD_RENDER_SNAPSHOT_H

#if defined(_WIN32)

#include "D3DTextRenderer.h"
#include "ScreenSnapshot.h"
#include "termcore/screen.h"

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

/// IME composition overlay data passed to the renderer without mutating Screen cells.
struct ImeOverlay {
    std::wstring text;       // composition (preedit) text
    int row = -1;            // cursor row where IME text starts
    int col = -1;            // cursor col where IME text starts
    uint32_t fg_color = 0;   // foreground for IME cells (typically background color for inverse)
    uint32_t bg_color = 0;   // background for IME cells (typically foreground color for inverse)
};

/// Lightweight snapshot of all state that renderFrame() reads.
/// Phase 1: Captures pointers and copied scalar values. In Phase 2 the
/// pointer reads will be protected by SRWLOCK and deep copies will replace
/// them where necessary.
struct RenderSnapshot {
    // --- Screen (pointer, will be lock-protected in Phase 2) ---
    termcore::Screen* screen = nullptr;

    // --- IME overlay (replaces mutable cell hack) ---
    ImeOverlay ime;

    // --- Cursor ---
    bool cursorBlinkOn = true;

    // --- UI overlay state (cheap copies) ---
    bool showResizeOverlay = false;
    int resizeOverlayCols = 0;
    int resizeOverlayRows = 0;

    // --- Sync interval ---
    bool inLiveResize = false;

    // --- Renderer-owned state (set via setters before render) ---
    // These fields are gathered from controller and pushed to the renderer
    // via set*() calls. They document the full render dependency surface.
    //
    // Set via renderer->setTabBar():       tab bar info
    // Set via renderer->setSidebar():      sidebar info
    // Set via renderer->setSelection():    selection range
    // Set via renderer->setCommandPalette(): command palette state
    // Set via renderer->setProfileDropdown(): profile dropdown state
    // Set via renderer->setSearchHighlights(): search match highlights
    // Set via renderer->setUrlHighlights():  URL highlights
    // Set via renderer->setBackgroundOpacity(): background opacity
    // Set via renderer->setFontLigatures():  ligature toggle
    // Set via renderer->setGhostText():     autocomplete ghost text
    // Set via renderer->setIMEActive():     whether IME is composing
};

#endif // _WIN32
#endif // BREAD_RENDER_SNAPSHOT_H
