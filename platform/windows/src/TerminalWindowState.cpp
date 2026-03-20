#if defined(_WIN32)

#include "TerminalWindowState.h"
#include "DirectWriteRasterizer.h"
#include "DirectWriteDiscovery.h"

#include <algorithm>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

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

    // Recalculate terminal dimensions
    if (cellWidth > 0 && cellHeight > 0) {
        // Reserve space for tab bar when visible (2+ tabs)
        bool tabBarVisible = mux && mux->getWorkspace(wsId)
            && mux->getWorkspace(wsId)->tabs.size() > 1;
        float tabBarH = tabBarVisible ? cellHeight * D3DTextRenderer::kTabBarHeightScale : 0.0f;
        float contentH = height - tabBarH;
        int cols = (std::max)(1, static_cast<int>(width / cellWidth));
        int rows = (std::max)(1, static_cast<int>(contentH / cellHeight));

        if (rows != termRows || cols != termCols) {
            termRows = rows;
            termCols = cols;
            // Resize ALL panes
            for (auto& [id, ps] : panes) {
                if (ps->screen) ps->screen->resize(rows, cols);
                if (ps->pty && ps->pty->isAlive()) {
                    ps->pty->resize(rows, cols);
                }
            }
            needsRender = true;
        }

        // Track resize overlay state
        showResizeOverlay = true;
        resizeOverlayStart = std::chrono::steady_clock::now();
        resizeOverlayCols = screen ? screen->cols() : 0;
        resizeOverlayRows = screen ? screen->rows() : 0;
        needsRender = true;
    }
}

// --- Pane management ---

PaneState* TerminalWindowState::activePane() const {
    if (!mux) return nullptr;
    auto* tab = mux->activeTab(wsId);
    if (!tab) return nullptr;
    PaneId pid = tab->active_pane;
    auto it = panes.find(pid);
    return it != panes.end() ? it->second.get() : nullptr;
}

PaneState* TerminalWindowState::paneById(PaneId id) const {
    auto it = panes.find(id);
    return it != panes.end() ? it->second.get() : nullptr;
}

void TerminalWindowState::syncActivePointers() {
    auto* ap = activePane();
    if (ap) {
        screen = ap->screen.get();
        pty = ap->pty.get();
    } else {
        screen = nullptr;
        pty = nullptr;
    }
}

PaneId TerminalWindowState::createPaneState(int rows, int cols) {
    PaneId id = nextPaneId++;
    auto ps = std::make_unique<PaneState>();
    ps->id = id;
    ps->screen = std::make_unique<Screen>(rows, cols);
    ps->parser = std::make_unique<VtParser>(*ps->screen);
    ps->screen->initDynamicColors(config);

    ps->pty = termcore::createPty();
    if (!ps->pty->spawn(config.shell, {}, "", rows, cols)) {
        OutputDebugStringW(L"BreadTerminal: failed to spawn shell for pane\n");
    }

    panes[id] = std::move(ps);
    return id;
}

void TerminalWindowState::destroyPaneState(PaneId id) {
    panes.erase(id);
}

bool TerminalWindowState::hasAnyAlivePty() const {
    for (const auto& [id, ps] : panes) {
        if (ps->pty && ps->pty->isAlive()) return true;
    }
    return false;
}

void TerminalWindowState::setupMuxCallbacks() {
    mux->setPaneCallbacks(
        // PaneCreateCallback
        [this](int rows, int cols) -> PaneId {
            return createPaneState(rows, cols);
        },
        // PaneDestroyCallback
        [this](PaneId id) {
            destroyPaneState(id);
        }
    );

    mux->setOnChanged([this]() {
        syncActivePointers();
        updateTabBar();
        needsRender = true;
    });
}

void TerminalWindowState::updateTabBar() {
    if (!mux || !renderer) return;
    auto* ws = mux->getWorkspace(wsId);
    if (!ws) return;

    D3DTextRenderer::TabBarInfo tabInfo;
    // Only show tab bar when there are 2+ tabs
    tabInfo.visible = ws->tabs.size() > 1;

    // Derive tab bar colors from theme
    uint32_t bgR = (config.background >> 16) & 0xFF;
    uint32_t bgG = (config.background >> 8) & 0xFF;
    uint32_t bgB = config.background & 0xFF;
    int lum = bgR * 299 + bgG * 587 + bgB * 114;
    bool isDark = lum < 128000;

    // Tab bar bg: darker/lighter than terminal bg for depth
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
    // Active tab = terminal bg (seamless connection to content)
    tabInfo.active_bg_color = config.background;
    // Inactive tab = blends into tab bar
    tabInfo.inactive_bg_color = tabInfo.bg_color;
    tabInfo.fg_color = config.foreground;
    // Accent from palette blue (index 4)
    tabInfo.accent_color = config.palette[4] ? config.palette[4] : 0x007acc;

    for (size_t i = 0; i < ws->tabs.size(); ++i) {
        auto& tab = ws->tabs[i];
        D3DTextRenderer::TabInfo ti;

        // Get title from the active pane's screen (reflects running process)
        std::string screenTitle;
        PaneId activePaneId = mux->activePaneId(wsId, tab->id);
        if (activePaneId != termcore::kInvalidPane) {
            auto it = panes.find(activePaneId);
            if (it != panes.end() && it->second->screen) {
                screenTitle = it->second->screen->title();
            }
        }

        if (!screenTitle.empty()) {
            ti.title = screenTitle;
        } else if (!tab->title.empty()) {
            ti.title = tab->title;
        } else {
            ti.title = "Tab " + std::to_string(i + 1);
        }

        ti.active = (i == ws->active_tab_index);
        tabInfo.tabs.push_back(ti);
    }

    renderer->setTabBar(tabInfo);
}

