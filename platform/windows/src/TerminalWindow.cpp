#if defined(_WIN32)

#include "TerminalWindowState.h"
#include "TerminalAccessibility.h"
#include "HighContrastDetector.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <windowsx.h>
#include <shellapi.h>
#include <imm.h>
#include <UIAutomationCore.h>
#include <UIAutomationCoreApi.h>

namespace termcore {
    void handleImeStartComposition(HWND hwnd, int cursor_x, int cursor_y, int cell_height);
    std::string handleImeComposition(HWND hwnd, LPARAM lParam);
    void handleImeEndComposition(HWND hwnd);
    void positionImeWindow(HWND hwnd, int x, int y, int height);

    // Notification helpers (TerminalWindowNotify.cpp)
    void removeNotificationIcon(HWND hwnd);
}

namespace {

// Must match kNotifyCallbackMsg in TerminalWindowNotify.cpp
constexpr UINT kNotifyCallbackMsg = WM_APP + 100;

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
            newState->checkAccessibilitySettings();

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
                        if (state->renderer) state->renderer->markContentDirty();
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
            if (state) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(hWnd, &pt);
                state->handleMouseWheel(delta, pt.x, pt.y);
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (state) {
                int mx = GET_X_LPARAM(lParam);
                int my = GET_Y_LPARAM(lParam);
                // Check if click is in tab bar area
                if (!state->handleTabBarClick(mx, my)) {
                    state->handleMouseDown(mx, my);
                }
            }
            return 0;

        case WM_MOUSEMOVE:
            if (state) {
                int mx = GET_X_LPARAM(lParam);
                int my = GET_Y_LPARAM(lParam);
                state->handleTabBarHover(mx, my);
                state->handleMouseMove(mx, my);
            }
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
                // Get text from edit control and send to controller
                if (state->searchEditHwnd && state->controller) {
                    int len = GetWindowTextLengthW(state->searchEditHwnd);
                    if (len > 0) {
                        std::wstring wquery(len + 1, L'\0');
                        GetWindowTextW(state->searchEditHwnd, &wquery[0], len + 1);
                        wquery.resize(len);
                        // Convert to UTF-8
                        int utf8Len = WideCharToMultiByte(CP_UTF8, 0,
                            wquery.c_str(), static_cast<int>(wquery.size()),
                            nullptr, 0, nullptr, nullptr);
                        std::string utf8Query(utf8Len, '\0');
                        WideCharToMultiByte(CP_UTF8, 0,
                            wquery.c_str(), static_cast<int>(wquery.size()),
                            &utf8Query[0], utf8Len, nullptr, nullptr);
                        state->controller->onSearchQuery(utf8Query);
                    } else {
                        state->controller->onSearchQuery("");
                    }
                    if (state->controller->needsRender()) {
                        state->needsRender = true;
                        state->controller->clearNeedsRender();
                        if (state->renderer) state->renderer->markContentDirty();
                    }
                }
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
            if (state) {
                state->needsRender = true;
                if (state->renderer) state->renderer->markContentDirty();
            }
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_CLOSE:
            if (state && state->controller &&
                state->controller->tabs() &&
                state->controller->tabs()->hasAnyAlivePty()) {
                int result = MessageBoxW(hWnd,
                    L"A process is still running. Close anyway?",
                    L"BreadTerminal",
                    MB_YESNO | MB_ICONWARNING);
                if (result != IDYES) return 0;
            }
            DestroyWindow(hWnd);
            return 0;

        case WM_IME_STARTCOMPOSITION:
            if (state && state->controller) {
                float cw = state->controller->cellWidth();
                float ch = state->controller->cellHeight();
                termcore::Screen* scr = state->controller->activeScreen();
                int cursorX = 0, cursorY = 0;
                if (scr) {
                    cursorX = static_cast<int>(scr->cursorCol() * cw);
                    cursorY = static_cast<int>(scr->cursorRow() * ch);
                }
                termcore::handleImeStartComposition(hWnd, cursorX, cursorY,
                                                     static_cast<int>(ch));
            }
            break;

