#ifndef BREAD_TERMINAL_WINDOW_STATE_H
#define BREAD_TERMINAL_WINDOW_STATE_H

#if defined(_WIN32)

#include "D3DTextRenderer.h"
#include "RenderSnapshot.h"

#include "termcore/terminal_controller.h"
#include "termcore/platform_host.h"
#include "termcore/font/font_collection.h"
#include "termcore/font/font_shaper.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/glyph_cache.h"
#include "termcore/font/i_font_rasterizer.h"
#include "termcore/font/i_font_discovery.h"
#include "termcore/config.h"
#include "termcore/accessibility.h"
#include "termcore/notification.h"
#include "termcore/agent.h"
#include "UnifiedSettingsWindow.h"
#include "ClipboardHistoryPopup.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <wrl/client.h>

#include <atomic>
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

using Microsoft::WRL::ComPtr;

// Forward declaration for accessibility
class TerminalAccessibilityProvider;

/// Lock-free input event for deferred processing.
struct InputEvent {
    enum Type : uint8_t { KeyDown, Char };
    Type type;
    WPARAM wParam;
    uint8_t mods;       // captured at enqueue time (modifier keys)
};

/// Lock-free SPSC ring buffer for input events.
/// Single producer (WndProc / main message dispatch) and
/// single consumer (main thread, inside withWriteLock during pollPty).
struct InputRingBuffer {
    static constexpr size_t kCapacity = 256;  // must be power of 2

    bool push(const InputEvent& ev) {
        size_t w = write_.load(std::memory_order_relaxed);
        size_t next = (w + 1) & (kCapacity - 1);
        if (next == read_.load(std::memory_order_acquire)) return false; // full
        buf_[w] = ev;
        write_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(InputEvent& ev) {
        size_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false; // empty
        ev = buf_[r];
        read_.store((r + 1) & (kCapacity - 1), std::memory_order_release);
        return true;
    }

    bool empty() const {
        return read_.load(std::memory_order_acquire) == write_.load(std::memory_order_acquire);
    }

private:
    std::array<InputEvent, kCapacity> buf_{};
    std::atomic<size_t> write_{0};
    std::atomic<size_t> read_{0};
};

/// Terminal window state, stored as GWLP_USERDATA on the HWND.
/// Implements IPlatformHost to bridge TerminalController with Win32.
struct TerminalWindowState : public termcore::IPlatformHost {
    HWND hwnd = nullptr;

    // D3D11 device
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> deviceContext;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID3D11RenderTargetView> rtv;

    // DirectComposition for per-pixel alpha transparency
    ComPtr<IDCompositionDevice> dcompDevice;
    ComPtr<IDCompositionTarget> dcompTarget;
    ComPtr<IDCompositionVisual> dcompVisual;
    bool useComposition = false;  // true when opacity < 1 or blur > 0

    // Core controller — owns all terminal state
    std::unique_ptr<termcore::TerminalController> controller;

    // Font rasterization stack (platform-owned, shared with controller)
    std::unique_ptr<termcore::IFontRasterizer> rasterizer;
    std::unique_ptr<termcore::IFontDiscovery> discovery;
    std::unique_ptr<termcore::FontShaper> shaper;
    std::unique_ptr<termcore::FontCollection> fontCollection;
    std::unique_ptr<termcore::GlyphAtlas> atlas;
    std::unique_ptr<termcore::GlyphCache> cache;

    // Renderer
    std::unique_ptr<termcore::D3DTextRenderer> renderer;

    // Notifications, agent tracking
    std::unique_ptr<termcore::NotificationStore> notifications;
    std::unique_ptr<termcore::AgentTracker> agentTracker;

    // UI windows
    std::unique_ptr<termcore::UnifiedSettingsWindow> unifiedSettings;
    std::unique_ptr<ClipboardHistoryPopup> clipboardHistoryPopup;

    // Accessibility
    TerminalAccessibilityProvider* accessibilityProvider = nullptr;

    // DPI
    float dpiScale_ = 1.0f;

    // --- Render thread synchronization (Phase 2) ---
    SRWLOCK renderLock_ = SRWLOCK_INIT;
    HANDLE invalidateEvent_ = nullptr;   // auto-reset event, created in initRenderThread()
    std::thread renderThread_;
    std::atomic<bool> renderRunning_{false};

    // Resize coordination: render thread sets this when paused
    HANDLE renderPausedEvent_ = nullptr; // manual-reset event

    void initRenderThread();
    void stopRenderThread();
    void renderThreadFunc();
    void signalInvalidate();

    /// Execute fn under exclusive SRWLOCK, then signal invalidation.
    template<typename Fn>
    void withWriteLock(Fn&& fn) {
        AcquireSRWLockExclusive(&renderLock_);
        fn();
        ReleaseSRWLockExclusive(&renderLock_);
        signalInvalidate();
    }

    // State
    bool needsRender = false;
    bool inLiveResize = false;
    bool cursorBlinkOn = true;

    // Fullscreen state
    bool isFullscreen = false;
    WINDOWPLACEMENT savedPlacement = {};
    LONG savedStyle = 0;
    LONG savedExStyle = 0;

    // Resize overlay
    bool showResizeOverlay = false;
    std::chrono::steady_clock::time_point resizeOverlayStart;
    int resizeOverlayCols = 0;
    int resizeOverlayRows = 0;

