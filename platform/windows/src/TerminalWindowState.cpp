#if defined(_WIN32)

#include "TerminalWindowState.h"
#include "DirectWriteRasterizer.h"
#include "DirectWriteDiscovery.h"

using termcore::D3DTextRenderer;

#include <algorithm>
#include <commctrl.h>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace {

constexpr UINT_PTR kSearchEditSubclassId = 1;

} // namespace

// Subclass procedure for the search EDIT control to handle Enter/Escape.
// Defined at file scope so TerminalWindowStateEvents.cpp can reference it.
LRESULT CALLBACK SearchEditSubclassProc(
        HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
        UINT_PTR /*uIdSubclass*/, DWORD_PTR dwRefData) {
    auto* state = reinterpret_cast<TerminalWindowState*>(dwRefData);

    switch (msg) {
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                if (state->controller) {
                    state->controller->onSearchQuery("");
                }
                state->hideSearchBar();
                return 0;
            }
            if (wParam == VK_RETURN) {
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (state->controller) {
                    if (shift) state->controller->onSearchPrev();
                    else state->controller->onSearchNext();
                }
                return 0;
            }
            if (wParam == VK_F3) {
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (state->controller) {
                    if (shift) state->controller->onSearchPrev();
                    else state->controller->onSearchNext();
                }
                return 0;
            }
            break;

        case WM_CHAR:
            if (wParam == '\r' || wParam == 27) {
                return 0;
            }
            break;

        case WM_NCDESTROY:
            RemoveWindowSubclass(hWnd, SearchEditSubclassProc,
                                 kSearchEditSubclassId);
            break;
    }

    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// --- D3D11 lifecycle ---

bool TerminalWindowState::initD3D(HWND hWnd) {
    hwnd = hWnd;

    // Create D3D11 device
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    UINT flags = 0;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        &featureLevel, 1, D3D11_SDK_VERSION,
        device.GetAddressOf(), nullptr,
        deviceContext.GetAddressOf());
    if (FAILED(hr)) return false;

    // Get DXGI factory
    ComPtr<IDXGIDevice> dxgiDevice;
    device.As(&dxgiDevice);
    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(adapter.GetAddressOf());
    ComPtr<IDXGIFactory2> factory;
    adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));

    // Create swap chain
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    hr = factory->CreateSwapChainForHwnd(
        device.Get(), hWnd, &desc, nullptr, nullptr,
        swapChain.GetAddressOf());
    if (FAILED(hr)) return false;

    createRenderTarget();
    return true;
}

void TerminalWindowState::createRenderTarget() {
    ComPtr<ID3D11Texture2D> backBuffer;
    swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (backBuffer) {
        device->CreateRenderTargetView(
            backBuffer.Get(), nullptr, rtv.GetAddressOf());
    }
}

void TerminalWindowState::destroyRenderTarget() {
    rtv.Reset();
}

void TerminalWindowState::resizeSwapChain(int width, int height) {
    if (width <= 0 || height <= 0) return;

    destroyRenderTarget();
    swapChain->ResizeBuffers(0, static_cast<UINT>(width),
                              static_cast<UINT>(height),
                              DXGI_FORMAT_UNKNOWN, 0);
    createRenderTarget();

    if (renderer) {
        renderer->setRenderTarget(rtv.Get());
        renderer->resize(static_cast<float>(width),
                         static_cast<float>(height));
    }

    // Delegate grid recalculation to controller
    if (controller) {
        controller->onResize(width, height);

        // Track resize overlay state
        showResizeOverlay = true;
        resizeOverlayStart = std::chrono::steady_clock::now();
        resizeOverlayCols = controller->termCols();
        resizeOverlayRows = controller->termRows();
        needsRender = true;
    }
}

// --- Terminal initialization ---