// --- Terminal initialization ---

void TerminalWindowState::initTerminal() {
    // Load config
    std::string configPath = termcore::defaultConfigPath();
    if (!configPath.empty()) {
        config = termcore::parseConfigFile(configPath);
    }

    // Apply font settings from config (fallback to Consolas 14pt)
    fontFamily = config.font_family.empty() ? "Consolas" : config.font_family;
    if (fontFamily == "Menlo") fontFamily = "Consolas";
    baseFontSize = config.font_size > 0 ? config.font_size : 14.0f;
    currentFontSize = baseFontSize;

    // Font stack
    rasterizer = createDirectWriteRasterizer();
    discovery = createDirectWriteDiscovery();
    shaper = std::make_unique<FontShaper>();
    fontCollection = std::make_unique<FontCollection>(
        *rasterizer, *discovery, *shaper);
    fontCollection->setPrimaryFont(fontFamily, currentFontSize);
    atlas = std::make_unique<GlyphAtlas>();
    cache = std::make_unique<GlyphCache>();

    // Cell dimensions
    auto metrics = fontCollection->primaryMetrics();
    cellWidth = metrics.cell_width > 0 ? metrics.cell_width : 8.0f;
    cellHeight = metrics.cell_height > 0 ? metrics.cell_height : 16.0f;

    // Calculate actual rows/cols from window client area
    RECT initRc;
    GetClientRect(hwnd, &initRc);
    int initWidth = initRc.right - initRc.left;
    int initHeight = initRc.bottom - initRc.top;
    if (initWidth > 0 && initHeight > 0 && cellWidth > 0 && cellHeight > 0) {
        termCols = (std::max)(1, static_cast<int>(initWidth / cellWidth));
        termRows = (std::max)(1, static_cast<int>(initHeight / cellHeight));
    }

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

    // Keybinding manager
    keybindings = std::make_unique<termcore::KeybindingManager>();
    if (!config.keybindings.empty()) {
        std::vector<std::pair<std::string, std::string>> bindings;
        for (const auto& kb : config.keybindings) {
            bindings.emplace_back(kb.trigger, kb.action);
        }
        keybindings->loadFromConfig(bindings);
    }

    // Mux, notifications, agent tracking
    mux = std::make_unique<termcore::Mux>();
    notifications = std::make_unique<termcore::NotificationStore>();
    agentTracker = std::make_unique<termcore::AgentTracker>();

    // Set up Mux callbacks and create initial workspace + tab
    setupMuxCallbacks();
    wsId = mux->createWorkspace("default");
    mux->createTab(wsId, termRows, termCols);
    syncActivePointers();
    updateTabBar();
}

void TerminalWindowState::startShell() {
    // Shell is now spawned per-pane in createPaneState().
    // This method is kept for compatibility; the first pane is
    // already created by initTerminal() via mux->createTab().
}

// --- Font size ---

void TerminalWindowState::changeFontSize(float delta) {
    float newSize = currentFontSize + delta;
    newSize = (std::max)(6.0f, (std::min)(72.0f, newSize));
    if (newSize == currentFontSize) return;
    currentFontSize = newSize;

    fontCollection->setPrimaryFont(fontFamily, currentFontSize);

    auto metrics = fontCollection->primaryMetrics();
    cellWidth = metrics.cell_width > 0 ? metrics.cell_width : 8.0f;
    cellHeight = metrics.cell_height > 0 ? metrics.cell_height : 16.0f;

    if (cache) cache->clear();

    RECT rc;
    GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    int cols = (std::max)(1, static_cast<int>(width / cellWidth));
    int rows = (std::max)(1, static_cast<int>(height / cellHeight));
    if (rows != termRows || cols != termCols) {
        termRows = rows;
        termCols = cols;
        for (auto& [id, ps] : panes) {
            if (ps->screen) ps->screen->resize(rows, cols);
            if (ps->pty && ps->pty->isAlive()) ps->pty->resize(rows, cols);
        }
    }

    needsRender = true;
}

