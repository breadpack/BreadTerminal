#ifndef BREAD_TERMINAL_WINDOW_STATE_H
#define BREAD_TERMINAL_WINDOW_STATE_H

#if defined(_WIN32)

#include "D3DTextRenderer.h"

#include "termcore/screen.h"
#include "termcore/mouse.h"
#include "termcore/vt_parser.h"
#include "termcore/pty.h"
#include "termcore/font/font_collection.h"
#include "termcore/font/font_shaper.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/glyph_cache.h"
#include "termcore/font/i_font_rasterizer.h"
#include "termcore/font/i_font_discovery.h"
#include "termcore/config.h"
#include "termcore/search.h"
#include "termcore/keybinding.h"
#include "termcore/url_detector.h"
#include "termcore/mux.h"
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
#include <unordered_map>

using Microsoft::WRL::ComPtr;
using namespace termcore;

/// Per-pane terminal state (PTY + Screen + Parser).
struct PaneState {
    PaneId id = kInvalidPane;
    std::unique_ptr<Screen> screen;
    std::unique_ptr<VtParser> parser;
    std::unique_ptr<Pty> pty;
};

/// Terminal window state, stored as GWLP_USERDATA on the HWND.
struct TerminalWindowState {
    HWND hwnd = nullptr;

    // D3D11 device
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> deviceContext;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID3D11RenderTargetView> rtv;

    // Per-pane terminal state (owned by panes map)
    std::unordered_map<PaneId, std::unique_ptr<PaneState>> panes;
    PaneId nextPaneId = 1;

    // Active pane raw pointers — updated by syncActivePointers()
    // All existing code uses these; they always point to the active pane.
    Screen* screen = nullptr;
    Pty* pty = nullptr;

    // Mux workspace/tab IDs
    WorkspaceId wsId = kInvalidWorkspace;

    // --- Pane management ---
    PaneState* activePane() const;
    PaneState* paneById(PaneId id) const;
    void syncActivePointers();  // call after any Mux active-pane change
    void setupMuxCallbacks();
    void updateTabBar();
    PaneId createPaneState(int rows, int cols);
    void destroyPaneState(PaneId id);
    bool hasAnyAlivePty() const;

    // Font stack
    std::unique_ptr<IFontRasterizer> rasterizer;
    std::unique_ptr<IFontDiscovery> discovery;
    std::unique_ptr<FontShaper> shaper;
    std::unique_ptr<FontCollection> fontCollection;
    std::unique_ptr<GlyphAtlas> atlas;
    std::unique_ptr<GlyphCache> cache;

    // Renderer
    std::unique_ptr<D3DTextRenderer> renderer;

    // Keybinding manager
    std::unique_ptr<termcore::KeybindingManager> keybindings;

    // URL detection
    termcore::UrlDetector urlDetector;
    std::vector<termcore::DetectedUrl> detectedUrls;

    // Mux, notifications, agent tracking
    std::unique_ptr<termcore::Mux> mux;
    std::unique_ptr<termcore::NotificationStore> notifications;
    std::unique_ptr<termcore::AgentTracker> agentTracker;
    std::unique_ptr<termcore::ThemeHubWindow> themeHub;
    std::unique_ptr<termcore::FontHubWindow> fontHub;
    std::unique_ptr<termcore::SettingsWindow> settingsWin;

    // Configuration
    termcore::Config config;
    std::string fontFamily = "Consolas";
    float baseFontSize = 14.0f;   // from config or default
    float currentFontSize = 14.0f;

    // DPI
    float dpiScale = 1.0f;

    // State
    float cellWidth = 8.0f;
    float cellHeight = 16.0f;
    int termRows = 24;
    int termCols = 80;
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

    // Selection state
    struct GridPos { int row = 0; int col = 0; };
    GridPos selectionStart;
    GridPos selectionEnd;
    bool hasSelection = false;
    bool isDragging = false;

    // --- D3D / terminal lifecycle ---
    bool initD3D(HWND hWnd);
    void createRenderTarget();
    void destroyRenderTarget();
    void resizeSwapChain(int width, int height);
    void initTerminal();
    void startShell();
    void pollPty();
    void renderFrame();

    // --- Font size ---
    void changeFontSize(float delta);
    void resetFontSize();

    // --- Fullscreen ---
    void toggleFullscreen();

    // --- DWM title bar / blur ---
    void applyTitleBarTheme(HWND hwnd);
    void applyBackgroundBlur(HWND hwnd);

    // --- DPI ---
    void handleDpiChange(HWND hwnd, UINT dpi, const RECT* newRect);

    // --- Input ---
    void handleKeyDown(WPARAM wParam, LPARAM lParam);
    void handleChar(WPARAM wParam);
    void sendPtyData(const char* data, size_t len);

    // --- Mouse / selection ---
    GridPos pixelToGrid(int x, int y) const;
    void clearSelection();
    void updateRendererSelection();
    void handleMouseDown(int x, int y);
    void handleMouseMove(int x, int y);
    void handleMouseUp(int x, int y);
    void handleDoubleClick(int x, int y);
    std::string handleClickToMoveCursor(int row, int col, const Screen& scr);

    // --- Mouse protocol reporting ---
    bool sendMouseEvent(MouseEventType type, MouseButton button,
                        int x, int y);

    // --- Search ---
    bool searchActive = false;
    std::wstring searchQuery;
    HWND searchEditHwnd = nullptr;
    termcore::TerminalSearch terminalSearch;
    int currentMatchIndex = -1;
    static constexpr int kSearchEditId = 100;

    void openSearch();
    void closeSearch();
    void performSearch();
    void searchNext();
    void searchPrev();
    void repositionSearchBar();

    // --- Clipboard ---
    void copySelectionToClipboard();
    void pasteFromClipboard();
    std::string getSelectedText() const;
};

#endif // _WIN32
#endif // BREAD_TERMINAL_WINDOW_STATE_H