void TerminalWindowState::initTerminal() {
    // Load config
    termcore::Config config = termcore::loadConfig();

    // Apply font settings from config (fallback to Consolas 14pt)
    std::string fontFamily = config.font_family.empty() ? "Consolas" : config.font_family;
    if (fontFamily == "Menlo") fontFamily = "Consolas";
    config.font_family = fontFamily;

    // Font rasterization stack
    rasterizer = termcore::createDirectWriteRasterizer();
    discovery = termcore::createDirectWriteDiscovery();
    shaper = std::make_unique<termcore::FontShaper>();
    fontCollection = std::make_unique<termcore::FontCollection>(
        *rasterizer, *discovery, *shaper);

    float fontSize = config.font_size > 0 ? config.font_size : 14.0f;
    fontCollection->setPrimaryFont(fontFamily, fontSize);

    atlas = std::make_unique<termcore::GlyphAtlas>();
    cache = std::make_unique<termcore::GlyphCache>();

    // Renderer
    renderer = std::make_unique<D3DTextRenderer>();
    renderer->initialize(device.Get(), deviceContext.Get());
    renderer->setRenderTarget(rtv.Get());
    renderer->setFontStack(
        fontCollection.get(), cache.get(),
        atlas.get(), rasterizer.get());

    // Set initial viewport
    RECT rc;
    GetClientRect(hwnd, &rc);
    renderer->resize(static_cast<float>(rc.right - rc.left),
                     static_cast<float>(rc.bottom - rc.top));

    // Notifications, agent tracking
    notifications = std::make_unique<termcore::NotificationStore>();
    agentTracker = std::make_unique<termcore::AgentTracker>();

    // Create controller with this as the IPlatformHost
    controller = std::make_unique<termcore::TerminalController>(
        this, std::move(config), fontCollection.get());
    controller->initTerminal();

    updateTabBar();
    needsRender = true;
}

// --- PTY / rendering ---

void TerminalWindowState::pollPty() {
    if (!controller) return;

    controller->pollPty();
    if (controller->needsRender()) {
        needsRender = true;
        controller->clearNeedsRender();
        if (renderer) renderer->markContentDirty();
    }
}

void TerminalWindowState::renderFrame() {
    if (!renderer || !controller) return;

    termcore::Screen* screen = controller->activeScreen();
    if (!screen) return;

    // Update tab bar and selection on renderer
    updateTabBar();
    updateRendererSelection();

    renderer->render(*screen);

    if (swapChain) {
        UINT syncInterval = inLiveResize ? 0 : 1;
        swapChain->Present(syncInterval, 0);
    }

    if (hwnd && screen) {
        int offset = screen->viewportOffset();
        if (offset > 0) {
            std::wstring title = L"BreadTerminal [scrollback: +"
                + std::to_wstring(offset) + L" lines]";
            SetWindowTextW(hwnd, title.c_str());
        } else {
            SetWindowTextW(hwnd, L"BreadTerminal");
        }
    }
}

// --- Helpers ---

void TerminalWindowState::updateTabBar() {
    if (!renderer || !controller) return;

    auto tabs = controller->tabBarInfo();
    const auto& config = controller->config();
    float cellH = controller->cellHeight();

    D3DTextRenderer::TabBarInfo tabInfo;
    tabInfo.visible = static_cast<int>(tabs.size()) > 1;

    // Derive tab bar colors from theme
    uint32_t bgR = (config.background >> 16) & 0xFF;
    uint32_t bgG = (config.background >> 8) & 0xFF;
    uint32_t bgB = config.background & 0xFF;
    int lum = bgR * 299 + bgG * 587 + bgB * 114;
    bool isDark = lum < 128000;

    if (isDark) {
        tabInfo.bg_color =
            ((uint32_t)(bgR * 0.7f) << 16) |
            ((uint32_t)(bgG * 0.7f) << 8) |
             (uint32_t)(bgB * 0.7f);
    } else {
        tabInfo.bg_color =
            ((uint32_t)(bgR * 0.92f) << 16) |
            ((uint32_t)(bgG * 0.92f) << 8) |
             (uint32_t)(bgB * 0.92f);
    }
    tabInfo.active_bg_color = config.background;
    tabInfo.inactive_bg_color = tabInfo.bg_color;
    tabInfo.fg_color = config.foreground;
    tabInfo.accent_color = config.palette[4] ? config.palette[4] : 0x007acc;

    for (size_t i = 0; i < tabs.size(); ++i) {
        D3DTextRenderer::TabInfo ti;
        ti.title = tabs[i].title;
        ti.icon_name = tabs[i].icon_name;
        ti.process_name = tabs[i].process_name;
        ti.active = tabs[i].active;
        ti.has_unread = tabs[i].has_unread;
        ti.needs_attention = tabs[i].needs_attention;
        tabInfo.tabs.push_back(ti);
    }

    renderer->setTabBar(tabInfo);
}

