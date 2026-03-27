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

// Render timer removed — rendering is driven by dedicated render thread.
// Cursor blink timer removed — render thread uses WaitForSingleObject timeout.

constexpr UINT_PTR kResizeOverlayTimerId = 3;
constexpr UINT kResizeOverlayCheckMs = 100;

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

            // Start dedicated render thread (Phase 2)
            newState->initRenderThread();
            newState->signalInvalidate();

            // Timer for resize overlay auto-hide check
            SetTimer(hWnd, kResizeOverlayTimerId, kResizeOverlayCheckMs, nullptr);
            return 0;
        }

        case WM_SETFOCUS:
            if (state && state->controller) {
                state->withWriteLock([&] {
                    state->controller->onFocusChange(true);
                });
            }
            return 0;

        case WM_KILLFOCUS:
            if (state && state->controller) {
                state->withWriteLock([&] {
                    state->controller->onFocusChange(false);
                });
            }
            return 0;

        case WM_ENTERSIZEMOVE:
            if (state) {
                state->withWriteLock([&] { state->inLiveResize = true; });
            }
            return 0;

        case WM_EXITSIZEMOVE:
            if (state) {
                state->withWriteLock([&] { state->inLiveResize = false; });
            }
            return 0;

        case WM_SIZE:
            if (state && wParam != SIZE_MINIMIZED) {
                int width = LOWORD(lParam);
                int height = HIWORD(lParam);

                // Pause render thread while resizing swap chain.
                // We temporarily stop the render thread, resize under
                // exclusive lock (no D3D contention), then restart.
                bool wasRunning = state->renderRunning_.load();
                if (wasRunning) {
                    state->stopRenderThread();
                }

                state->resizeSwapChain(width, height);
                state->repositionSearchBar();
                state->needsRender = false;

                // Restart render thread and immediately signal for a frame
                if (wasRunning) {
                    state->initRenderThread();
                    state->signalInvalidate();
                }
            }
            return 0;

        case WM_TIMER:
            if (wParam == kResizeOverlayTimerId && state) {
                // Auto-hide resize overlay after 1 second
                if (state->showResizeOverlay) {
                    auto elapsed = std::chrono::steady_clock::now()
                                   - state->resizeOverlayStart;
                    if (elapsed > std::chrono::seconds(1)) {
                        state->withWriteLock([&] {
                            state->showResizeOverlay = false;
                            if (state->renderer) state->renderer->markContentDirty();
                        });
                    }
                }
            }
            return 0;

        case WM_MOUSEWHEEL:
            if (state) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(hWnd, &pt);
                state->withWriteLock([&] {
                    state->handleMouseWheel(delta, pt.x, pt.y);
                });
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (state) {
                int mx = GET_X_LPARAM(lParam);
                int my = GET_Y_LPARAM(lParam);
                state->withWriteLock([&] {
                    if (!state->handleTabBarClick(mx, my)) {
                        state->handleMouseDown(mx, my);
                    }
                });
            }
            return 0;

        case WM_MOUSEMOVE:
            if (state) {
                int mx = GET_X_LPARAM(lParam);
                int my = GET_Y_LPARAM(lParam);
                state->withWriteLock([&] {
                    state->handleTabBarHover(mx, my);
                    state->handleMouseMove(mx, my);
                });
            }
            return 0;

        case WM_LBUTTONUP:
            if (state) {
                state->withWriteLock([&] {
                    state->handleMouseUp(
                        GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                });
            }
            return 0;

        case WM_LBUTTONDBLCLK:
            if (state) {
                state->withWriteLock([&] {
                    state->handleDoubleClick(
                        GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                });
            }
            return 0;

        case WM_COMMAND:
            if (state && HIWORD(wParam) == EN_CHANGE &&
                LOWORD(wParam) == TerminalWindowState::kSearchEditId) {
                // Get text from edit control and send to controller
                if (state->searchEditHwnd && state->controller) {
                    int len = GetWindowTextLengthW(state->searchEditHwnd);
                    std::string utf8Query;
                    if (len > 0) {
                        std::wstring wquery(len + 1, L'\0');
                        GetWindowTextW(state->searchEditHwnd, &wquery[0], len + 1);
                        wquery.resize(len);
                        int utf8Len = WideCharToMultiByte(CP_UTF8, 0,
                            wquery.c_str(), static_cast<int>(wquery.size()),
                            nullptr, 0, nullptr, nullptr);
                        utf8Query.resize(utf8Len);
                        WideCharToMultiByte(CP_UTF8, 0,
                            wquery.c_str(), static_cast<int>(wquery.size()),
                            &utf8Query[0], utf8Len, nullptr, nullptr);
                    }
                    state->withWriteLock([&] {
                        state->controller->onSearchQuery(utf8Query);
                        if (state->controller->needsRender()) {
                            state->needsRender = true;
                            state->controller->clearNeedsRender();
                            if (state->renderer) state->renderer->markContentDirty();
                        }
                    });
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
            if (state) {
                state->withWriteLock([&] {
                    state->handleKeyDown(wParam, lParam);
                });
            }
            return 0;

        case WM_CHAR:
            if (state) {
                state->withWriteLock([&] {
                    state->handleChar(wParam);
                });
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
            if (state) {
                state->withWriteLock([&] {
                    state->needsRender = true;
                    if (state->renderer) state->renderer->markContentDirty();
                });
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
                AcquireSRWLockShared(&state->renderLock_);
                float cw = state->controller->cellWidth();
                float ch = state->controller->cellHeight();
                termcore::Screen* scr = state->controller->activeScreen();
                int cursorX = 0, cursorY = 0;
                if (scr) {
                    cursorX = static_cast<int>(scr->cursorCol() * cw);
                    cursorY = static_cast<int>(scr->cursorRow() * ch);
                }
                ReleaseSRWLockShared(&state->renderLock_);
                termcore::handleImeStartComposition(hWnd, cursorX, cursorY,
                                                     static_cast<int>(ch));
            }
            break;

        case WM_IME_COMPOSITION: {
            // Capture composition (preedit) string for inline rendering
            std::wstring compText;
            if (state && (lParam & GCS_COMPSTR)) {
                HIMC imc = ImmGetContext(hWnd);
                if (imc) {
                    LONG bytes = ImmGetCompositionStringW(imc, GCS_COMPSTR, nullptr, 0);
                    if (bytes > 0) {
                        compText.resize(bytes / sizeof(wchar_t));
                        ImmGetCompositionStringW(imc, GCS_COMPSTR,
                            compText.data(), bytes);
                    }
                    ImmReleaseContext(hWnd, imc);
                }
            }

            std::string result = termcore::handleImeComposition(hWnd, lParam);

            if (state) {
                state->withWriteLock([&] {
                    if (!compText.empty()) {
                        state->imeCompositionText = compText;
                        state->needsRender = true;
                        if (state->renderer) state->renderer->markContentDirty();
                    } else if (lParam & GCS_COMPSTR) {
                        state->imeCompositionText.clear();
                    }

                    if (!result.empty() && state->controller) {
                        state->imeCompositionText.clear();
                        state->controller->onCharInput(result);
                        if (state->controller->needsRender()) {
                            state->needsRender = true;
                            state->controller->clearNeedsRender();
                            if (state->renderer) state->renderer->markContentDirty();
                        }
                    }
                });
            }
            return 0;
        }

        case WM_IME_ENDCOMPOSITION:
            if (state) {
                state->withWriteLock([&] {
                    state->imeCompositionText.clear();
                    state->needsRender = true;
                    if (state->renderer) state->renderer->markContentDirty();
                });
            }
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
            // Stop render thread before destroying resources
            if (state) state->stopRenderThread();
            KillTimer(hWnd, kResizeOverlayTimerId);
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

    // Event-driven message loop. Rendering is on the dedicated render thread.
    // Main thread handles input + PTY polling under exclusive lock.
    MSG msg = {};
    bool running = true;
    bool lastPollHadData = false;

    while (running) {
        // When data was flowing, poll immediately (waitMs=0).
        // Otherwise sleep up to 1ms to avoid busy-waiting.
        DWORD waitMs = lastPollHadData ? 0 : 1;

        MsgWaitForMultipleObjects(0, nullptr, FALSE, waitMs, QS_ALLINPUT);

        // 1. Process all pending Windows messages (input priority)
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;

        // 2. Poll PTY under exclusive lock (fast: reads data, sets needsRender)
        lastPollHadData = false;
        if (state->controller) {
            state->withWriteLock([&] {
                bool hadRender = state->needsRender;
                state->pollPty();
                if (!hadRender && state->needsRender) {
                    lastPollHadData = true;
                }
            });
        }
    }

    return static_cast<int>(msg.wParam);
}

#endif // _WIN32
