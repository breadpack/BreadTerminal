#if defined(_WIN32)

#include "FontHubWindow.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <thread>
#include <shellapi.h>
#include "FontInstaller.h"

namespace termcore {

const wchar_t* FontHubWindow::kClassName = L"BreadTerminal_FontHub";
bool FontHubWindow::sClassRegistered = false;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

FontHubWindow::FontHubWindow() {
    Gdiplus::GdiplusStartupInput si;
    Gdiplus::Status st = Gdiplus::GdiplusStartup(&gdiplusToken_, &si, nullptr);
    gdiplusOwned_ = (st == Gdiplus::Ok);
}

FontHubWindow::~FontHubWindow() {
    close();
    if (gdiplusOwned_ && gdiplusToken_) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
    }
}

void FontHubWindow::setConfig(const Config& config) {
    config_ = config;
    chrome_ = deriveChrome(config.background, config.foreground, config.palette);
    activeFontName_ = config.font_family;
    if (hwnd_) {
        recalcCardLayout(kFhWinWidth);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void FontHubWindow::setApplyCallback(ApplyCallback cb) {
    applyCallback_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// Window class registration
// ---------------------------------------------------------------------------

void FontHubWindow::registerWindowClass(HINSTANCE hInstance) {
    if (sClassRegistered) return;
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = FontHubWindow::WndProc;
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

void FontHubWindow::show(HWND parent) {
    if (hwnd_) { SetForegroundWindow(hwnd_); return; }
    parentHwnd_ = parent;

    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE);
    registerWindowClass(hInst);

    // Load font index
    std::string path = findFontIndexPath();
    if (!path.empty()) {
        std::ifstream f(path);
        if (f.is_open()) {
            std::ostringstream ss;
            ss << f.rdbuf();
            indexLoaded_ = fontIndex_.loadFromJSON(ss.str());
        }
    }

    // Inject GDI+ font-installed predicate
    fontIndex_.setInstalledPredicate([this](const std::string& postscriptName) -> bool {
        if (postscriptName.empty()) return false;
        std::wstring wide = toWide(postscriptName);
        return isFontInstalled(wide);
    });
    fontIndex_.refreshInstallStatus();
    rebuildFilteredList();

    // Center on parent
    RECT pr;
    GetWindowRect(parent, &pr);
    int cx = (pr.left + pr.right) / 2 - kFhWinWidth / 2;
    int cy = (pr.top + pr.bottom) / 2 - kFhWinHeight / 2;

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kClassName, L"Font Hub",
        WS_POPUP | WS_CLIPCHILDREN,
        cx, cy, kFhWinWidth, kFhWinHeight,
        parent, nullptr, hInst, this);

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
}

void FontHubWindow::close() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Font index discovery
// ---------------------------------------------------------------------------

std::string FontHubWindow::findFontIndexPath() const {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring exeDir(buf);
    auto pos = exeDir.find_last_of(L'\\');
    if (pos != std::wstring::npos) exeDir.resize(pos);

    auto tryPath = [](const std::wstring& dir) -> std::string {
        std::wstring full = dir + L"\\font_index.json";
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
// Font installed check via GDI+
// ---------------------------------------------------------------------------

bool FontHubWindow::isFontInstalled(const std::wstring& fontName) const {
    Gdiplus::FontFamily family(fontName.c_str());
    return family.IsAvailable();
}

// ---------------------------------------------------------------------------
// Search / filter logic
// ---------------------------------------------------------------------------

void FontHubWindow::rebuildFilteredList() {
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
        filteredFonts_ = fontIndex_.search(query);
    } else {
        switch (activeFilter_) {
            case FontFilter::Installed:
                filteredFonts_ = fontIndex_.filter(true, false, false);
                break;
            case FontFilter::NerdFonts:
                filteredFonts_ = fontIndex_.filter(false, true, false);
                break;
            case FontFilter::Ligatures:
                filteredFonts_ = fontIndex_.filter(false, false, true);
                break;
            case FontFilter::All:
            default:
                filteredFonts_.clear();
                for (auto& f : fontIndex_.all())
                    filteredFonts_.push_back(&f);
                break;
        }
    }

    scrollY_ = 0.f;
    hoveredCard_ = -1;
}

void FontHubWindow::onSearchChanged() {
    wchar_t buf[256] = {};
    GetWindowTextW(searchEdit_, buf, 255);
    searchText_ = buf;
    rebuildFilteredList();
    if (hwnd_) {
        recalcCardLayout(kFhWinWidth);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void FontHubWindow::recalcCardLayout(int clientW) {
    int usable = clientW - 2 * kFhGridPad + kFhCardGap;
    gridColumns_ = (std::max)(1, usable / (kFhCardW + kFhCardGap));

    visibleCards_.clear();
    int topOffset = kFhTitleH + kFhToolbarH + kFhGridPad;
    int scrollOff = (int)scrollY_;

    for (size_t i = 0; i < filteredFonts_.size(); ++i) {
        int col = (int)(i % gridColumns_);
        int row = (int)(i / gridColumns_);
        int x = kFhGridPad + col * (kFhCardW + kFhCardGap);
        int y = topOffset + row * (kFhCardH + kFhCardGap) - scrollOff;

        FontCardInfo ci;
        ci.meta = filteredFonts_[i];
        ci.cardRect = { x, y, x + kFhCardW, y + kFhCardH };
        // Button rect: bottom-right of card, 64x22
        ci.buttonRect = { x + kFhCardW - 72, y + kFhCardH - 26,
                          x + kFhCardW - 6,  y + kFhCardH - 4 };
        ci.isActive = false;
        if (ci.meta && !activeFontName_.empty()) {
            // Match by name or postscript_name, case-insensitive
            auto iequal = [](const std::string& a, const std::string& b) {
                if (a.size() != b.size()) return false;
                for (size_t j = 0; j < a.size(); ++j)
                    if (tolower((unsigned char)a[j]) != tolower((unsigned char)b[j]))
                        return false;
                return true;
            };
            // Also check if activeFontName_ contains the index name (e.g. "JetBrainsMono NF" contains "JetBrains Mono")
            ci.isActive = iequal(ci.meta->name, activeFontName_)
                       || iequal(ci.meta->postscript_name, activeFontName_)
                       || activeFontName_.find(ci.meta->name) != std::string::npos;
        }
        ci.isInstalling = ((int)i == installingCard_);
        visibleCards_.push_back(ci);
    }
}

int FontHubWindow::totalContentHeight() const {
    if (filteredFonts_.empty()) return 0;
    int rows = ((int)filteredFonts_.size() + gridColumns_ - 1) / gridColumns_;
    return rows * (kFhCardH + kFhCardGap) - kFhCardGap + 2 * kFhGridPad;
}

void FontHubWindow::clampScroll(int clientH) {
    int contentH = totalContentHeight();
    int viewH = clientH - kFhTitleH - kFhToolbarH;
    float maxS = (float)(std::max)(0, contentH - viewH);
    scrollY_ = (std::max)(0.f, (std::min)(scrollY_, maxS));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::wstring FontHubWindow::toWide(const std::string& utf8) const {
    if (utf8.empty()) return {};
    int sz = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring w(sz - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, w.data(), sz);
    return w;
}

void FontHubWindow::drawRoundedRect(Gdiplus::GraphicsPath& path,
                                     float x, float y, float w, float h,
                                     float r) const {
    path.AddArc(x, y, r * 2, r * 2, 180.f, 90.f);
    path.AddArc(x + w - r * 2, y, r * 2, r * 2, 270.f, 90.f);
    path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0.f, 90.f);
    path.AddArc(x, y + h - r * 2, r * 2, r * 2, 90.f, 90.f);
    path.CloseFigure();
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------

LRESULT CALLBACK FontHubWindow::WndProc(HWND hwnd, UINT msg,
                                         WPARAM wParam, LPARAM lParam) {
    FontHubWindow* self = nullptr;
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<FontHubWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = reinterpret_cast<FontHubWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT FontHubWindow::handleMessage(HWND hwnd, UINT msg,
                                      WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // Create search edit
        searchEdit_ = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
            kFhGridPad + 24, kFhTitleH + 8, kFhSearchW - 28, kFhSearchH - 4,
            hwnd, nullptr,
            ((CREATESTRUCT*)lParam)->hInstance, nullptr);

        // Set font
        HFONT uiFont = CreateFontW(
            -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        SendMessageW(searchEdit_, WM_SETFONT, (WPARAM)uiFont, TRUE);

        recalcCardLayout(kFhWinWidth);
        return 0;
    }

    case WM_PAINT:
        paintWindow(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

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
            KillTimer(hwnd, kFhSearchTimerId);
            SetTimer(hwnd, kFhSearchTimerId, kFhSearchDelay, nullptr);
        }
        return 0;

    case WM_TIMER:
        if (wParam == kFhSearchTimerId) {
            KillTimer(hwnd, kFhSearchTimerId);
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
            activeFilter_ = static_cast<FontFilter>(fb);
            rebuildFilteredList();
            recalcCardLayout(kFhWinWidth);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Card button click
        int ci = hitTestCard(mx, my);
        if (ci >= 0 && ci < (int)visibleCards_.size()) {
            if (hitTestCardButton(ci, mx, my)) {
                auto& card = visibleCards_[ci];
                if (card.meta) {
                    if (card.meta->installed && applyCallback_) {
                        // Apply the font
                        activeFontName_ = card.meta->name;
                        applyCallback_(card.meta->name);
                        recalcCardLayout(kFhWinWidth);
                        InvalidateRect(hwnd, nullptr, FALSE);
                    } else if (!card.meta->download_url.empty() &&
                               installingCard_ < 0) {
                        // Download and install font internally
                        installingCard_ = ci;
                        InvalidateRect(hwnd, nullptr, FALSE);

                        std::string dlUrl = card.meta->download_url;
                        std::string dlName = card.meta->name;
                        HWND hWnd = hwnd;

                        // Run install on a background thread
                        std::thread([this, dlUrl, dlName, hWnd]() {
                            bool ok = installFontFromUrl(dlUrl, dlName,
                                [](const std::string&) { /* progress */ });

                            // Back on UI thread
                            PostMessageW(hWnd, WM_APP + 1,
                                         ok ? 1 : 0, 0);
                        }).detach();
                    }
                }
                return 0;
            }
        }

        // Title bar drag
        if (my < kFhTitleH) {
            ReleaseCapture();
            SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, lParam);
        }
        return 0;
    }

    case WM_APP + 1: {
        // Font install completed (wParam=1 success, 0 fail)
        installingCard_ = -1;
        if (wParam == 1) {
            // Refresh install status and repaint
            fontIndex_.refreshInstallStatus();
            rebuildFilteredList();
            recalcCardLayout(kFhWinWidth);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
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
