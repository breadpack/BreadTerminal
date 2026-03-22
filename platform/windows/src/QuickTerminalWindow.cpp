#if defined(_WIN32)

#include "QuickTerminalWindow.h"

#include <algorithm>
#include <cmath>
#include <windowsx.h>
#include <imm.h>

namespace termcore {
    void handleImeStartComposition(HWND hwnd, int cursor_x, int cursor_y, int cell_height);
    std::string handleImeComposition(HWND hwnd, LPARAM lParam);
    void handleImeEndComposition(HWND hwnd);
}

namespace {

const wchar_t* kQuickTermClassName = L"BreadQuickTerminalWindow";

/// EaseOutCubic: t = 1 - (1-t)^3
float easeOutCubic(float t) {
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

/// EaseInCubic: t = t^3
float easeInCubic(float t) {
    return t * t * t;
}

} // namespace

// --- Construction / Destruction ---

QuickTerminalWindow::QuickTerminalWindow() = default;

QuickTerminalWindow::~QuickTerminalWindow() {
    unregisterGlobalHotkey();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

// --- Window Procedure ---

LRESULT CALLBACK QuickTerminalWindow::WndProc(
        HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<QuickTerminalWindow*>(
        GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            auto* qw = reinterpret_cast<QuickTerminalWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(qw));
            return 0;
        }

        case WM_HOTKEY:
            if (self && wParam == kHotkeyId) {
                self->toggle();
            }
            return 0;

        case WM_TIMER:
            if (!self) break;
            if (wParam == kAnimTimerId) {
                self->onAnimationTick();
            } else if (wParam == kRenderTimerId) {
                if (self->state_) {
                    self->state_->pollPty();
                    if (self->state_->needsRender) {
                        self->state_->needsRender = false;
                        self->state_->renderFrame();
                    }
                }
            } else if (wParam == kCursorBlinkTimerId) {
                if (self->state_) {
                    self->state_->cursorBlinkOn = !self->state_->cursorBlinkOn;
                    if (self->state_->renderer) {
                        self->state_->renderer->setCursorBlink(
                            self->state_->cursorBlinkOn);
                    }
                    self->state_->needsRender = true;
                }
            }
            return 0;

        case WM_SIZE:
            if (self && self->state_ && wParam != SIZE_MINIMIZED) {
                int w = LOWORD(lParam);
                int h = HIWORD(lParam);
                self->state_->resizeSwapChain(w, h);
                self->state_->needsRender = false;
                self->state_->renderFrame();
            }
            return 0;

        case WM_ACTIVATE:
            if (self && LOWORD(wParam) == WA_INACTIVE) {
                if (self->config_.auto_hide_on_focus_loss &&
                    self->visible_ && !self->animating_) {
                    self->startHideAnimation();
                }
            }
            return 0;

        case WM_KEYDOWN:
            if (self && self->state_) {
                self->state_->handleKeyDown(wParam, lParam);
            }
            return 0;

        case WM_CHAR:
            if (self && self->state_) {
                self->state_->handleChar(wParam);
            }
            return 0;

        case WM_MOUSEWHEEL:
            if (self && self->state_) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(hWnd, &pt);
                self->state_->handleMouseWheel(delta, pt.x, pt.y);
            }
            return 0;

        case WM_LBUTTONDOWN:
            if (self && self->state_) {
                int mx = GET_X_LPARAM(lParam);
                int my = GET_Y_LPARAM(lParam);
                if (!self->state_->handleTabBarClick(mx, my)) {
                    self->state_->handleMouseDown(mx, my);
                }
            }
            return 0;

        case WM_MOUSEMOVE:
            if (self && self->state_) {
                int mx = GET_X_LPARAM(lParam);
                int my = GET_Y_LPARAM(lParam);
                self->state_->handleTabBarHover(mx, my);
                self->state_->handleMouseMove(mx, my);
            }
            return 0;

