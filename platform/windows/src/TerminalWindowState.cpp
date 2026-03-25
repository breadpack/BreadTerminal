#if defined(_WIN32)

#include "TerminalWindowState.h"
#include "TerminalAccessibility.h"
#include "HighContrastDetector.h"
#include "DirectWriteRasterizer.h"
#include "DirectWriteDiscovery.h"
#include "termcore/font/unicode_width.h"

#include <imm.h>

using termcore::D3DTextRenderer;

#include <algorithm>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <thread>

#pragma comment(lib, "dcomp.lib")

namespace termcore {
    void positionImeWindow(HWND hwnd, int x, int y, int height);
}

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
            if (wParam == VK_UP) {
                if (state->controller) state->controller->onSearchHistoryPrev();
                return 0;
            }
            if (wParam == VK_DOWN) {
                if (state->controller) state->controller->onSearchHistoryNext();
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

    // Create D3D11 device with BGRA support (needed for DirectComposition)
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
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

    // Get initial window client size for swap chain
    RECT clientRect = {};
    GetClientRect(hWnd, &clientRect);
    UINT initWidth = (std::max)(1u, static_cast<UINT>(clientRect.right - clientRect.left));
    UINT initHeight = (std::max)(1u, static_cast<UINT>(clientRect.bottom - clientRect.top));

    // Try composition swap chain (per-pixel alpha for background transparency).
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = initWidth;
    desc.Height = initHeight;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    hr = factory->CreateSwapChainForComposition(
        device.Get(), &desc, nullptr,
        swapChain.GetAddressOf());

    if (SUCCEEDED(hr)) {
        // Set up DirectComposition visual tree
        hr = DCompositionCreateDevice(dxgiDevice.Get(),
            IID_PPV_ARGS(dcompDevice.GetAddressOf()));
        if (SUCCEEDED(hr))
            hr = dcompDevice->CreateTargetForHwnd(hWnd, TRUE,
                dcompTarget.GetAddressOf());
        if (SUCCEEDED(hr))
            hr = dcompDevice->CreateVisual(dcompVisual.GetAddressOf());
        if (SUCCEEDED(hr))
            hr = dcompVisual->SetContent(swapChain.Get());
        if (SUCCEEDED(hr))
            hr = dcompTarget->SetRoot(dcompVisual.Get());
        if (SUCCEEDED(hr))
            hr = dcompDevice->Commit();

        useComposition = SUCCEEDED(hr);
    }

    if (!useComposition) {
        // Fallback: standard hwnd swap chain (no per-pixel alpha)
        OutputDebugStringW(L"[BreadTerminal] DirectComposition failed, falling back to hwnd swap chain\n");
        desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        desc.Width = 0;  // auto-size from hwnd
        desc.Height = 0;
        swapChain.Reset();
        dcompDevice.Reset(); dcompTarget.Reset(); dcompVisual.Reset();
        hr = factory->CreateSwapChainForHwnd(
            device.Get(), hWnd, &desc, nullptr, nullptr,
            swapChain.GetAddressOf());
        if (FAILED(hr)) return false;
    } else {
        OutputDebugStringW(L"[BreadTerminal] DirectComposition active - per-pixel alpha enabled\n");
        SetWindowTextW(hWnd, L"BreadTerminal");
    }

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

    // Wire notification ring trigger
    notifications->setCallback([this](const termcore::Notification& n) {
        auto& ring = pane_ring_states_[n.pane_id];
        ring.intensity = 1.0f;
        ring.color = (n.urgency == termcore::NotificationUrgency::Critical)
                     ? 0xEF4444u : 0x3B82F6u;
        ring.triggered = std::chrono::steady_clock::now();
    });

    // Wire agent state change ring trigger
    agentTracker->setStateCallback([this](uint32_t pane_id, const termcore::AgentInfo& info) {
        auto& ring = pane_ring_states_[pane_id];
        switch (info.state) {
            case termcore::AgentState::Waiting:
            case termcore::AgentState::NeedsInput:
                ring.intensity = 1.0f; ring.color = 0x3B82F6u; break;
            case termcore::AgentState::Error:
                ring.intensity = 1.0f; ring.color = 0xEF4444u; break;
            case termcore::AgentState::Exited:
                ring.intensity = 0.8f; ring.color = 0x22C55Eu; break;
            default: return;
        }
        ring.triggered = std::chrono::steady_clock::now();
    });

    // Create controller with this as the IPlatformHost
    controller = std::make_unique<termcore::TerminalController>(
        this, std::move(config), fontCollection.get());
    controller->initTerminal();

    // Initialize UI Automation accessibility provider
    accessibilityProvider = new TerminalAccessibilityProvider(hwnd);
    accessibilityProvider->setScreen(controller->activeScreen());
    accessibilityProvider->setSelection(&controller->selection());
    accessibilityProvider->setCellSize(
        controller->cellWidth(), controller->cellHeight());

    updateTabBar();
    needsRender = true;
}

