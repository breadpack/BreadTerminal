#if defined(_WIN32)

#include "TerminalWindowState.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <windowsx.h>
#include <imm.h>

namespace termcore {
    void handleImeStartComposition(HWND hwnd, int cursor_x, int cursor_y, int cell_height);
    std::string handleImeComposition(HWND hwnd, LPARAM lParam);
    void handleImeEndComposition(HWND hwnd);
    void positionImeWindow(HWND hwnd, int x, int y, int height);
}

namespace {

constexpr UINT_PTR kRenderTimerId = 1;
constexpr UINT kRenderIntervalMs = 16;

constexpr UINT_PTR kCursorBlinkTimerId = 2;
constexpr UINT kCursorBlinkIntervalMs = 500;

const wchar_t* kWindowClassName = L"BreadTerminalWindow";

} // namespace

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

            newState->needsRender = true;

            SetTimer(hWnd, kRenderTimerId, kRenderIntervalMs, nullptr);
            SetTimer(hWnd, kCursorBlinkTimerId,
                     kCursorBlinkIntervalMs, nullptr);
            return 0;
        }

        case WM_ENTERSIZEMOVE:
            if (state) state->inLiveResize = true;
            return 0;

        case WM_EXITSIZEMOVE:
            if (state) state->inLiveResize = false;
            return 0;

        case WM_SIZE:
            if (state && wParam != SIZE_MINIMIZED) {
                int width = LOWORD(lParam);
                int height = HIWORD(lParam);
                state->resizeSwapChain(width, height);
                state->repositionSearchBar();
                state->needsRender = false;
                state->renderFrame();
            }
            return 0;

        case WM_TIMER:
            if (wParam == kRenderTimerId && state) {
                state->pollPty();
                // Auto-hide resize overlay after 1 second
                if (state->showResizeOverlay) {
                    auto elapsed = std::chrono::steady_clock::now()
                                   - state->resizeOverlayStart;
                    if (elapsed > std::chrono::seconds(1)) {
                        state->showResizeOverlay = false;
                        state->needsRender = true;
                    }
                }
                if (state->needsRender) {
                    state->needsRender = false;
                    state->renderFrame();
                }
            } else if (wParam == kCursorBlinkTimerId && state) {
                state->cursorBlinkOn = !state->cursorBlinkOn;
                if (state->renderer) {
                    state->renderer->setCursorBlink(state->cursorBlinkOn);
                }
                state->needsRender = true;
            }
            return 0;

        case WM_MOUSEWHEEL:
            if (state && state->screen) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                int lines = (std::max)(1, std::abs(delta / WHEEL_DELTA) * 3);

                if (state->screen->mouseMode() != MouseMode::None) {
                    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    ScreenToClient(hWnd, &pt);
                    MouseEventType scrollType = delta > 0
                        ? MouseEventType::ScrollUp
                        : MouseEventType::ScrollDown;
                    MouseButton scrollBtn = delta > 0
                        ? MouseButton::ScrollUp
                        : MouseButton::ScrollDown;
                    for (int i = 0; i < lines; ++i) {
                        state->sendMouseEvent(scrollType, scrollBtn,
                                              pt.x, pt.y);
                    }
                } else {
                    if (delta > 0) {
                        state->screen->scrollViewportUp(lines);
                    } else {
                        state->screen->scrollViewportDown(lines);
                    }
                }
                state->needsRender = true;
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (state) state->handleMouseDown(
                GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_MOUSEMOVE:
            if (state) state->handleMouseMove(
                GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_LBUTTONUP:
            if (state) state->handleMouseUp(
                GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_LBUTTONDBLCLK:
            if (state) state->handleDoubleClick(
                GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_COMMAND:
            if (state && HIWORD(wParam) == EN_CHANGE &&
                LOWORD(wParam) == TerminalWindowState::kSearchEditId) {
                state->performSearch();
            }
            return 0;

        case WM_CTLCOLOREDIT:
            if (state && state->searchEditHwnd &&
                reinterpret_cast<HWND>(lParam) == state->searchEditHwnd) {
                HDC hdc = reinterpret_cast<HDC>(wParam);
                SetTextColor(hdc, RGB(220, 220, 220));
                SetBkColor(hdc, RGB(45, 45, 45));
                static HBRUSH searchBrush = CreateSolidBrush(RGB(45, 45, 45));
                return reinterpret_cast<LRESULT>(searchBrush);
            }
            break;

        case WM_KEYDOWN:
            if (state) state->handleKeyDown(wParam, lParam);
            return 0;

        case WM_CHAR:
            if (state) state->handleChar(wParam);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
            if (state) state->needsRender = true;
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_CLOSE:
            if (state && state->pty && state->pty->isAlive()) {
                int result = MessageBoxW(hWnd,
                    L"A process is still running. Close anyway?",
                    L"BreadTerminal",
                    MB_YESNO | MB_ICONWARNING);
                if (result != IDYES) return 0;
            }
            DestroyWindow(hWnd);
            return 0;

        case WM_IME_STARTCOMPOSITION:
            if (state && state->fontCollection) {
                auto m = state->fontCollection->primaryMetrics();
                int cursorX = 0, cursorY = 0;
                if (state->screen) {
                    cursorX = static_cast<int>(state->screen->cursorCol() * m.cell_width);
                    cursorY = static_cast<int>(state->screen->cursorRow() * m.cell_height);
                }
                termcore::handleImeStartComposition(hWnd, cursorX, cursorY,
                                                     static_cast<int>(m.cell_height));
            }
            break;

        case WM_IME_COMPOSITION: {
            std::string result = termcore::handleImeComposition(hWnd, lParam);
            if (!result.empty() && state) {
                state->sendPtyData(result.data(), result.size());
                if (state->screen && !state->screen->isViewportAtBottom()) {
                    state->screen->scrollViewportToBottom();
                }
                state->needsRender = true;
            }
            return 0;
        }

        case WM_IME_ENDCOMPOSITION:
            termcore::handleImeEndComposition(hWnd);
            break;

        case WM_DPICHANGED:
            if (state) {
                UINT dpi = HIWORD(wParam);
                const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
                state->handleDpiChange(hWnd, dpi, suggested);
            }
            return 0;

        case WM_DESTROY:
            KillTimer(hWnd, kRenderTimerId);
            KillTimer(hWnd, kCursorBlinkTimerId);
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// Public API: create and show the terminal window, run message loop.
int runTerminalWindow(HINSTANCE hInstance, int nCmdShow) {
    // Enable per-monitor DPI awareness
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32513)); // IDC_IBEAM
    wc.lpszClassName = kWindowClassName;
    wc.hbrBackground = nullptr;

    if (!RegisterClassExW(&wc)) return 1;

    auto state = std::make_unique<TerminalWindowState>();

    // Pre-load config for window dimensions and opacity
    std::string configPath = termcore::defaultConfigPath();
    if (!configPath.empty()) {
        state->config = termcore::parseConfigFile(configPath);
    }

    int winW = state->config.window_width > 0 ? state->config.window_width : 800;
    int winH = state->config.window_height > 0 ? state->config.window_height : 600;

    DWORD exStyle = 0;
    if (state->config.background_opacity < 1.0f) {
        exStyle |= WS_EX_LAYERED;
    }

    HWND hwnd = CreateWindowExW(
        exStyle, kWindowClassName, L"BreadTerminal",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, winW, winH,
        nullptr, nullptr, hInstance, state.get());

    if (!hwnd) return 1;

    if (state->config.background_opacity < 1.0f) {
        BYTE alpha = static_cast<BYTE>(state->config.background_opacity * 255.0f);
        SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
    }

    state->applyBackgroundBlur(hwnd);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}

#endif // _WIN32