        case WM_LBUTTONUP:
            if (self && self->state_) {
                self->state_->handleMouseUp(
                    GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            return 0;

        case WM_LBUTTONDBLCLK:
            if (self && self->state_) {
                self->state_->handleDoubleClick(
                    GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            return 0;

        case WM_IME_STARTCOMPOSITION:
            if (self && self->state_ && self->state_->controller) {
                float cw = self->state_->controller->cellWidth();
                float ch = self->state_->controller->cellHeight();
                auto* scr = self->state_->controller->activeScreen();
                int cx = 0, cy = 0;
                if (scr) {
                    cx = static_cast<int>(scr->cursorCol() * cw);
                    cy = static_cast<int>(scr->cursorRow() * ch);
                }
                termcore::handleImeStartComposition(hWnd, cx, cy,
                                                     static_cast<int>(ch));
            }
            break;

        case WM_IME_COMPOSITION: {
            std::string result = termcore::handleImeComposition(hWnd, lParam);
            if (!result.empty() && self && self->state_ &&
                self->state_->controller) {
                self->state_->controller->onCharInput(result);
                if (self->state_->controller->needsRender()) {
                    self->state_->needsRender = true;
                    self->state_->controller->clearNeedsRender();
                }
            }
            return 0;
        }

        case WM_IME_ENDCOMPOSITION:
            termcore::handleImeEndComposition(hWnd);
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
            if (self && self->state_) self->state_->needsRender = true;
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_CLOSE:
            // Don't destroy; just hide
            if (self && self->visible_) {
                self->startHideAnimation();
            }
            return 0;

        case WM_DESTROY:
            KillTimer(hWnd, kAnimTimerId);
            KillTimer(hWnd, kRenderTimerId);
            KillTimer(hWnd, kCursorBlinkTimerId);
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// --- Initialization ---

bool QuickTerminalWindow::init(HINSTANCE hInstance,
                                const termcore::QuickTerminalConfig& config) {
    hInstance_ = hInstance;
    config_ = config;

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32513)); // IDC_IBEAM
    wc.lpszClassName = kQuickTermClassName;
    wc.hbrBackground = nullptr;

    if (!RegisterClassExW(&wc)) return false;

    // Create hidden popup window (no title bar, no borders, topmost)
    DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW; // TOOLWINDOW hides from taskbar
    DWORD style = WS_POPUP | WS_CLIPCHILDREN;

    // Start with a 1x1 hidden window; we size it properly on first toggle
    hwnd_ = CreateWindowExW(
        exStyle, kQuickTermClassName, L"BreadTerminal Quick",
        style,
        0, 0, 1, 1,
        nullptr, nullptr, hInstance, this);

    if (!hwnd_) return false;

    // Register global hotkey
    if (!registerGlobalHotkey()) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }

    return true;
}

int QuickTerminalWindow::run() {
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// --- Toggle ---

void QuickTerminalWindow::toggle() {
    if (animating_) return; // Ignore during animation

    if (visible_) {
        startHideAnimation();
    } else {
        ensureTerminalInit();
        startShowAnimation();
    }
}

// --- Animation ---

void QuickTerminalWindow::startShowAnimation() {
    targetRect_ = computeTargetRect();
    animShowing_ = true;
    animProgress_ = 0.0f;
    animating_ = true;
    visible_ = true;

    // Position at start of animation (off-screen)
    updateWindowPosition(0.0f);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);

    SetTimer(hwnd_, kAnimTimerId, kAnimIntervalMs, nullptr);
}

void QuickTerminalWindow::startHideAnimation() {
    animShowing_ = false;
    animProgress_ = 1.0f;
    animating_ = true;

    SetTimer(hwnd_, kAnimTimerId, kAnimIntervalMs, nullptr);
}

void QuickTerminalWindow::onAnimationTick() {
    float step = static_cast<float>(kAnimIntervalMs) /
                 static_cast<float>((std::max)(config_.animation_duration_ms, 1));

    if (animShowing_) {
        animProgress_ += step;
        if (animProgress_ >= 1.0f) {
            animProgress_ = 1.0f;
            animating_ = false;
            KillTimer(hwnd_, kAnimTimerId);
        }
        float eased = easeOutCubic(animProgress_);
        updateWindowPosition(eased);
    } else {
        animProgress_ -= step;
        if (animProgress_ <= 0.0f) {
            animProgress_ = 0.0f;
            animating_ = false;
            visible_ = false;
            KillTimer(hwnd_, kAnimTimerId);
            ShowWindow(hwnd_, SW_HIDE);
            return;
        }
        float eased = easeInCubic(animProgress_);
        updateWindowPosition(eased);
    }

    // Render during animation for smooth visuals
    if (state_ && state_->swapChain) {
        state_->renderFrame();
    }
}

void QuickTerminalWindow::updateWindowPosition(float progress) {
    int targetX = targetRect_.left;
    int targetY = targetRect_.top;
    int targetW = targetRect_.right - targetRect_.left;
    int targetH = targetRect_.bottom - targetRect_.top;

    int x = targetX, y = targetY;

    std::string pos = config_.position;
    if (pos == "bottom") {
        // Slide up from bottom
        int offscreenY = targetY + targetH;
        y = offscreenY + static_cast<int>((targetY - offscreenY) * progress);
    } else if (pos == "left") {
        // Slide right from left
        int offscreenX = targetX - targetW;
        x = offscreenX + static_cast<int>((targetX - offscreenX) * progress);
    } else if (pos == "right") {
        // Slide left from right
        int offscreenX = targetX + targetW;
        x = offscreenX + static_cast<int>((targetX - offscreenX) * progress);
    } else {
        // "top" (default): slide down from top
        int offscreenY = targetY - targetH;
        y = offscreenY + static_cast<int>((targetY - offscreenY) * progress);
    }

    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, targetW, targetH,
                 SWP_NOACTIVATE);
}

RECT QuickTerminalWindow::computeTargetRect() const {
    // Get monitor where the mouse cursor currently is
    POINT cursorPos;
    GetCursorPos(&cursorPos);
    HMONITOR hMon = MonitorFromPoint(cursorPos, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMon, &mi);

    RECT work = mi.rcWork; // Work area (excludes taskbar)
    int monW = work.right - work.left;
    int monH = work.bottom - work.top;

    RECT target;
    std::string pos = config_.position;

    if (pos == "bottom") {
        int h = static_cast<int>(monH * config_.height_percent);
        target.left = work.left;
        target.right = work.right;
        target.bottom = work.bottom;
        target.top = work.bottom - h;
    } else if (pos == "left") {
        int w = static_cast<int>(monW * config_.height_percent);
        target.left = work.left;
        target.right = work.left + w;
        target.top = work.top;
        target.bottom = work.bottom;
    } else if (pos == "right") {
        int w = static_cast<int>(monW * config_.height_percent);
        target.left = work.right - w;
        target.right = work.right;
        target.top = work.top;
        target.bottom = work.bottom;
    } else {
        // "top" (default)
        int h = static_cast<int>(monH * config_.height_percent);
        target.left = work.left;
        target.right = work.right;
        target.top = work.top;
        target.bottom = work.top + h;
    }

    return target;
}

// --- Hotkey ---

bool QuickTerminalWindow::registerGlobalHotkey() {
    auto parsed = termcore::parseHotkey(config_.hotkey);
    if (!parsed.valid()) return false;

    // Convert ParsedHotkey mods to Win32 MOD_ flags
    UINT winMods = MOD_NOREPEAT;
    if (parsed.mods & 1) winMods |= MOD_ALT;
    if (parsed.mods & 2) winMods |= MOD_CONTROL;
    if (parsed.mods & 4) winMods |= MOD_SHIFT;
    if (parsed.mods & 8) winMods |= MOD_WIN;

    return RegisterHotKey(hwnd_, kHotkeyId, winMods, parsed.vk) != 0;
}

void QuickTerminalWindow::unregisterGlobalHotkey() {
    if (hwnd_) {
        UnregisterHotKey(hwnd_, kHotkeyId);
    }
}

// --- Lazy Terminal Init ---

void QuickTerminalWindow::ensureTerminalInit() {
    if (terminalInitialized_) return;
    terminalInitialized_ = true;

    state_ = std::make_unique<TerminalWindowState>();

    if (!state_->initD3D(hwnd_)) {
        OutputDebugStringW(L"QuickTerminal: D3D init failed\n");
        return;
    }

    state_->initTerminal();
    state_->needsRender = true;

    // Start render and cursor blink timers
    SetTimer(hwnd_, kRenderTimerId, kRenderIntervalMs, nullptr);
    SetTimer(hwnd_, kCursorBlinkTimerId, kCursorBlinkIntervalMs, nullptr);
}

#endif // _WIN32