// --- PTY / rendering ---

void TerminalWindowState::pollPty() {
    if (!controller) return;

    controller->pollPty();
    if (controller->needsRender()) {
        needsRender = true;
        controller->flushPendingUrlScan();
        controller->clearNeedsRender();
        if (renderer) renderer->markContentDirty();

        // Notify screen readers of content change
        if (accessibilityProvider) {
            accessibilityProvider->setScreen(controller->activeScreen());
            accessibilityProvider->notifyTextChanged();
        }
    }
}

void TerminalWindowState::renderFrame() {
    if (!renderer || !controller) return;

    termcore::Screen* screen = controller->activeScreen();
    if (!screen) return;

    // Decay notification ring intensities
    {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last_frame_time_).count();
        if (dt > 0.5f) dt = 0.016f; // clamp on first frame or long pauses
        last_frame_time_ = now;

        bool anyRingActive = false;
        for (auto& [pane, ring] : pane_ring_states_) {
            if (ring.intensity > 0.0f) {
                ring.intensity -= dt * 0.33f;
                if (ring.intensity < 0.0f) ring.intensity = 0.0f;
                if (ring.intensity > 0.0f) anyRingActive = true;
            }
        }
        if (anyRingActive) needsRender = true;
    }

    // Update tab bar, sidebar, and selection on renderer
    updateTabBar();
    updateSidebar();
    updateRendererSelection();

    // Update command palette state on renderer
    updateCommandPalette();

    // Update profile dropdown state on renderer
    updateProfileDropdown();

    // Update search highlights on renderer
    if (controller->search().isActive()) {
        const auto& matches = controller->search().search().matches();
        int currentIdx = controller->search().currentMatch();
        std::vector<termcore::D3DTextRenderer::SearchHighlight> highlights;
        highlights.reserve(matches.size());
        for (const auto& m : matches) {
            highlights.push_back({m.row, m.start_col, m.end_col});
        }
        renderer->setSearchHighlights(highlights, currentIdx);
    } else {
        renderer->setSearchHighlights({}, -1);
    }

    // Update URL highlights on renderer
    {
        auto hints = controller->urlHighlight().getRenderHints();
        uint32_t urlColor = controller->urlHighlight().urlColor();
        std::vector<termcore::D3DTextRenderer::UrlHighlight> urlHighlights;
        urlHighlights.reserve(hints.size());
        for (const auto& h : hints) {
            urlHighlights.push_back({h.row, h.start_col, h.end_col, h.hovered, urlColor});
        }
        renderer->setUrlHighlights(urlHighlights);
    }

    // Update background opacity on renderer.
    {
        const auto& cfg = controller->config();
        renderer->setBackgroundOpacity(cfg.background_opacity);
    }

    // Ghost text for autocomplete
    {
        auto& cm = controller->completionManager();
        if (cm.hasGhostText()) {
            renderer->setGhostText(cm.ghostText(),
                                    screen->cursorRow(),
                                    screen->cursorCol());
        } else {
            renderer->setGhostText("", -1, -1);
        }
    }

    // Hide cursor during IME composition
    bool imeComposing = !imeCompositionText.empty();
    renderer->setIMEActive(imeComposing);

    // Inject IME composition (preedit) text into screen cells before render
    struct IMESavedCell { int row, col; termcore::TermCell cell; };
    std::vector<IMESavedCell> imeSaved;

    if (imeComposing) {
        int curRow = screen->cursorRow();
        int curCol = screen->cursorCol();
        int cols = screen->cols();
        int rows = screen->rows();
        if (curRow >= 0 && curRow < rows) {
            int col = curCol;
            for (size_t i = 0; i < imeCompositionText.size(); ++i) {
                if (col >= cols) break;

                wchar_t ch = imeCompositionText[i];
                char32_t cp = static_cast<char32_t>(ch);
                // Handle surrogate pairs
                if (i + 1 < imeCompositionText.size() &&
                    ch >= 0xD800 && ch <= 0xDBFF) {
                    wchar_t lo = imeCompositionText[i + 1];
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((ch - 0xD800) << 10) + (lo - 0xDC00);
                        ++i;
                    }
                }

                int w = termcore::codepoint_width(cp);
                if (w < 1) w = 1;
                if (col + w > cols) break;

                // Save original cells
                for (int c = col; c < col + w; ++c) {
                    const termcore::TermCell& orig = screen->cellAt(curRow, c);
                    imeSaved.push_back({curRow, c, orig});
                }

                // Write IME character with inverted colors (fg/bg swapped)
                termcore::TermCell& cell = screen->mutableCellAt(curRow, col);
                cell.codepoint = cp;
                cell.fg_color = screen->dynamicColors().background;
                cell.bg_color = screen->dynamicColors().foreground;
                cell.attributes = 0;
                cell.width = w;

                if (w == 2 && col + 1 < cols) {
                    termcore::TermCell& cont = screen->mutableCellAt(curRow, col + 1);
                    cont.codepoint = 0;
                    cont.fg_color = cell.fg_color;
                    cont.bg_color = cell.bg_color;
                    cont.attributes = 0;
                    cont.width = 0;
                }

                col += w;
            }
        }
    }

    renderer->render(*screen);

    // Restore original cells after render
    for (const auto& sc : imeSaved) {
        termcore::TermCell& cell = screen->mutableCellAt(sc.row, sc.col);
        cell = sc.cell;
    }

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

    // Tab bar uses same background as terminal area
    tabInfo.bg_color = config.background;
    tabInfo.active_bg_color = config.background;
    tabInfo.inactive_bg_color = config.background;
    tabInfo.fg_color = config.foreground;
    tabInfo.accent_color = config.palette[4] ? config.palette[4] : 0x007acc;
    tabInfo.process_icon_map = &config.tab_process_icons;

    for (size_t i = 0; i < tabs.size(); ++i) {
        D3DTextRenderer::TabInfo ti;
        ti.title = tabs[i].title;
        ti.icon_name = tabs[i].icon_name;
        ti.process_name = tabs[i].process_name;
        ti.active = tabs[i].active;
        ti.has_unread = tabs[i].has_unread;
        ti.needs_attention = tabs[i].needs_attention;
        ti.agent_state = static_cast<int>(tabs[i].agent_state);
        ti.progress_value = tabs[i].progress_value;
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

void TerminalWindowState::updateProfileDropdown() {
    if (!renderer || !controller) return;

    auto& pd = controller->profileDropdown();
    D3DTextRenderer::ProfileDropdownInfo info;
    info.visible = pd.isOpen();

    if (info.visible) {
        info.selectedIndex = pd.selectedIndex();

        const auto& items = pd.items();
        int maxItems = (std::min)(static_cast<int>(items.size()),
                                  termcore::ProfileDropdown::kMaxVisibleItems);
        for (int i = 0; i < maxItems; ++i) {
            D3DTextRenderer::ProfileDropdownInfo::Item item;
            item.name = items[i].name;
            item.icon = items[i].icon;
            info.items.push_back(std::move(item));
        }
    }

    renderer->setProfileDropdown(info);
}

void TerminalWindowState::updateSidebar() {
    if (!renderer || !controller) return;

    const auto& config = controller->config();
    termcore::D3DTextRenderer::SidebarRenderInfo info;
    info.visible = config.sidebar_visible;
    info.width = config.sidebar_width > 0 ? config.sidebar_width : 220;

    if (!info.visible) {
        renderer->setSidebar(info);
        return;
    }

    // Style from config colors
    info.bg_color = config.background;
    info.fg_color = config.foreground;
    info.accent_color = config.palette[4] ? config.palette[4] : 0x007acc;
    // Separator is a blend toward foreground
    {
        uint32_t bg = config.background;
        uint32_t fg = config.foreground;
        auto blend = [](uint32_t base, uint32_t target, float t) -> uint32_t {
            int bR = (base >> 16) & 0xFF, bG = (base >> 8) & 0xFF, bB = base & 0xFF;
            int tR = (target >> 16) & 0xFF, tG = (target >> 8) & 0xFF, tB = target & 0xFF;
            return ((uint32_t)(bR + (tR - bR) * t) << 16) |
                   ((uint32_t)(bG + (tG - bG) * t) << 8) |
                    (uint32_t)(bB + (tB - bB) * t);
        };
        info.separator_color = blend(bg, fg, 0.15f);
    }

    // Build entries from tab controller data
    auto tabs = controller->tabBarInfo();
    for (size_t i = 0; i < tabs.size(); ++i) {
        termcore::D3DTextRenderer::SidebarRenderEntry entry;
        entry.pane_id = static_cast<uint32_t>(i);
        entry.title = tabs[i].title;
        entry.active = tabs[i].active;
        entry.has_unread = tabs[i].has_unread;

        // Agent state from tab info if available
        entry.agent_state = static_cast<int>(tabs[i].agent_state);

        info.entries.push_back(std::move(entry));
    }

    renderer->setSidebar(info);
}

bool TerminalWindowState::handleSidebarClick(int x, int y) {
    if (!controller || !renderer) return false;
    const auto& config = controller->config();
    if (!config.sidebar_visible) return false;

    int sidebarW = config.sidebar_width > 0 ? config.sidebar_width : 220;
    if (x >= sidebarW) return false;

    // Determine which entry was clicked based on Y position
    // For now, use the tab index as a simple mapping
    auto tabs = controller->tabBarInfo();
    float cellH = controller->cellHeight();
    float tabBarH = cellH * termcore::D3DTextRenderer::kTabBarHeightScale;
    float topY = (tabs.size() > 1) ? tabBarH : 0.0f;
    float entryH = cellH * 2.5f; // approximate entry height

    int entryIdx = static_cast<int>((y - topY) / entryH);
    if (entryIdx >= 0 && entryIdx < static_cast<int>(tabs.size())) {
        controller->tabs()->switchToTab(entryIdx);
        needsRender = true;
    }
    return true;
}

void TerminalWindowState::handleSidebarHover(int x, int y) {
    // Basic hover tracking - could be expanded later
    (void)x; (void)y;
}

void TerminalWindowState::handleSidebarWheel(int delta) {
    // Could scroll sidebar if many entries
    (void)delta;
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

// ---------------------------------------------------------------------------
// Background blur: DWMWA_SYSTEMBACKDROP_TYPE (Win11) + SetWindowCompositionAttribute fallback
// ---------------------------------------------------------------------------

// Win11 22621+ documented API (DWMWA_SYSTEMBACKDROP_TYPE = attribute 38)
// NOTE: Do NOT use DwmExtendFrameIntoClientArea — it creates an opaque DWM
// surface that blocks our DirectComposition swap chain transparency.
static bool tryDwmSystemBackdrop(HWND hwnd, bool enable) {
    // DWMSBT_NONE = 1, DWMSBT_TRANSIENTWINDOW = 3 (Desktop Acrylic)
    int backdrop = enable ? 3 : 1;
    HRESULT hr = DwmSetWindowAttribute(hwnd, 38, &backdrop, sizeof(backdrop));
    if (SUCCEEDED(hr)) {
        return true;
    }
    return false;
}

// Win10 fallback: undocumented SetWindowCompositionAttribute
namespace {

enum ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
};

struct ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
};

struct WINDOWCOMPOSITIONATTRIBDATA {
    DWORD Attrib;
    PVOID pvData;
    SIZE_T cbData;
};

constexpr DWORD WCA_ACCENT_POLICY = 19;

using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

SetWindowCompositionAttributeFn getSetWindowCompositionAttribute() {
    static auto fn = reinterpret_cast<SetWindowCompositionAttributeFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
    return fn;
}

} // anonymous namespace

