#if defined(_WIN32)

#include "TerminalWindowState.h"
#include "DirectWriteRasterizer.h"
#include "DirectWriteDiscovery.h"

using termcore::D3DTextRenderer;

#include <algorithm>
#include <commctrl.h>
#include <dwmapi.h>
#include <thread>

namespace termcore {
    void positionImeWindow(HWND hwnd, int x, int y, int height);
}

#pragma comment(lib, "dwmapi.lib")

namespace {

constexpr UINT_PTR kSearchEditSubclassId = 1;

// Subclass procedure for the search EDIT control to handle Enter/Escape
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

} // namespace

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
    }
}

void TerminalWindowState::renderFrame() {
    if (!renderer || !controller) return;

    termcore::Screen* screen = controller->activeScreen();
    if (!screen) return;

    // Update selection on renderer
    updateRendererSelection();

    // Update command palette state on renderer
    updateCommandPalette();

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
        ti.active = tabs[i].active;
        tabInfo.tabs.push_back(ti);
    }

    renderer->setTabBar(tabInfo);
}

void TerminalWindowState::updateCommandPalette() {
    if (!renderer || !controller) return;

    auto& cp = controller->commandPalette();
    D3DTextRenderer::CommandPaletteInfo info;
    info.visible = cp.isOpen();

    if (info.visible) {
        info.query = cp.query();
        info.selectedIndex = cp.selectedIndex();

        const auto& filtered = cp.filteredCommands();
        int maxItems = (std::min)(static_cast<int>(filtered.size()),
                                  termcore::CommandPalette::kMaxVisibleItems);
        for (int i = 0; i < maxItems; ++i) {
            D3DTextRenderer::CommandPaletteInfo::Item item;
            item.name = filtered[i].name;
            item.shortcut_hint = filtered[i].shortcut_hint;
            info.items.push_back(std::move(item));
        }
    }

    renderer->setCommandPalette(info);
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

// --- IPlatformHost implementation ---

void TerminalWindowState::invalidate() {
    needsRender = true;
}

void TerminalWindowState::getViewportSize(int& w, int& h) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
}

std::string TerminalWindowState::getClipboardText() {
    std::string utf8;
    if (!OpenClipboard(hwnd)) return utf8;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        const wchar_t* pData = static_cast<const wchar_t*>(GlobalLock(hData));
        if (pData) {
            int wlen = static_cast<int>(wcslen(pData));
            int utf8Len = WideCharToMultiByte(CP_UTF8, 0, pData, wlen,
                                               nullptr, 0, nullptr, nullptr);
            if (utf8Len > 0) {
                utf8.resize(utf8Len);
                WideCharToMultiByte(CP_UTF8, 0, pData, wlen,
                                    &utf8[0], utf8Len, nullptr, nullptr);
            }
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return utf8;
}

void TerminalWindowState::setClipboardText(const std::string& text) {
    if (text.empty()) return;

    int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                    text.c_str(), static_cast<int>(text.size()),
                                    nullptr, 0);
    if (wlen <= 0) return;

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (wlen + 1) * sizeof(wchar_t));
    if (!hMem) return;

    wchar_t* pMem = static_cast<wchar_t*>(GlobalLock(hMem));
    if (pMem) {
        MultiByteToWideChar(CP_UTF8, 0,
                            text.c_str(), static_cast<int>(text.size()),
                            pMem, wlen);
        pMem[wlen] = L'\0';
        GlobalUnlock(hMem);

        if (OpenClipboard(hwnd)) {
            EmptyClipboard();
            SetClipboardData(CF_UNICODETEXT, hMem);
            CloseClipboard();
        } else {
            GlobalFree(hMem);
        }
    } else {
        GlobalFree(hMem);
    }
}

void TerminalWindowState::setWindowTitle(const std::string& title) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                    title.c_str(), static_cast<int>(title.size()),
                                    nullptr, 0);
    if (wlen > 0) {
        std::wstring wtitle(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0,
                            title.c_str(), static_cast<int>(title.size()),
                            &wtitle[0], wlen);
        SetWindowTextW(hwnd, wtitle.c_str());
    }
}

void TerminalWindowState::closeWindow() {
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

void TerminalWindowState::showConfirmDialog(const std::string& msg,
                                             std::function<void(bool)> cb) {
    // Run dialog in a separate thread to avoid blocking the event loop
    std::string capturedMsg = msg;
    HWND capturedHwnd = hwnd;
    std::thread([capturedMsg, capturedHwnd, cb = std::move(cb)]() {
        int wlen = MultiByteToWideChar(CP_UTF8, 0,
                                        capturedMsg.c_str(),
                                        static_cast<int>(capturedMsg.size()),
                                        nullptr, 0);
        std::wstring wmsg(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0,
                            capturedMsg.c_str(),
                            static_cast<int>(capturedMsg.size()),
                            &wmsg[0], wlen);

        int result = MessageBoxW(capturedHwnd, wmsg.c_str(),
                                 L"BreadTerminal",
                                 MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
        if (cb) cb(result == IDYES);
    }).detach();
}

void TerminalWindowState::showSearchBar() {
    if (searchActive && searchEditHwnd) {
        SetFocus(searchEditHwnd);
        SendMessageW(searchEditHwnd, EM_SETSEL, 0, -1);
        return;
    }

    searchActive = true;

    RECT rc;
    GetClientRect(hwnd, &rc);

    constexpr int kSearchBarWidth = 300;
    constexpr int kSearchBarHeight = 24;
    constexpr int kSearchBarMargin = 8;

    int x = rc.right - kSearchBarWidth - kSearchBarMargin;
    int y = kSearchBarMargin;

    searchEditHwnd = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        x, y, kSearchBarWidth, kSearchBarHeight,
        hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchEditId)),
        GetModuleHandleW(nullptr),
        nullptr);

    if (!searchEditHwnd) {
        searchActive = false;
        return;
    }

    // Subclass for Enter/Escape handling
    SetWindowSubclass(searchEditHwnd, SearchEditSubclassProc,
                      kSearchEditSubclassId,
                      reinterpret_cast<DWORD_PTR>(this));

    // Set font to match terminal feel
    HFONT hFont = CreateFontW(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Consolas");
    if (hFont) {
        SendMessageW(searchEditHwnd, WM_SETFONT,
                     reinterpret_cast<WPARAM>(hFont), TRUE);
    }

    SetFocus(searchEditHwnd);
}