    // Accessibility
    termcore::AccessibilityPreferences accessibility;
    std::string themeBeforeHighContrast;  // user's theme to restore when HC is disabled

    // Search bar UI
    bool searchActive = false;
    HWND searchEditHwnd = nullptr;
    static constexpr int kSearchEditId = 100;

    // Notification tray icon state
    bool notifyIconAdded = false;

    // IME composition (preedit) text for inline rendering
    std::wstring imeCompositionText;

    // --- Message loop state (event-driven wait + output coalescing) ---
    int consecutiveBurstPolls = 0;       // consecutive polls that returned data
    bool lastPollHadData = false;         // whether the most recent poll had data
    std::chrono::steady_clock::time_point lastDataTime;  // last time PTY data arrived
    std::chrono::steady_clock::time_point lastRenderTime; // last render timestamp

    // --- D3D / terminal lifecycle ---
    bool initD3D(HWND hWnd);
    void createRenderTarget();
    void destroyRenderTarget();
    void resizeSwapChain(int width, int height);
    void initTerminal();
    void pollPty();
    void renderFrame();
    void renderFrame(const RenderSnapshot& snap);
    void pushRendererState(const RenderSnapshot& snap);

    /// Capture all state needed for rendering into a RenderSnapshot.
    /// In Phase 2 this will be called under SRWLOCK shared lock.
    RenderSnapshot captureRenderSnapshot();

    // --- Fullscreen ---
    void toggleFullscreen() override;

    // --- DWM title bar / blur / opacity ---
    void applyTitleBarTheme(HWND hwnd);
    void applyBackgroundBlur(HWND hwnd);
    void applyOpacity(HWND hwnd);

    // --- DPI ---
    void handleDpiChange(HWND hwnd, UINT dpi, const RECT* newRect);

    // --- Input ---
    InputRingBuffer inputQueue_;              // lock-free SPSC input queue
    void enqueueKeyDown(WPARAM wParam);       // push to queue (no lock)
    void enqueueChar(WPARAM wParam);          // push to queue (no lock)
    void drainInputQueue();                   // process queued events (under write lock)
    void handleKeyDown(WPARAM wParam, LPARAM lParam);
    void handleChar(WPARAM wParam);

    // --- Mouse ---
    void handleMouseDown(int x, int y);
    void handleMouseMove(int x, int y);
    void handleMouseUp(int x, int y);
    void handleDoubleClick(int x, int y);
    void handleMouseWheel(int delta, int x, int y);

    // --- Accessibility ---
    void checkAccessibilitySettings();

    // --- Search bar UI ---
    void repositionSearchBar();

    // --- Tab bar click handling ---
    bool handleTabBarClick(int x, int y);
    void handleTabBarHover(int x, int y);
    bool handleTabBarDrag(int x, int y);
    void handleTabBarDragEnd(int x, int y);

    // Tab drag state
    bool tabDragging = false;
    int tabDragSourceIndex = -1;
    int tabDragStartX = 0;

    // --- Sidebar ---
    bool handleSidebarClick(int x, int y);
    void handleSidebarHover(int x, int y);
    void handleSidebarWheel(int delta);

    // --- IPlatformHost interface ---
    void invalidate() override;
    void getViewportSize(int& w, int& h) override;
    std::string getClipboardText() override;
    void setClipboardText(const std::string& text) override;
    void setWindowTitle(const std::string& title) override;
    // toggleFullscreen() declared above
    void closeWindow() override;
    void showConfirmDialog(const std::string& msg,
                           std::function<void(bool)> cb) override;
    void showSearchBar() override;
    void hideSearchBar() override;
    void updateSearchResults(int current, int total) override;
    void setSearchBarText(const std::string& text) override;
    void positionIME(int x, int y, int height) override;
    void onFontChanged(float cellW, float cellH) override;
    void onColorsChanged() override;
    void onGridSizeChanged(int rows, int cols) override;
    void showNotification(const std::string& title,
                          const std::string& body) override;
    void showClipboardHistory(const std::vector<termcore::ClipboardEntry>& entries) override;
    void openSettingsWindow(const termcore::Config& config) override;
    float dpiScale() override;
    void openUrl(const std::string& url) override;
    void setMouseCursor(CursorType cursor) override;
    std::unique_ptr<termcore::Pty> createPty(const termcore::Profile& profile,
                                              int rows, int cols) override;

private:
    // Helper: update tab bar on renderer from controller state
    void updateTabBar();
    // Helper: update selection on renderer from controller state
    void updateRendererSelection();
    // Helper: update command palette on renderer from controller state
    void updateCommandPalette();
    // Helper: update profile dropdown on renderer from controller state
    void updateProfileDropdown();
    // Helper: update sidebar on renderer from controller/agent state
    void updateSidebar();

    // Notification ring animation state per pane
    struct PaneRingState {
        float intensity = 0.0f;
        uint32_t color = 0x007acc;
        std::chrono::steady_clock::time_point triggered;
    };
    std::unordered_map<uint32_t, PaneRingState> pane_ring_states_;
    std::chrono::steady_clock::time_point last_frame_time_;
};

#endif // _WIN32
#endif // BREAD_TERMINAL_WINDOW_STATE_H