void TerminalWindowState::updateRendererSelection() {
    if (!renderer || !controller) return;
    const auto& sel = controller->selection();
    D3DTextRenderer::Selection dSel;
    dSel.active = sel.hasSelection();
    dSel.startRow = sel.start().row;
    dSel.startCol = sel.start().col;
    dSel.endRow = sel.end().row;
    dSel.endCol = sel.end().col;
    renderer->setSelection(dSel);
}

// --- Fullscreen ---

void TerminalWindowState::toggleFullscreen() {
    if (!hwnd) return;

    if (!isFullscreen) {
        savedStyle = GetWindowLong(hwnd, GWL_STYLE);
        savedExStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        GetWindowPlacement(hwnd, &savedPlacement);

        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);

        SetWindowLong(hwnd, GWL_STYLE,
                      savedStyle & ~(WS_CAPTION | WS_THICKFRAME));
        SetWindowLong(hwnd, GWL_EXSTYLE,
                      savedExStyle & ~(WS_EX_DLGMODALFRAME |
                                       WS_EX_WINDOWEDGE |
                                       WS_EX_CLIENTEDGE |
                                       WS_EX_STATICEDGE));

        SetWindowPos(hwnd, HWND_TOP,
            mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_NOZORDER | SWP_FRAMECHANGED);

        isFullscreen = true;
    } else {
        SetWindowLong(hwnd, GWL_STYLE, savedStyle);
        SetWindowLong(hwnd, GWL_EXSTYLE, savedExStyle);
        SetWindowPlacement(hwnd, &savedPlacement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

        isFullscreen = false;
    }
}

// --- DWM title bar theming ---

void TerminalWindowState::applyTitleBarTheme(HWND hwnd) {
    const auto& config = controller ? controller->config() : termcore::Config{};

    uint32_t bg = config.background;
    int lum = ((bg >> 16) & 0xFF) * 299 + ((bg >> 8) & 0xFF) * 587 + (bg & 0xFF) * 114;
    BOOL useDark = (lum < 128000) ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark));

    COLORREF captionColor = RGB((bg >> 16) & 0xFF, (bg >> 8) & 0xFF, bg & 0xFF);
    DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor));

    uint32_t fg = config.foreground;
    COLORREF textColor = RGB((fg >> 16) & 0xFF, (fg >> 8) & 0xFF, fg & 0xFF);
    DwmSetWindowAttribute(hwnd, 36, &textColor, sizeof(textColor));
}

// --- DWM background blur ---

void TerminalWindowState::applyBackgroundBlur(HWND hwnd) {
    const auto& config = controller ? controller->config() : termcore::Config{};
    if (config.background_blur <= 0) return;

    enum { DWMSBT_NONE = 1, DWMSBT_MAINWINDOW = 2, DWMSBT_TRANSIENTWINDOW = 3, DWMSBT_TABBEDWINDOW = 4 };
    int backdrop = DWMSBT_TRANSIENTWINDOW;
    HRESULT hr = DwmSetWindowAttribute(hwnd, 38, &backdrop, sizeof(backdrop));

    if (FAILED(hr)) {
        DWM_BLURBEHIND bb = {};
        bb.dwFlags = DWM_BB_ENABLE;
        bb.fEnable = TRUE;
        DwmEnableBlurBehindWindow(hwnd, &bb);
    }
}

// --- DPI ---

void TerminalWindowState::handleDpiChange(HWND hwnd, UINT dpi, const RECT* newRect) {
    dpiScale_ = static_cast<float>(dpi) / 96.0f;

    if (newRect) {
        SetWindowPos(hwnd, nullptr,
            newRect->left, newRect->top,
            newRect->right - newRect->left,
            newRect->bottom - newRect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }

    needsRender = true;
}

#endif // _WIN32
