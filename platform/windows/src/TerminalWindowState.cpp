#if defined(_WIN32)

#include "TerminalWindowState.h"
#include "DirectWriteRasterizer.h"
#include "DirectWriteDiscovery.h"

#include <algorithm>

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
        int cols = (std::max)(1, static_cast<int>(width / cellWidth));
        int rows = (std::max)(1, static_cast<int>(height / cellHeight));

        if (rows != termRows || cols != termCols) {
            termRows = rows;
            termCols = cols;
            if (screen) screen->resize(rows, cols);
            if (pty && pty->isAlive()) {
                pty->resize(rows, cols);
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

    // Screen + parser
    screen = std::make_unique<Screen>(termRows, termCols);
    parser = std::make_unique<VtParser>(*screen);

    // Apply theme/colors to dynamic color system
    screen->initDynamicColors(config);

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
}

void TerminalWindowState::startShell() {
    pty = termcore::createPty();
    if (!pty->spawn(config.shell, {}, "", termRows, termCols)) {
        OutputDebugStringW(L"BreadTerminal: failed to spawn shell\n");
    }
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
        if (screen) screen->resize(rows, cols);
        if (pty && pty->isAlive()) pty->resize(rows, cols);
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
        if (screen) screen->resize(rows, cols);
        if (pty && pty->isAlive()) pty->resize(rows, cols);
    }

    needsRender = true;
}

// --- PTY / rendering ---

void TerminalWindowState::pollPty() {
    if (!pty || !pty->isAlive()) return;

    bool wasAtBottom = screen ? screen->isViewportAtBottom() : true;

    char buf[8192];
    int n = pty->read(buf, sizeof(buf));
    while (n > 0) {
        parser->feed(buf, static_cast<size_t>(n));
        needsRender = true;
        n = pty->read(buf, sizeof(buf));
    }

    if (needsRender && wasAtBottom && screen) {
        screen->scrollViewportToBottom();
    }

    // Detect URLs in visible screen content
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

#endif // _WIN32
