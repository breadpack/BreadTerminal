#ifndef BREAD_QUICK_TERMINAL_WINDOW_H
#define BREAD_QUICK_TERMINAL_WINDOW_H

#if defined(_WIN32)

#include "TerminalWindowState.h"
#include "termcore/quick_terminal.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <memory>

/// Manages a dropdown/visor quick terminal window on Windows.
/// Creates a borderless popup HWND with system-wide hotkey registration,
/// smooth slide animation, and auto-hide on focus loss.
class QuickTerminalWindow {
public:
    QuickTerminalWindow();
    ~QuickTerminalWindow();

    /// Initialize the quick terminal with the given config.
    /// Returns false if the hotkey could not be registered.
    bool init(HINSTANCE hInstance, const termcore::QuickTerminalConfig& config);

    /// Run the message loop (blocking). Returns exit code.
    int run();

    /// Toggle visibility (called from hotkey handler).
    void toggle();

    /// Returns true if the window is currently visible (or animating to visible).
    bool isVisible() const { return visible_; }

    /// Returns the HWND (may be null before init).
    HWND hwnd() const { return hwnd_; }

private:
    // Window procedure (static, routes to instance via GWLP_USERDATA)
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg,
                                     WPARAM wParam, LPARAM lParam);

    // Animation
    void startShowAnimation();
    void startHideAnimation();
    void onAnimationTick();
    void updateWindowPosition(float progress);

    // Compute target rect on the monitor where the mouse cursor is
    RECT computeTargetRect() const;

    // Hotkey
    bool registerGlobalHotkey();
    void unregisterGlobalHotkey();

    // Ensure terminal is initialized (lazy init on first show)
    void ensureTerminalInit();

    HINSTANCE hInstance_ = nullptr;
    HWND hwnd_ = nullptr;
    termcore::QuickTerminalConfig config_;

    // Terminal state (owns D3D, controller, etc.)
    std::unique_ptr<TerminalWindowState> state_;
    bool terminalInitialized_ = false;

    // Visibility / animation state
    bool visible_ = false;
    bool animating_ = false;
    float animProgress_ = 0.0f;  // 0.0 = hidden, 1.0 = fully visible
    bool animShowing_ = false;   // true = animating to show, false = to hide

    // Target rectangle (screen coords) for the current monitor
    RECT targetRect_ = {};

    // Hotkey ID
    static constexpr int kHotkeyId = 0xBEAD;

    // Timer IDs
    static constexpr UINT_PTR kAnimTimerId = 100;
    static constexpr UINT kAnimIntervalMs = 10;    // ~100 fps animation
    static constexpr UINT_PTR kRenderTimerId = 101;
    static constexpr UINT kRenderIntervalMs = 16;
    static constexpr UINT_PTR kCursorBlinkTimerId = 102;
    static constexpr UINT kCursorBlinkIntervalMs = 500;
};

#endif // _WIN32
#endif // BREAD_QUICK_TERMINAL_WINDOW_H