void TerminalWindowState::applyBackgroundBlur(HWND hwnd) {
    const auto& config = controller ? controller->config() : termcore::Config{};
    // DWM blur only applies in "acrylic" mode.
    bool wantAcrylicBlur = config.background_blur_mode == "acrylic";

    // Skip redundant calls — blur on/off state didn't change
    static bool currentBlurState = false;
    if (wantAcrylicBlur == currentBlurState) return;
    currentBlurState = wantAcrylicBlur;

    // When disabling, explicitly reset both APIs to clear any prior state
    if (!wantAcrylicBlur) {
        tryDwmSystemBackdrop(hwnd, false);
        auto setWCA = getSetWindowCompositionAttribute();
        if (setWCA) {
            ACCENT_POLICY accent = {};
            accent.AccentState = ACCENT_DISABLED;
            WINDOWCOMPOSITIONATTRIBDATA data = {};
            data.Attrib = WCA_ACCENT_POLICY;
            data.pvData = &accent;
            data.cbData = sizeof(accent);
            setWCA(hwnd, &data);
        }
        return;
    }

    // Try Win11 documented API first
    if (tryDwmSystemBackdrop(hwnd, true)) {
        return;
    }

    // Fallback: SetWindowCompositionAttribute (Win10)
    auto setWCA = getSetWindowCompositionAttribute();
    if (!setWCA) return;

    ACCENT_POLICY accent = {};
    accent.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
    // Minimal tint alpha (0x01) — swap chain's backgroundOpacity controls visibility
    uint32_t bg = config.background;
    BYTE r = static_cast<BYTE>((bg >> 16) & 0xFF);
    BYTE g = static_cast<BYTE>((bg >> 8) & 0xFF);
    BYTE b = static_cast<BYTE>(bg & 0xFF);
    accent.GradientColor = (0x01u << 24)
                         | (static_cast<DWORD>(b) << 16)
                         | (static_cast<DWORD>(g) << 8)
                         | r;

    WINDOWCOMPOSITIONATTRIBDATA data = {};
    data.Attrib = WCA_ACCENT_POLICY;
    data.pvData = &accent;
    data.cbData = sizeof(accent);
    setWCA(hwnd, &data);
}