        case WM_IME_COMPOSITION: {
            std::string result = termcore::handleImeComposition(hWnd, lParam);
            if (!result.empty() && state && state->controller) {
                state->controller->onCharInput(result);
                if (state->controller->needsRender()) {
                    state->needsRender = true;
                    state->controller->clearNeedsRender();
                    if (state->renderer) state->renderer->markContentDirty();
                }
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

        case WM_GETOBJECT:
            if (state && state->accessibilityProvider &&
                static_cast<long>(lParam) == UiaRootObjectId) {
                return UiaReturnRawElementProvider(
                    hWnd, wParam, lParam,
                    static_cast<IRawElementProviderSimple*>(
                        state->accessibilityProvider));
            }
            break;

        case WM_DESTROY:
            KillTimer(hWnd, kRenderTimerId);
            KillTimer(hWnd, kCursorBlinkTimerId);
            if (state && state->notifyIconAdded) {
                termcore::removeNotificationIcon(hWnd);
                state->notifyIconAdded = false;
            }
            if (state && state->accessibilityProvider) {
                UiaReturnRawElementProvider(hWnd, 0, 0, nullptr);
                state->accessibilityProvider->Release();
                state->accessibilityProvider = nullptr;
            }
            PostQuitMessage(0);
            return 0;

        case WM_NCACTIVATE:
        case WM_NCPAINT: {
            // Let DWM paint the default frame first
            LRESULT res = DefWindowProcW(hWnd, msg, wParam, lParam);
            // Draw the app icon in the title bar caption area
            {
                HDC hdc = GetWindowDC(hWnd);
                if (hdc) {
                    // Get frame metrics
                    int frameX = GetSystemMetrics(SM_CXFRAME) +
                                 GetSystemMetrics(SM_CXPADDEDBORDER);
                    int frameY = GetSystemMetrics(SM_CYFRAME) +
                                 GetSystemMetrics(SM_CXPADDEDBORDER);
                    int captionH = GetSystemMetrics(SM_CYCAPTION);
                    int iconSize = GetSystemMetrics(SM_CXSMICON);
                    int iconX = frameX + 4;
                    int iconY = frameY + (captionH - iconSize) / 2;
                    HICON hIcon = reinterpret_cast<HICON>(
                        SendMessageW(hWnd, WM_GETICON, ICON_SMALL, 0));
                    if (!hIcon) {
                        hIcon = reinterpret_cast<HICON>(
                            GetClassLongPtrW(hWnd, GCLP_HICONSM));
                    }
                    if (hIcon) {
                        DrawIconEx(hdc, iconX, iconY, hIcon,
                                   iconSize, iconSize, 0, nullptr, DI_NORMAL);
                    }
                    ReleaseDC(hWnd, hdc);
                }
            }
            return res;
        }

        case WM_THEMECHANGED:
            if (state) {
                state->checkAccessibilitySettings();
            }
            return 0;

        case WM_SETTINGCHANGE:
            if (state) {
                if (wParam == SPI_SETHIGHCONTRAST ||
                    wParam == SPI_SETCLIENTAREAANIMATION) {
                    state->checkAccessibilitySettings();
                }
            }
            return 0;

        default:
            // Handle notification tray icon callback
            if (msg == kNotifyCallbackMsg && state) {
                UINT event = LOWORD(lParam);
                if (event == NIN_BALLOONUSERCLICK ||
                    event == WM_LBUTTONUP) {
                    // Bring the terminal window to the foreground
                    if (IsIconic(hWnd)) {
                        ShowWindow(hWnd, SW_RESTORE);
                    }
                    SetForegroundWindow(hWnd);
                }
                return 0;
            }
            break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// Public API: create and show the terminal window, run message loop.
int runTerminalWindow(HINSTANCE hInstance, int nCmdShow) {
    // Enable per-monitor DPI awareness
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Load icons at proper sizes for DPI-aware display
    HICON hIconLarge = static_cast<HICON>(LoadImageW(
        hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR));
    HICON hIconSmall = static_cast<HICON>(LoadImageW(
        hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR));

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hIcon = hIconLarge;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32513)); // IDC_IBEAM
    wc.lpszClassName = kWindowClassName;
    wc.hbrBackground = nullptr;
    wc.hIconSm = hIconSmall;

    if (!RegisterClassExW(&wc)) return 1;

    auto state = std::make_unique<TerminalWindowState>();

    // Pre-load config for window dimensions
    termcore::Config preConfig = termcore::loadConfig();

    int winW = preConfig.window_width > 0 ? preConfig.window_width : 800;
    int winH = preConfig.window_height > 0 ? preConfig.window_height : 600;

    // WS_EX_NOREDIRECTIONBITMAP: required for DirectComposition per-pixel alpha.
    // Without it, DWM allocates an opaque redirection surface that hides transparency.
    HWND hwnd = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP, kWindowClassName, L"BreadTerminal",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, winW, winH,
        nullptr, nullptr, hInstance, state.get());

    if (!hwnd) return 1;

    // Explicitly set icons on the window for title bar / taskbar / Alt+Tab
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIconLarge));
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIconSmall));

    state->applyTitleBarTheme(hwnd);
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
