#ifndef BREAD_TERMINAL_WINDOW_STATE_H
#define BREAD_TERMINAL_WINDOW_STATE_H

#if defined(_WIN32)

#include "D3DTextRenderer.h"

#include "termcore/terminal_controller.h"
#include "termcore/platform_host.h"
#include "termcore/font/font_collection.h"
#include "termcore/font/font_shaper.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/glyph_cache.h"
#include "termcore/font/i_font_rasterizer.h"
#include "termcore/font/i_font_discovery.h"
#include "termcore/config.h"
#include "termcore/notification.h"
#include "termcore/agent.h"
#include "ThemeHubWindow.h"
#include "FontHubWindow.h"
#include "SettingsWindow.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <chrono>
#include <memory>
#include <string>

using Microsoft::WRL::ComPtr;

/// Terminal window state, stored as GWLP_USERDATA on the HWND.
/// Implements IPlatformHost to bridge TerminalController with Win32.
struct TerminalWindowState : public termcore::IPlatformHost {
    HWND hwnd = nullptr;

    // D3D11 device
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> deviceContext;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID3D11RenderTargetView> rtv;

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
    std::unique_ptr<termcore::ThemeHubWindow> themeHub;
    std::unique_ptr<termcore::FontHubWindow> fontHub;
    std::unique_ptr<termcore::SettingsWindow> settingsWin;

    // DPI
    float dpiScale_ = 1.0f;

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

    // Search bar UI
    bool searchActive = false;
    HWND searchEditHwnd = nullptr;
    static constexpr int kSearchEditId = 100;

    // --- D3D / terminal lifecycle ---
    bool initD3D(HWND hWnd);
    void createRenderTarget();
    void destroyRenderTarget();
    void resizeSwapChain(int width, int height);
    void initTerminal();
    void pollPty();
    void renderFrame();

    // --- Fullscreen ---
    void toggleFullscreen() override;

    // --- DWM title bar / blur ---
    void applyTitleBarTheme(HWND hwnd);
    void applyBackgroundBlur(HWND hwnd);

    // --- DPI ---
    void handleDpiChange(HWND hwnd, UINT dpi, const RECT* newRect);

    // --- Input ---
    void handleKeyDown(WPARAM wParam, LPARAM lParam);
    void handleChar(WPARAM wParam);

    // --- Mouse ---
    void handleMouseDown(int x, int y);
    void handleMouseMove(int x, int y);
    void handleMouseUp(int x, int y);
    void handleDoubleClick(int x, int y);
    void handleMouseWheel(int delta, int x, int y);

    // --- Search bar UI ---
    void repositionSearchBar();

    // --- Tab bar click handling ---
    bool handleTabBarClick(int x, int y);
    void handleTabBarHover(int x, int y);

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
    void positionIME(int x, int y, int height) override;
    void onFontChanged(float cellW, float cellH) override;
    void onColorsChanged() override;
    void onGridSizeChanged(int rows, int cols) override;
    void showNotification(const std::string& title,
                          const std::string& body) override;
    void openSettingsWindow(const termcore::Config& config) override;
    void openThemeHub(const termcore::Config& config) override;
    void openFontHub(const termcore::Config& config) override;
    float dpiScale() override;
    void openUrl(const std::string& url) override;
    void setMouseCursor(CursorType cursor) override;
    std::unique_ptr<termcore::Pty> createPty(const std::string& shell,
                                              int rows, int cols) override;

private:
    // Helper: update tab bar on renderer from controller state
    void updateTabBar();
    // Helper: update selection on renderer from controller state
    void updateRendererSelection();
};

#endif // _WIN32
#endif // BREAD_TERMINAL_WINDOW_STATE_H
