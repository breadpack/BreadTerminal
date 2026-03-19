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

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <memory>
#include <string>

using Microsoft::WRL::ComPtr;
using namespace termcore;

/// Terminal window state, stored as GWLP_USERDATA on the HWND.
struct TerminalWindowState {
    HWND hwnd = nullptr;

    // D3D11 device
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> deviceContext;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID3D11RenderTargetView> rtv;

    // Core terminal state
    std::unique_ptr<Screen> screen;
    std::unique_ptr<VtParser> parser;
    std::unique_ptr<Pty> pty;

    // Font stack
    std::unique_ptr<IFontRasterizer> rasterizer;
    std::unique_ptr<IFontDiscovery> discovery;
    std::unique_ptr<FontShaper> shaper;
    std::unique_ptr<FontCollection> fontCollection;
    std::unique_ptr<GlyphAtlas> atlas;
    std::unique_ptr<GlyphCache> cache;

    // Renderer
    std::unique_ptr<D3DTextRenderer> renderer;

    // Configuration
    termcore::Config config;
    std::string fontFamily = "Consolas";
    float baseFontSize = 14.0f;   // from config or default
    float currentFontSize = 14.0f;

    // State
    float cellWidth = 8.0f;
    float cellHeight = 16.0f;
    int termRows = 24;
    int termCols = 80;
    bool needsRender = false;
    bool inLiveResize = false;
    bool cursorBlinkOn = true;

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
