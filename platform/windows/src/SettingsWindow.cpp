#if defined(_WIN32)

#include "SettingsWindow.h"

#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace termcore {

const wchar_t* SettingsWindow::kClassName = L"BreadTerminal_Settings";
bool SettingsWindow::sClassRegistered = false;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

SettingsWindow::SettingsWindow() {
    Gdiplus::GdiplusStartupInput si;
    Gdiplus::Status st = Gdiplus::GdiplusStartup(&gdiplusToken_, &si, nullptr);
    gdiplusOwned_ = (st == Gdiplus::Ok);
}

SettingsWindow::~SettingsWindow() {
    close();
    if (editFont_) { DeleteObject(editFont_); editFont_ = nullptr; }
    if (gdiplusOwned_ && gdiplusToken_) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
    }
}

void SettingsWindow::setConfig(const Config& config) {
    config_ = config;
    chrome_ = deriveChrome(config.background, config.foreground, config.palette);
    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void SettingsWindow::setSaveCallback(SaveCallback cb) {
    saveCallback_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// Window class registration
// ---------------------------------------------------------------------------

void SettingsWindow::registerWindowClass(HINSTANCE hInstance) {
    if (sClassRegistered) return;
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = SettingsWindow::WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);
    sClassRegistered = true;
}

// ---------------------------------------------------------------------------
// Show / Close
// ---------------------------------------------------------------------------

void SettingsWindow::show(HWND parent) {
    if (hwnd_) { SetForegroundWindow(hwnd_); return; }
    parentHwnd_ = parent;

    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE);
    registerWindowClass(hInst);

    // Center on parent
    RECT pr;
    GetWindowRect(parent, &pr);
    int cx = (pr.left + pr.right) / 2 - kSetWinWidth / 2;
    int cy = (pr.top + pr.bottom) / 2 - kSetWinHeight / 2;

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kClassName, L"Settings",
        WS_POPUP | WS_CLIPCHILDREN,
        cx, cy, kSetWinWidth, kSetWinHeight,
        parent, nullptr, hInst, this);

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
}

void SettingsWindow::close() {
    destroyTextEdit();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

Gdiplus::Color SettingsWindow::toGdipColor(uint32_t rgb, BYTE a) const {
    return Gdiplus::Color(a,
        (BYTE)((rgb >> 16) & 0xFF),
        (BYTE)((rgb >> 8) & 0xFF),
        (BYTE)(rgb & 0xFF));
}

Gdiplus::Color SettingsWindow::toGdipColorCR(COLORREF cr, BYTE a) const {
    return Gdiplus::Color(a, GetRValue(cr), GetGValue(cr), GetBValue(cr));
}

std::wstring SettingsWindow::toWide(const std::string& s) const {
    if (s.empty()) return {};
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(sz - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), sz);
    return w;
}

std::string SettingsWindow::toUtf8(const std::wstring& s) const {
    if (s.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1,
                                 nullptr, 0, nullptr, nullptr);
    std::string u(sz - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1,
                        u.data(), sz, nullptr, nullptr);
    return u;
}

void SettingsWindow::notifySave() {
    if (saveCallback_) saveCallback_(config_);
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg,
                                          WPARAM wParam, LPARAM lParam) {
    SettingsWindow* self = nullptr;
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<SettingsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = reinterpret_cast<SettingsWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT SettingsWindow::handleMessage(HWND hwnd, UINT msg,
                                       WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        return 0;

    case WM_PAINT:
        paintWindow(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, chrome_.textColor);
        SetBkColor(hdc, chrome_.fieldBg);
        static HBRUSH br = CreateSolidBrush(chrome_.fieldBg);
        return (LRESULT)br;
    }

    case WM_COMMAND:
        if ((HWND)lParam == editCtrl_ && HIWORD(wParam) == EN_KILLFOCUS) {
            commitTextEdit();
        }
        return 0;

    case WM_LBUTTONDOWN: {
        int mx = (short)LOWORD(lParam);
        int my = (short)HIWORD(lParam);
        onLButtonDown(mx, my);
        return 0;
    }

    case WM_LBUTTONUP: {
        int mx = (short)LOWORD(lParam);
        int my = (short)HIWORD(lParam);
        onLButtonUp(mx, my);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx = (short)LOWORD(lParam);
        int my = (short)HIWORD(lParam);
        onMouseMove(mx, my);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (activeTab_ == SettingsTab::Keys) {
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            keysScrollY_ -= delta * 30 / WHEEL_DELTA;
            int maxScroll = (std::max)(0,
                (int)config_.keybindings.size() * kSetRowHeight - 300);
            keysScrollY_ = (std::max)(0, (std::min)(keysScrollY_, maxScroll));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { close(); return 0; }
        break;

    case WM_DESTROY:
        hwnd_ = nullptr;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Text editing
// ---------------------------------------------------------------------------

static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg,
                                          WPARAM wParam, LPARAM lParam) {
    WNDPROC origProc = (WNDPROC)GetPropW(hwnd, L"OrigProc");
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN || wParam == VK_ESCAPE) {
            SetFocus(GetParent(hwnd));
            return 0;
        }
    }
    return CallWindowProcW(origProc, hwnd, msg, wParam, lParam);
}