void TerminalWindowState::hideSearchBar() {
    if (searchEditHwnd) {
        HFONT hFont = reinterpret_cast<HFONT>(
            SendMessageW(searchEditHwnd, WM_GETFONT, 0, 0));
        DestroyWindow(searchEditHwnd);
        if (hFont) {
            DeleteObject(hFont);
        }
        searchEditHwnd = nullptr;
    }

    searchActive = false;

    // Clear search highlights from renderer
    if (renderer) {
        renderer->setSearchHighlights({}, -1);
    }

    SetFocus(hwnd);
    needsRender = true;
}

void TerminalWindowState::updateSearchResults(int current, int total) {
    // For now, we don't display match count in the search bar.
    // Could add a label next to the edit control in future.
    (void)current;
    (void)total;
    needsRender = true;
}

void TerminalWindowState::positionIME(int x, int y, int height) {
    termcore::positionImeWindow(hwnd, x, y, height);
}

void TerminalWindowState::onFontChanged(float cellW, float cellH) {
    if (cache) cache->clear();
    // Recreate atlas to free old font glyphs
    if (atlas) {
        atlas = std::make_unique<termcore::GlyphAtlas>();
        if (renderer) {
            renderer->setFontStack(fontCollection.get(), cache.get(),
                                   atlas.get(), rasterizer.get());
        }
    }
    needsRender = true;
}

void TerminalWindowState::onColorsChanged() {
    applyTitleBarTheme(hwnd);
    updateTabBar();
    needsRender = true;
}

void TerminalWindowState::onGridSizeChanged(int rows, int cols) {
    // Resize overlay
    showResizeOverlay = true;
    resizeOverlayStart = std::chrono::steady_clock::now();
    resizeOverlayCols = cols;
    resizeOverlayRows = rows;
    updateTabBar();
    needsRender = true;
}

void TerminalWindowState::showNotification(const std::string& title,
                                            const std::string& body) {
    // TODO: Win32 notification toast
    (void)title;
    (void)body;
}

void TerminalWindowState::openSettingsWindow(const termcore::Config& config) {
    if (!settingsWin) {
        settingsWin = std::make_unique<termcore::SettingsWindow>();
    }
    settingsWin->setConfig(config);
    settingsWin->setSaveCallback([this](const termcore::Config& updated) {
        if (controller) {
            controller->onConfigChanged(updated);
        }
    });
    settingsWin->show(hwnd);
}

void TerminalWindowState::openThemeHub(const termcore::Config& config) {
    if (!themeHub) {
        themeHub = std::make_unique<termcore::ThemeHubWindow>();
    }
    themeHub->setConfig(config);
    themeHub->setApplyCallback([this](const std::string& name,
                                       const termcore::ThemeMetadata* /*meta*/) {
        if (controller) {
            controller->onThemeChanged(name);
            // Update the ThemeHub popup itself with new theme colors
            themeHub->setConfig(controller->config());
        }
    });
    themeHub->show(hwnd);
}

void TerminalWindowState::openFontHub(const termcore::Config& config) {
    if (!fontHub) {
        fontHub = std::make_unique<termcore::FontHubWindow>();
    }
    fontHub->setConfig(config);
    fontHub->setApplyCallback([this](const std::string& name) {
        if (controller) {
            controller->onFontChanged(name);
        }
    });
    fontHub->show(hwnd);
}

float TerminalWindowState::dpiScale() {
    return dpiScale_;
}

std::unique_ptr<termcore::Pty> TerminalWindowState::createPty(
        const std::string& shell, int rows, int cols) {
    auto pty = termcore::createPty();
    if (!pty->spawn(shell, {}, "", rows, cols)) {
        OutputDebugStringW(L"BreadTerminal: failed to spawn shell for pane\n");
    }
    return pty;
}

void TerminalWindowState::repositionSearchBar() {
    if (!searchEditHwnd) return;

    constexpr int kSearchBarWidth = 300;
    constexpr int kSearchBarHeight = 24;
    constexpr int kSearchBarMargin = 8;

    RECT rc;
    GetClientRect(hwnd, &rc);

    int x = rc.right - kSearchBarWidth - kSearchBarMargin;
    int y = kSearchBarMargin;

    SetWindowPos(searchEditHwnd, nullptr, x, y,
                 kSearchBarWidth, kSearchBarHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

#endif // _WIN32