void TerminalWindowState::resetFontSize() {
    if (currentFontSize == baseFontSize) return;
    currentFontSize = baseFontSize;

    fontCollection->setPrimaryFont(fontFamily, currentFontSize);

    auto metrics = fontCollection->primaryMetrics();
    cellWidth = metrics.cell_width > 0 ? metrics.cell_width : 8.0f;
    cellHeight = metrics.cell_height > 0 ? metrics.cell_height : 16.0f;

    if (cache) cache->clear();

    RECT rc;
    GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    int cols = (std::max)(1, static_cast<int>(width / cellWidth));
    int rows = (std::max)(1, static_cast<int>(height / cellHeight));
    if (rows != termRows || cols != termCols) {
        termRows = rows;
        termCols = cols;
        for (auto& [id, ps] : panes) {
            if (ps->screen) ps->screen->resize(rows, cols);
            if (ps->pty && ps->pty->isAlive()) ps->pty->resize(rows, cols);
        }
    }

    needsRender = true;
}

// --- PTY / rendering ---

void TerminalWindowState::pollPty() {
    // Poll ALL panes for PTY output
    std::vector<PaneId> deadPanes;
    char buf[8192];

    for (auto& [id, ps] : panes) {
        if (!ps->pty) continue;

        bool wasAtBottom = ps->screen ? ps->screen->isViewportAtBottom() : true;

        int n = ps->pty->read(buf, sizeof(buf));
        while (n > 0) {
            ps->parser->feed(buf, static_cast<size_t>(n));
            needsRender = true;
            n = ps->pty->read(buf, sizeof(buf));
        }

        if (needsRender && wasAtBottom && ps->screen) {
            ps->screen->scrollViewportToBottom();
        }

        // Track dead panes for cleanup
        if (!ps->pty->isAlive()) {
            deadPanes.push_back(id);
        }
    }

    // Auto-close dead panes
    for (PaneId deadId : deadPanes) {
        if (!mux) continue;
        auto* tab = mux->activeTab(wsId);
        if (!tab) continue;

        // Find which tab contains this pane and close it
        auto tabIds = mux->allTabIds(wsId);
        for (auto tid : tabIds) {
            auto allPanesInTab = mux->allPanes(wsId, tid);
            for (auto pid : allPanesInTab) {
                if (pid == deadId) {
                    if (allPanesInTab.size() == 1 && tabIds.size() == 1) {
                        // Last pane in last tab — close window
                        PostMessageW(hwnd, WM_CLOSE, 0, 0);
                        return;
                    }
                    mux->closePane(wsId, tid, deadId);
                    syncActivePointers();
                    updateTabBar();
                    goto nextDead;
                }
            }
        }
        nextDead:;
    }

    // Detect URLs in active screen
    if (needsRender && screen) {
        detectedUrls = urlDetector.detectInScreen(*screen);
    }
}

void TerminalWindowState::renderFrame() {
    if (!renderer || !screen) return;

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

void TerminalWindowState::sendPtyData(const char* data, size_t len) {
    if (pty && pty->isAlive()) {
        pty->write(data, len);
    }
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
    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20
    // DWMWA_CAPTION_COLOR = 35
    // DWMWA_TEXT_COLOR = 36

    // Determine if theme is dark (simple luminance check)
    uint32_t bg = config.background;
    int lum = ((bg >> 16) & 0xFF) * 299 + ((bg >> 8) & 0xFF) * 587 + (bg & 0xFF) * 114;
    BOOL useDark = (lum < 128000) ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark));

    // Set caption color to match terminal background
    COLORREF captionColor = RGB((bg >> 16) & 0xFF, (bg >> 8) & 0xFF, bg & 0xFF);
    DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor));

    // Set title text color to match terminal foreground
    uint32_t fg = config.foreground;
    COLORREF textColor = RGB((fg >> 16) & 0xFF, (fg >> 8) & 0xFF, fg & 0xFF);
    DwmSetWindowAttribute(hwnd, 36, &textColor, sizeof(textColor));
}

// --- DWM background blur ---

void TerminalWindowState::applyBackgroundBlur(HWND hwnd) {
    if (config.background_blur <= 0) return;

    // Try Windows 11 Mica/Acrylic first (DWM_SYSTEMBACKDROP_TYPE)
    // DWMWA_SYSTEMBACKDROP_TYPE = 38
    enum { DWMSBT_NONE = 1, DWMSBT_MAINWINDOW = 2, DWMSBT_TRANSIENTWINDOW = 3, DWMSBT_TABBEDWINDOW = 4 };
    int backdrop = DWMSBT_TRANSIENTWINDOW; // Acrylic
    HRESULT hr = DwmSetWindowAttribute(hwnd, 38, &backdrop, sizeof(backdrop));

    if (FAILED(hr)) {
        // Fallback for older Windows: use DWM blur behind
        DWM_BLURBEHIND bb = {};
        bb.dwFlags = DWM_BB_ENABLE;
        bb.fEnable = TRUE;
        DwmEnableBlurBehindWindow(hwnd, &bb);
    }
}

// --- DPI ---

void TerminalWindowState::handleDpiChange(HWND hwnd, UINT dpi, const RECT* newRect) {
    dpiScale = static_cast<float>(dpi) / 96.0f;

    if (newRect) {
        SetWindowPos(hwnd, nullptr,
            newRect->left, newRect->top,
            newRect->right - newRect->left,
            newRect->bottom - newRect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // Font size needs to be recalculated with new DPI
    // The font system already handles this through changeFontSize/resetFontSize
    needsRender = true;
}

#endif // _WIN32