void SettingsWindow::beginTextEdit(int fieldId, float x, float y, float w,
                                   const std::wstring& value) {
    destroyTextEdit();
    editFieldId_ = fieldId;

    if (!editFont_) {
        editFont_ = CreateFontW(
            -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    }

    editCtrl_ = CreateWindowExW(
        0, L"EDIT", value.c_str(),
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
        (int)(x + 4), (int)(y + 2), (int)(w - 8), kSetFieldH - 4,
        hwnd_, nullptr,
        (HINSTANCE)GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE), nullptr);

    SendMessageW(editCtrl_, WM_SETFONT, (WPARAM)editFont_, TRUE);
    SendMessageW(editCtrl_, EM_SETSEL, 0, -1);

    // Subclass to handle Enter/Escape
    origEditProc_ = (WNDPROC)SetWindowLongPtrW(
        editCtrl_, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    SetPropW(editCtrl_, L"OrigProc", (HANDLE)origEditProc_);

    SetFocus(editCtrl_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::commitTextEdit() {
    if (!editCtrl_) return;

    wchar_t buf[512] = {};
    GetWindowTextW(editCtrl_, buf, 511);
    std::wstring val = buf;

    int id = editFieldId_;
    destroyTextEdit();

    // Apply value based on field id
    switch (id) {
    case 0: // Shell
        config_.shell = toUtf8(val);
        break;
    case 1: { // Scrollback
        int v = _wtoi(val.c_str());
        if (v > 0) config_.scrollback_limit = v;
        break;
    }
    case 10: // Font Family
        if (!val.empty()) config_.font_family = toUtf8(val);
        break;
    case 11: { // Font Size
        float v = (float)_wtof(val.c_str());
        if (v >= 6.f && v <= 72.f) config_.font_size = v;
        break;
    }
    case 12: { // Font Features
        config_.font_features.clear();
        std::wstring token;
        std::wistringstream stream(val);
        while (std::getline(stream, token, L',')) {
            size_t start = token.find_first_not_of(L' ');
            size_t end = token.find_last_not_of(L' ');
            if (start != std::wstring::npos) {
                std::wstring trimmed = token.substr(start, end - start + 1);
                if (!trimmed.empty()) {
                    config_.font_features.push_back(toUtf8(trimmed));
                }
            }
        }
        break;
    }
    default:
        // Keybinding fields: id >= 100
        if (id >= 100) {
            int kbIdx = (id - 100) / 2;
            bool isTrigger = ((id - 100) % 2 == 0);
            if (kbIdx >= 0 && kbIdx < (int)config_.keybindings.size()) {
                std::string utf8 = toUtf8(val);
                if (isTrigger)
                    config_.keybindings[kbIdx].trigger = utf8;
                else
                    config_.keybindings[kbIdx].action = utf8;
            }
        }
        break;
    }

    notifySave();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::destroyTextEdit() {
    if (editCtrl_) {
        RemovePropW(editCtrl_, L"OrigProc");
        DestroyWindow(editCtrl_);
        editCtrl_ = nullptr;
    }
    editFieldId_ = -1;
}

// ---------------------------------------------------------------------------
// Color picker
// ---------------------------------------------------------------------------

void SettingsWindow::openColorPicker(uint32_t& colorField) {
    static COLORREF customColors[16] = {};

    BYTE r = (BYTE)((colorField >> 16) & 0xFF);
    BYTE gg = (BYTE)((colorField >> 8) & 0xFF);
    BYTE b = (BYTE)(colorField & 0xFF);

    CHOOSECOLORW cc = {};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner   = hwnd_;
    cc.rgbResult   = RGB(r, gg, b);
    cc.lpCustColors = customColors;
    cc.Flags       = CC_FULLOPEN | CC_RGBINIT;

    if (ChooseColorW(&cc)) {
        BYTE nr = GetRValue(cc.rgbResult);
        BYTE ng = GetGValue(cc.rgbResult);
        BYTE nb = GetBValue(cc.rgbResult);
        colorField = ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | nb;
        notifySave();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

} // namespace termcore

#endif
