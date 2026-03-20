#if defined(_WIN32)

#include "ThemeHubWindow.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace termcore {

const wchar_t* ThemeHubWindow::kClassName = L"BreadTerminal_ThemeHub";
bool ThemeHubWindow::sClassRegistered = false;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ThemeHubWindow::ThemeHubWindow() {
    Gdiplus::GdiplusStartupInput si;
    Gdiplus::Status st = Gdiplus::GdiplusStartup(&gdiplusToken_, &si, nullptr);
    gdiplusOwned_ = (st == Gdiplus::Ok);
}

ThemeHubWindow::~ThemeHubWindow() {
    close();
    if (gdiplusOwned_ && gdiplusToken_) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
    }
}

void ThemeHubWindow::setConfig(const Config& config) {
    config_ = config;
    activeThemeName_ = config.theme;
    chrome_ = deriveChrome(config.background, config.foreground, config.palette);
    // Repaint if the window is open
    if (hwnd_) {
        recalcCardLayout(kThWinWidth);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void ThemeHubWindow::setApplyCallback(ApplyCallback cb) {
    applyCallback_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// Window class registration
// ---------------------------------------------------------------------------

void ThemeHubWindow::registerWindowClass(HINSTANCE hInstance) {
    if (sClassRegistered) return;
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = ThemeHubWindow::WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    wc.hbrBackground = nullptr; // we paint everything
    RegisterClassExW(&wc);
    sClassRegistered = true;
}

// ---------------------------------------------------------------------------
// Show / Close
// ---------------------------------------------------------------------------

void ThemeHubWindow::show(HWND parent) {
    if (hwnd_) { SetForegroundWindow(hwnd_); return; }
    parentHwnd_ = parent;

    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE);
    registerWindowClass(hInst);

    // Load theme index
    std::string path = findThemeIndexPath();
    if (!path.empty()) {
        std::ifstream f(path);
        if (f.is_open()) {
            std::ostringstream ss;
            ss << f.rdbuf();
            indexLoaded_ = themeIndex_.loadFromJSON(ss.str());
        }
    }
    themeIndex_.refreshInstallStatus();
    rebuildFilteredList();

    // Center on parent
    RECT pr;
    GetWindowRect(parent, &pr);
    int cx = (pr.left + pr.right) / 2 - kThWinWidth / 2;
    int cy = (pr.top + pr.bottom) / 2 - kThWinHeight / 2;

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kClassName, L"Theme Hub",
        WS_POPUP | WS_CLIPCHILDREN,
        cx, cy, kThWinWidth, kThWinHeight,
        parent, nullptr, hInst, this);

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
}

void ThemeHubWindow::close() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Theme index discovery
// ---------------------------------------------------------------------------

std::string ThemeHubWindow::findThemeIndexPath() const {
    // Next to executable
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring exeDir(buf);
    auto pos = exeDir.find_last_of(L'\\');
    if (pos != std::wstring::npos) exeDir.resize(pos);

    auto tryPath = [](const std::wstring& dir) -> std::string {
        std::wstring full = dir + L"\\theme_index.json";
        DWORD attr = GetFileAttributesW(full.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES) {
            int sz = WideCharToMultiByte(CP_UTF8, 0, full.c_str(), -1,
                                         nullptr, 0, nullptr, nullptr);
            std::string s(sz - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, full.c_str(), -1,
                                s.data(), sz, nullptr, nullptr);
            return s;
        }
        return {};
    };

    std::string r = tryPath(exeDir);
    if (!r.empty()) return r;
    r = tryPath(exeDir + L"\\resources");
    if (!r.empty()) return r;
    r = tryPath(exeDir + L"\\..\\resources");
    return r;
}

// ---------------------------------------------------------------------------
// Search / filter logic
// ---------------------------------------------------------------------------

void ThemeHubWindow::rebuildFilteredList() {
    // Convert search text to UTF-8
    std::string query;
    if (!searchText_.empty()) {
        int sz = WideCharToMultiByte(CP_UTF8, 0, searchText_.c_str(), -1,
                                     nullptr, 0, nullptr, nullptr);
        query.resize(sz - 1);
        WideCharToMultiByte(CP_UTF8, 0, searchText_.c_str(), -1,
                            query.data(), sz, nullptr, nullptr);
    }

    if (!query.empty()) {
        filteredThemes_ = themeIndex_.search(query);
    } else {
        bool dark  = (activeFilter_ == ThemeFilter::Dark);
        bool light = (activeFilter_ == ThemeFilter::Light);
        bool inst  = (activeFilter_ == ThemeFilter::Installed);
        if (dark || light || inst) {
            filteredThemes_ = themeIndex_.filterByCategory(dark, light, inst);
        } else {
            // All
            filteredThemes_.clear();
            for (auto& t : themeIndex_.all())
                filteredThemes_.push_back(&t);
        }
    }

    // Reset scroll
    scrollY_ = 0.f;
    hoveredCard_ = -1;
}

void ThemeHubWindow::onSearchChanged() {
    wchar_t buf[256] = {};
    GetWindowTextW(searchEdit_, buf, 255);
    searchText_ = buf;
    rebuildFilteredList();
    if (hwnd_) {
        recalcCardLayout(kThWinWidth);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void ThemeHubWindow::recalcCardLayout(int clientW) {
    int usable = clientW - 2 * kThGridPad + kThCardGap;
    gridColumns_ = (std::max)(1, usable / (kThCardW + kThCardGap));

    visibleCards_.clear();
    int topOffset = kThTitleH + kThToolbarH + kThGridPad;
    int scrollOff = (int)scrollY_;

    for (size_t i = 0; i < filteredThemes_.size(); ++i) {
        int col = (int)(i % gridColumns_);
        int row = (int)(i / gridColumns_);
        int x = kThGridPad + col * (kThCardW + kThCardGap);
        int y = topOffset + row * (kThCardH + kThCardGap) - scrollOff;

        ThemeCardInfo ci;
        ci.meta = filteredThemes_[i];
        ci.cardRect = { x, y, x + kThCardW, y + kThCardH };
        // Button rect: bottom-right of card, 60x22
        ci.buttonRect = { x + kThCardW - 68, y + kThCardH - 28,
                          x + kThCardW - 4,  y + kThCardH - 6 };
        ci.isActive = false;
        if (ci.meta) {
            if (!activeThemeName_.empty()) {
                auto iequal = [](const std::string& a, const std::string& b) {
                    if (a.size() != b.size()) return false;
                    for (size_t j = 0; j < a.size(); ++j)
                        if (tolower((unsigned char)a[j]) != tolower((unsigned char)b[j]))
                            return false;
                    return true;
                };
                ci.isActive = iequal(ci.meta->name, activeThemeName_)
                           || activeThemeName_.find(ci.meta->name) != std::string::npos;
            } else {
                // No theme name set — match by background+foreground colors
                ci.isActive = (ci.meta->background == config_.background
                            && ci.meta->foreground == config_.foreground);
            }
        }
        visibleCards_.push_back(ci);
    }
}

int ThemeHubWindow::totalContentHeight() const {
    if (filteredThemes_.empty()) return 0;
    int rows = ((int)filteredThemes_.size() + gridColumns_ - 1) / gridColumns_;
    return rows * (kThCardH + kThCardGap) - kThCardGap + 2 * kThGridPad;
}

void ThemeHubWindow::clampScroll(int clientH) {
    int contentH = totalContentHeight();
    int viewH = clientH - kThTitleH - kThToolbarH;
    float maxS = (float)(std::max)(0, contentH - viewH);
    scrollY_ = (std::max)(0.f, (std::min)(scrollY_, maxS));
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------

LRESULT CALLBACK ThemeHubWindow::WndProc(HWND hwnd, UINT msg,
                                          WPARAM wParam, LPARAM lParam) {
    ThemeHubWindow* self = nullptr;
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<ThemeHubWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = reinterpret_cast<ThemeHubWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT ThemeHubWindow::handleMessage(HWND hwnd, UINT msg,
                                       WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // Create search edit
        searchEdit_ = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
            kThGridPad + 24, kThTitleH + 8, kThSearchW - 28, kThSearchH - 4,
            hwnd, nullptr,
            ((CREATESTRUCT*)lParam)->hInstance, nullptr);

        // Set font
        HFONT uiFont = CreateFontW(
            -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        SendMessageW(searchEdit_, WM_SETFONT, (WPARAM)uiFont, TRUE);

        recalcCardLayout(kThWinWidth);
        return 0;
    }

    case WM_PAINT:
        paintWindow(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1; // we handle erase in WM_PAINT

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, chrome_.textColor);
        SetBkColor(hdc, chrome_.fieldBg);
        static HBRUSH br = nullptr;
        static COLORREF lastBg = 0;
        if (!br || lastBg != chrome_.fieldBg) {
            if (br) DeleteObject(br);
            br = CreateSolidBrush(chrome_.fieldBg);
            lastBg = chrome_.fieldBg;
        }
        return (LRESULT)br;
    }

    case WM_COMMAND:
        if ((HWND)lParam == searchEdit_ && HIWORD(wParam) == EN_CHANGE) {
            KillTimer(hwnd, kThSearchTimerId);
            SetTimer(hwnd, kThSearchTimerId, kThSearchDelay, nullptr);
        }
        return 0;

    case WM_TIMER:
        if (wParam == kThSearchTimerId) {
            KillTimer(hwnd, kThSearchTimerId);
            onSearchChanged();
        }
        return 0;

    case WM_MOUSEWHEEL: {
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        scrollY_ -= (float)delta * 40.f / WHEEL_DELTA;
        RECT rc;
        GetClientRect(hwnd, &rc);
        clampScroll(rc.bottom);
        recalcCardLayout(rc.right);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx = LOWORD(lParam), my = HIWORD(lParam);
        int idx = hitTestCard(mx, my);
        if (idx != hoveredCard_) {
            hoveredCard_ = idx;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        // Track mouse leave for hover reset
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (hoveredCard_ != -1) {
            hoveredCard_ = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN: {
        int mx = LOWORD(lParam), my = HIWORD(lParam);

        // Close button
        if (hitTestCloseButton(mx, my)) {
            close();
            return 0;
        }

        // Filter buttons
        int fb = hitTestFilterButton(mx, my);
        if (fb >= 0) {
            activeFilter_ = static_cast<ThemeFilter>(fb);
            rebuildFilteredList();
            recalcCardLayout(kThWinWidth);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Card button click
        int ci = hitTestCard(mx, my);
        if (ci >= 0 && ci < (int)visibleCards_.size()) {
            if (hitTestCardButton(ci, mx, my)) {
                auto& card = visibleCards_[ci];
                if (applyCallback_ && card.meta) {
                    activeThemeName_ = card.meta->name;
                    applyCallback_(card.meta->name, card.meta);
                    recalcCardLayout(kThWinWidth);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
        }

        // Title bar drag
        if (my < kThTitleH) {
            ReleaseCapture();
            SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, lParam);
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

} // namespace termcore

#endif
