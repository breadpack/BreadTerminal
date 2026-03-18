#if defined(_WIN32)

#include "DirectWriteRasterizer.h"
#include "DirectWriteDiscovery.h"
#include "D3DTextRenderer.h"

#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include "termcore/pty.h"
#include "termcore/font/font_collection.h"
#include "termcore/font/font_shaper.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/glyph_cache.h"

#include <algorithm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <memory>
#include <cmath>
#include <string>

using Microsoft::WRL::ComPtr;
using namespace termcore;

namespace {

// Timer ID for render tick (~60 fps)
constexpr UINT_PTR kRenderTimerId = 1;
constexpr UINT kRenderIntervalMs = 16;

// Window class name
const wchar_t* kWindowClassName = L"BreadTerminalWindow";

} // namespace

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

    // State
    float cellWidth = 8.0f;
    float cellHeight = 16.0f;
    int termRows = 24;
    int termCols = 80;
    bool needsRender = false;

    bool initD3D(HWND hWnd);
    void createRenderTarget();
    void destroyRenderTarget();
    void resizeSwapChain(int width, int height);
    void initTerminal();
    void startShell();
    void pollPty();
    void renderFrame();
    void handleKeyDown(WPARAM wParam, LPARAM lParam);
    void handleChar(WPARAM wParam);
    void sendPtyData(const char* data, size_t len);
};

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
    }
}

void TerminalWindowState::initTerminal() {
    // Font stack
    rasterizer = createDirectWriteRasterizer();
    discovery = createDirectWriteDiscovery();
    shaper = std::make_unique<FontShaper>();
    fontCollection = std::make_unique<FontCollection>(
        *rasterizer, *discovery, *shaper);
    fontCollection->setPrimaryFont("Consolas", 14.0f);
    atlas = std::make_unique<GlyphAtlas>();
    cache = std::make_unique<GlyphCache>();

    // Cell dimensions
    auto metrics = fontCollection->primaryMetrics();
    cellWidth = metrics.cell_width > 0 ? metrics.cell_width : 8.0f;
    cellHeight = metrics.cell_height > 0 ? metrics.cell_height : 16.0f;

    // Screen + parser
    screen = std::make_unique<Screen>(termRows, termCols);
    parser = std::make_unique<VtParser>(*screen);

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
}

void TerminalWindowState::startShell() {
    pty = termcore::createPty();
    if (!pty->spawn("", {}, "", termRows, termCols)) {
        OutputDebugStringW(L"BreadTerminal: failed to spawn shell\n");
    }
}

void TerminalWindowState::pollPty() {
    if (!pty || !pty->isAlive()) return;

    char buf[8192];
    int n = pty->read(buf, sizeof(buf));
    while (n > 0) {
        parser->feed(buf, static_cast<size_t>(n));
        needsRender = true;
        n = pty->read(buf, sizeof(buf));
    }
}

void TerminalWindowState::renderFrame() {
    if (!renderer || !screen) return;

    renderer->render(*screen);

    if (swapChain) {
        swapChain->Present(1, 0);
    }
}

void TerminalWindowState::sendPtyData(const char* data, size_t len) {
    if (pty && pty->isAlive()) {
        pty->write(data, len);
    }
}

void TerminalWindowState::handleKeyDown(WPARAM wParam, LPARAM /*lParam*/) {
    bool appCursor = screen && screen->appCursorKeys();
    const char* pfx = appCursor ? "\x1bO" : "\x1b[";

    switch (wParam) {
        case VK_UP:
            { char s[3]={pfx[0],pfx[1],'A'}; sendPtyData(s,3); } return;
        case VK_DOWN:
            { char s[3]={pfx[0],pfx[1],'B'}; sendPtyData(s,3); } return;
        case VK_RIGHT:
            { char s[3]={pfx[0],pfx[1],'C'}; sendPtyData(s,3); } return;
        case VK_LEFT:
            { char s[3]={pfx[0],pfx[1],'D'}; sendPtyData(s,3); } return;
        case VK_RETURN:
            sendPtyData("\r", 1); return;
        case VK_BACK:
            sendPtyData("\x7f", 1); return;
        case VK_TAB:
            sendPtyData("\t", 1); return;
        case VK_ESCAPE:
            sendPtyData("\x1b", 1); return;
        case VK_HOME:
            sendPtyData("\x1b[H", 3); return;
        case VK_END:
            sendPtyData("\x1b[F", 3); return;
        case VK_PRIOR:  // Page Up
            sendPtyData("\x1b[5~", 4); return;
        case VK_NEXT:   // Page Down
            sendPtyData("\x1b[6~", 4); return;
        case VK_DELETE:
            sendPtyData("\x1b[3~", 4); return;
        case VK_F1:
            sendPtyData("\x1bOP", 3); return;
        case VK_F2:
            sendPtyData("\x1bOQ", 3); return;
        case VK_F3:
            sendPtyData("\x1bOR", 3); return;
        case VK_F4:
            sendPtyData("\x1bOS", 3); return;
        default:
            break;
    }

    // Ctrl+key
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrl && wParam >= 'A' && wParam <= 'Z') {
        char c = static_cast<char>(wParam - 'A' + 1);
        sendPtyData(&c, 1);
    }
}

void TerminalWindowState::handleChar(WPARAM wParam) {
    // WM_CHAR gives us UTF-16 code units
    wchar_t wc = static_cast<wchar_t>(wParam);
    if (wc < 0x20 && wc != '\r' && wc != '\t') return;

    char utf8[4];
    int len = WideCharToMultiByte(CP_UTF8, 0, &wc, 1,
                                  utf8, sizeof(utf8),
                                  nullptr, nullptr);
    if (len > 0) {
        sendPtyData(utf8, static_cast<size_t>(len));
    }
}

// Window procedure
static LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<TerminalWindowState*>(
        GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            auto* newState =
                reinterpret_cast<TerminalWindowState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(newState));

            if (!newState->initD3D(hWnd)) {
                return -1;
            }
            newState->initTerminal();
            newState->startShell();

            SetTimer(hWnd, kRenderTimerId, kRenderIntervalMs, nullptr);
            return 0;
        }

        case WM_SIZE:
            if (state && wParam != SIZE_MINIMIZED) {
                int width = LOWORD(lParam);
                int height = HIWORD(lParam);
                state->resizeSwapChain(width, height);
            }
            return 0;

        case WM_TIMER:
            if (wParam == kRenderTimerId && state) {
                state->pollPty();
                if (state->needsRender) {
                    state->needsRender = false;
                    state->renderFrame();
                }
            }
            return 0;

        case WM_KEYDOWN:
            if (state) state->handleKeyDown(wParam, lParam);
            return 0;

        case WM_CHAR:
            if (state) state->handleChar(wParam);
            return 0;

        case WM_DESTROY:
            KillTimer(hWnd, kRenderTimerId);
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// Public API: create and show the terminal window, run message loop.
int runTerminalWindow(HINSTANCE hInstance, int nCmdShow) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32513)); // IDC_IBEAM
    wc.lpszClassName = kWindowClassName;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(
        GetStockObject(BLACK_BRUSH));

    if (!RegisterClassExW(&wc)) return 1;

    auto state = std::make_unique<TerminalWindowState>();

    HWND hwnd = CreateWindowExW(
        0, kWindowClassName, L"BreadTerminal",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, hInstance, state.get());

    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Message loop
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}

#endif // _WIN32