void TerminalWindowState::applyOpacity(HWND hwnd) {
    // Opacity is applied via premultiplied alpha in the composition swap chain.
    // The renderer's backgroundOpacity is set each frame in renderFrame().
    // Just mark dirty so next frame picks up the new value.
    needsRender = true;
    if (renderer) renderer->markContentDirty();
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

// --- IPlatformHost implementation (unique to this file) ---
// NOTE: Most IPlatformHost methods are in TerminalWindowStateEvents.cpp.
//       Only methods NOT in that file are defined here.

void TerminalWindowState::showClipboardHistory(
        const std::vector<termcore::ClipboardEntry>& entries) {
    if (entries.empty()) return;

    if (!clipboardHistoryPopup) {
        clipboardHistoryPopup = std::make_unique<ClipboardHistoryPopup>();
    }

    clipboardHistoryPopup->show(hwnd, entries,
        [this](const std::string& text) {
            if (controller) {
                controller->pasteText(text);
            }
        });
}

void TerminalWindowState::openUrl(const std::string& url) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    if (wlen > 0) {
        std::wstring wurl(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wurl.data(), wlen);
        ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOW);
    }
}

void TerminalWindowState::setMouseCursor(CursorType cursor) {
    HCURSOR hcur = LoadCursor(nullptr,
        cursor == CursorType::Hand ? IDC_HAND : IDC_ARROW);
    SetCursor(hcur);
}

// --- Accessibility ---

void TerminalWindowState::checkAccessibilitySettings() {
    if (!controller) return;

    const auto& config = controller->config();

    // High contrast detection
    bool hcNow = termcore::HighContrastDetector::isHighContrastEnabled();
    if (config.auto_detect_high_contrast) {
        if (hcNow && !accessibility.high_contrast) {
            // HC just turned on — save current theme and apply HC theme
            themeBeforeHighContrast = config.theme;
            auto sysColors =
                termcore::HighContrastDetector::getSystemColors();
            auto hcTheme =
                termcore::HighContrastDetector::buildThemeFromSystemColors(
                    sysColors);
            // Build a new config with the HC theme colors applied
            termcore::Config newConfig = config;
            termcore::applyTheme(newConfig, hcTheme);
            newConfig.theme = hcTheme.name;
            controller->onConfigChanged(newConfig);
            needsRender = true;
        } else if (!hcNow && accessibility.high_contrast) {
            // HC just turned off — restore previous theme
            if (!themeBeforeHighContrast.empty()) {
                controller->onThemeChanged(themeBeforeHighContrast);
            }
            needsRender = true;
        }
    }
    accessibility.high_contrast = hcNow;

    // Reduced motion detection
    bool reducedNow =
        termcore::HighContrastDetector::isReducedMotionEnabled();
    if (config.respect_reduced_motion) {
        accessibility.reduced_motion = reducedNow;
        accessibility.animation_speed_factor = reducedNow ? 0.0f : 1.0f;
    } else {
        accessibility.reduced_motion = false;
        accessibility.animation_speed_factor = 1.0f;
    }
}

#endif // _WIN32
