#if defined(_WIN32)

#include "UnifiedSettingsWindow.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <set>

#include <shlobj.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

namespace termcore {

const wchar_t* UnifiedSettingsWindow::kClassName = L"BreadTermUnifiedSettings";
bool UnifiedSettingsWindow::sClassRegistered = false;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

UnifiedSettingsWindow::UnifiedSettingsWindow() {
    Gdiplus::GdiplusStartupInput si;
    Gdiplus::Status st = Gdiplus::GdiplusStartup(&gdiplusToken_, &si, nullptr);
    gdiplusOwned_ = (st == Gdiplus::Ok);
}

UnifiedSettingsWindow::~UnifiedSettingsWindow() {
    close();
    if (gdiplusOwned_ && gdiplusToken_) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
    }
}

// ---------------------------------------------------------------------------
// setConfig
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::setConfig(const Config& config) {
    config_ = config;
    defaultConfig_ = Config{};
    chrome_ = deriveChrome(config.background, config.foreground, config.palette);

    model_ = std::make_unique<SettingsModel>(config_, defaultConfig_);

    // Build allCategoryIds_ and visibleCategoryIds_ from all subcategories
    allCategoryIds_.clear();
    visibleCategoryIds_.clear();
    auto topCats = model_->topLevelCategories();
    for (auto* top : topCats) {
        auto subs = model_->subcategories(top->id);
        for (auto* sub : subs) {
            allCategoryIds_.push_back(sub->id);
            visibleCategoryIds_.push_back(sub->id);
        }
    }

    // Try loading font_index.json
    {
        std::string path = findFontIndexPath();
        if (!path.empty()) {
            std::ifstream f(path);
            if (f.is_open()) {
                std::ostringstream ss;
                ss << f.rdbuf();
                fontIndexReady_ = fontIndex_.loadFromJSON(ss.str());
            }
        }
    }

    // Set up installed predicate and refresh font install status
    if (fontIndexReady_) {
        fontIndex_.setInstalledPredicate([this](const std::string& psName) -> bool {
            if (psName.empty()) return false;
            return isFontInstalled(toWide(psName));
        });
        fontIndex_.refreshInstallStatus();
        rebuildFontFilteredList();
    }

    // Try loading theme_index.json
    {
        std::string path = findThemeIndexPath();
        if (!path.empty()) {
            std::ifstream f(path);
            if (f.is_open()) {
                std::ostringstream ss;
                ss << f.rdbuf();
                themeIndex_.loadFromJSON(ss.str());
            }
        }
    }

    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void UnifiedSettingsWindow::setSaveCallback(SaveCallback cb) {
    saveCallback_ = std::move(cb);
}

// ---------------------------------------------------------------------------
// Window class registration
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::registerWindowClass(HINSTANCE hInstance) {
    if (sClassRegistered) return;
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = UnifiedSettingsWindow::WndProc;
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

void UnifiedSettingsWindow::show(HWND parent) {
    if (hwnd_) { SetForegroundWindow(hwnd_); return; }
    parentHwnd_ = parent;

    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE);
    registerWindowClass(hInst);

    // Center on parent
    RECT pr;
    GetWindowRect(parent, &pr);
    int cx = (pr.left + pr.right) / 2 - kUsWinWidth / 2;
    int cy = (pr.top + pr.bottom) / 2 - kUsWinHeight / 2;

    hwnd_ = CreateWindowExW(
        0,
        kClassName, L"Settings",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        cx, cy, kUsWinWidth, kUsWinHeight,
        parent, nullptr, hInst, this);

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
}

void UnifiedSettingsWindow::close() {
    if (searchEdit_) {
        DestroyWindow(searchEdit_);
        searchEdit_ = nullptr;
    }
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

Gdiplus::Color UnifiedSettingsWindow::toGdipColor(uint32_t rgb, BYTE a) const {
    return Gdiplus::Color(a,
        (BYTE)((rgb >> 16) & 0xFF),
        (BYTE)((rgb >> 8) & 0xFF),
        (BYTE)(rgb & 0xFF));
}

Gdiplus::Color UnifiedSettingsWindow::toGdipColorCR(COLORREF cr, BYTE a) const {
    return Gdiplus::Color(a, GetRValue(cr), GetGValue(cr), GetBValue(cr));
}

std::wstring UnifiedSettingsWindow::toWide(const std::string& s) const {
    if (s.empty()) return {};
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(sz - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), sz);
    return w;
}

void UnifiedSettingsWindow::drawRoundedRect(Gdiplus::Graphics& g,
                                             Gdiplus::Brush* brush,
                                             float x, float y, float w,
                                             float h, float r) {
    Gdiplus::GraphicsPath path;
    float d = r * 2.f;
    path.AddArc(x, y, d, d, 180.f, 90.f);
    path.AddArc(x + w - d, y, d, d, 270.f, 90.f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.f, 90.f);
    path.AddArc(x, y + h - d, d, d, 90.f, 90.f);
    path.CloseFigure();
    g.FillPath(brush, &path);
}

void UnifiedSettingsWindow::drawRoundedRectPath(Gdiplus::GraphicsPath& path,
                                                  float x, float y, float w,
                                                  float h, float r) const {
    float d = r * 2.f;
    path.AddArc(x, y, d, d, 180.f, 90.f);
    path.AddArc(x + w - d, y, d, d, 270.f, 90.f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.f, 90.f);
    path.AddArc(x, y + h - d, d, d, 90.f, 90.f);
    path.CloseFigure();
}

bool UnifiedSettingsWindow::isFontInstalled(const std::wstring& fontName) const {
    Gdiplus::FontFamily family(fontName.c_str());
    return family.IsAvailable();
}

void UnifiedSettingsWindow::notifySave() {
    if (saveCallback_) saveCallback_(config_);
}

// ---------------------------------------------------------------------------
// Index file discovery
// ---------------------------------------------------------------------------

std::string UnifiedSettingsWindow::findFontIndexPath() const {
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

std::string UnifiedSettingsWindow::findThemeIndexPath() const {
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
// Sidebar resize
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::beginSidebarResize(int mx) {
    sidebarResizing_ = true;
    sidebarResizeStartX_ = mx;
    sidebarResizeStartW_ = sidebarWidth_;
    SetCapture(hwnd_);
}

void UnifiedSettingsWindow::updateSidebarResize(int mx) {
    if (!sidebarResizing_) return;
    int delta = mx - sidebarResizeStartX_;
    int newW = sidebarResizeStartW_ + delta;
    sidebarWidth_ = (std::max)(kUsSidebarMin, (std::min)(kUsSidebarMax, newW));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void UnifiedSettingsWindow::endSidebarResize() {
    if (sidebarResizing_) {
        sidebarResizing_ = false;
        ReleaseCapture();
    }
}

// ---------------------------------------------------------------------------
// Sidebar hit test
// ---------------------------------------------------------------------------

int UnifiedSettingsWindow::hitTestSidebar(int mx, int my) const {
    if (mx < 0 || mx >= sidebarWidth_) return -1;
    if (my < kUsTopBarH) return -1;
    if (!model_) return -1;

    // Build visible set for filtering (matches paintSidebar logic)
    std::set<std::string> visibleSet(visibleCategoryIds_.begin(),
                                     visibleCategoryIds_.end());

    int y = kUsTopBarH + 8;
    int idx = 0;

    auto topCats = model_->topLevelCategories();
    for (auto* top : topCats) {
        auto subs = model_->subcategories(top->id);
        bool hasVisible = false;
        for (auto* sub : subs) {
            if (visibleSet.count(sub->id)) { hasVisible = true; break; }
        }
        if (!hasVisible) continue;

        y += kUsCatRowH; // category label row
        for (auto* sub : subs) {
            if (!visibleSet.count(sub->id)) continue;
            if (my >= y && my < y + kUsSubCatRowH) {
                return idx;
            }
            y += kUsSubCatRowH;
            idx++;
        }
        y += 4; // gap between groups
    }
    return -1;
}

void UnifiedSettingsWindow::onSidebarClick(int mx, int my) {
    int idx = hitTestSidebar(mx, my);
    if (idx >= 0 && idx < (int)visibleCategoryIds_.size()) {
        selectedCategoryId_ = visibleCategoryIds_[idx];
        scrollY_ = 0.f;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------

LRESULT CALLBACK UnifiedSettingsWindow::WndProc(HWND hwnd, UINT msg,
                                                  WPARAM wParam, LPARAM lParam) {
    UnifiedSettingsWindow* self = nullptr;
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<UnifiedSettingsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = reinterpret_cast<UnifiedSettingsWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT UnifiedSettingsWindow::handleMessage(HWND hwnd, UINT msg,
                                               WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        createSearchEdit();
        return 0;

    case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        repositionSearchEdit(rc.right);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == 1001 && HIWORD(wParam) == EN_CHANGE) {
            onSearchTextChanged();
            return 0;
        }
        // Inline edit: commit on kill focus
        if (LOWORD(wParam) == 1002 && HIWORD(wParam) == EN_KILLFOCUS) {
            commitInlineEdit();
            return 0;
        }
        break;

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        HWND ctrl = (HWND)lParam;
        if (ctrl == searchEdit_) {
            SetTextColor(hdc, chrome_.textColor);
            SetBkColor(hdc, chrome_.fieldBg);
            static HBRUSH fieldBrush = nullptr;
            if (fieldBrush) DeleteObject(fieldBrush);
            fieldBrush = CreateSolidBrush(chrome_.fieldBg);
            return (LRESULT)fieldBrush;
        }
        break;
    }

    case WM_PAINT:
        paintWindow(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = kUsMinWidth;
        mmi->ptMinTrackSize.y = kUsMinHeight;
        return 0;
    }

    case WM_MOUSEWHEEL: {
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        scrollY_ -= static_cast<float>(delta) * 30.f / WHEEL_DELTA;
        scrollY_ = (std::max)(0.f, scrollY_);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        // Commit any open inline edit if clicking outside it
        if (inlineEdit_) {
            POINT pt = { mx, my };
            RECT er;
            GetWindowRect(inlineEdit_, &er);
            MapWindowPoints(HWND_DESKTOP, hwnd_, (LPPOINT)&er, 2);
            if (!PtInRect(&er, pt)) {
                commitInlineEdit();
            }
        }

        // Top bar clicks
        if (my < kUsTopBarH) {
            RECT rc;
            GetClientRect(hwnd, &rc);

            // "Open Lua" button (right side)
            float btnW = 80.f, btnH = 26.f;
            float btnX = rc.right - btnW - 12.f;
            float btnY = (kUsTopBarH - btnH) / 2.f;
            if ((float)mx >= btnX && (float)mx < btnX + btnW &&
                (float)my >= btnY && (float)my < btnY + btnH) {
                const wchar_t* homeEnv = _wgetenv(L"USERPROFILE");
                if (homeEnv) {
                    std::wstring configPath = std::wstring(homeEnv) + L"\\.bt\\config.lua";
                    DWORD attr = GetFileAttributesW(configPath.c_str());
                    if (attr == INVALID_FILE_ATTRIBUTES) {
                        std::wstring dir = std::wstring(homeEnv) + L"\\.bt";
                        CreateDirectoryW(dir.c_str(), nullptr);
                        HANDLE hFile = CreateFileW(configPath.c_str(), GENERIC_WRITE, 0,
                                                    nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                        if (hFile != INVALID_HANDLE_VALUE) {
                            const char* defaultContent = "-- BreadTerminal config\nreturn {}\n";
                            DWORD written;
                            WriteFile(hFile, defaultContent, (DWORD)strlen(defaultContent), &written, nullptr);
                            CloseHandle(hFile);
                        }
                    }
                    ShellExecuteW(hwnd, L"open", configPath.c_str(), nullptr, nullptr, SW_SHOW);
                }
                return 0;
            }
        }

        // Check clear search (x) button click in top bar
        if (my < kUsTopBarH && !searchText_.empty()) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            float searchX = (rc.right - kUsSearchW) / 2.f;
            float clearX = searchX + kUsSearchW - 24.f;
            float clearY = (kUsTopBarH - kUsSearchH) / 2.f;
            if (mx >= (int)clearX && mx <= (int)(clearX + 20) &&
                my >= (int)clearY && my <= (int)(clearY + kUsSearchH)) {
                clearSearch();
                return 0;
            }
        }

        // Check sidebar resize border (+-3px of sidebar edge)
        if (my >= kUsTopBarH && std::abs(mx - sidebarWidth_) <= 3) {
            beginSidebarResize(mx);
            return 0;
        }

        // Check sidebar click
        if (mx < sidebarWidth_ && my >= kUsTopBarH) {
            onSidebarClick(mx, my);
            return 0;
        }

        // Content area clicks — font card grid
        if (mx >= sidebarWidth_ && my >= kUsTopBarH &&
            selectedCategoryId_.find("font.family") != std::string::npos) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            int contentX = sidebarWidth_ + kUsContentPad;

            // Filter pill buttons
            int fb = hitTestFontFilterButton(mx, my, contentX);
            if (fb >= 0) {
                activeFontFilter_ = static_cast<FontFilter>(fb);
                rebuildFontFilteredList();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            // Card buttons
            int ci = hitTestFontCard(mx, my);
            if (ci >= 0) {
                if (hitTestFontUninstallButton(ci, mx, my)) {
                    onFontCardUninstall(ci);
                    return 0;
                }
                if (hitTestFontCardButton(ci, mx, my)) {
                    onFontCardClick(ci);
                    return 0;
                }
            }
        }

        // Content area clicks — theme card grid
        if (mx >= sidebarWidth_ && my >= kUsTopBarH &&
            selectedCategoryId_.find("theme") != std::string::npos) {
            int contentX = sidebarWidth_ + kUsContentPad;
            int contentY = kUsTopBarH + kUsContentPad + 40; // after title

            // Filter pill buttons (in the filter bar area)
            if (my >= contentY - (int)scrollY_ && my < contentY - (int)scrollY_ + 26) {
                int fb = hitTestThemeFilterButton(mx, my, contentX);
                if (fb >= 0) {
                    activeThemeFilter_ = static_cast<ThemeFilter>(fb);
                    rebuildThemeFilteredList();
                    scrollY_ = 0.f;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            // Card button clicks
            int ci = hitTestThemeCard(mx, my);
            if (ci >= 0 && hitTestThemeCardButton(ci, mx, my)) {
                onThemeCardApply(ci);
                return 0;
            }
        }

        // Content area clicks — settings items (toggle, number, slider, dropdown, color)
        if (mx >= sidebarWidth_ && my >= kUsTopBarH) {
            const SettingsCategory* cat = model_ ? model_->category(selectedCategoryId_) : nullptr;
            if (cat && cat->sectionType == SectionType::Settings) {
                onSettingsItemClick(mx, my);
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        if (sidebarResizing_) {
            updateSidebarResize(mx);
            return 0;
        }

        // Change cursor near sidebar border
        if (my >= kUsTopBarH && std::abs(mx - sidebarWidth_) <= 3) {
            SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
        } else {
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        endSidebarResize();
        return 0;
    }

    case WM_TIMER:
        if (wParam == 300) {
            KillTimer(hwnd, 300);
            fontFailedCard_ = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wParam == 301) {
            KillTimer(hwnd, 301);
            fontIndex_.refreshInstallStatus();
            rebuildFontFilteredList();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_APP + 10: {
        std::string installedFontName;
        if (fontInstallingCard_ >= 0 &&
            fontInstallingCard_ < (int)fontCardRects_.size()) {
            if (fontCardRects_[fontInstallingCard_].meta)
                installedFontName = fontCardRects_[fontInstallingCard_].meta->name;
        }
        if (wParam == 0) {
            fontFailedCard_ = fontInstallingCard_;
            fontInstallingCard_ = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            SetTimer(hwnd, 300, 3000, nullptr);
            return 0;
        }
        fontInstallingCard_ = -1;
        if (!installedFontName.empty())
            fontIndex_.markInstalled(installedFontName);
        fontIndex_.refreshInstallStatus();
        if (!installedFontName.empty())
            fontIndex_.markInstalled(installedFontName);
        rebuildFontFilteredList();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (inlineEdit_) { cancelInlineEdit(); return 0; }
            close();
            return 0;
        }
        if (wParam == VK_RETURN && inlineEdit_) {
            commitInlineEdit();
            return 0;
        }
        break;

    case WM_DESTROY:
        hwnd_ = nullptr;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// paintWindow - double-buffered main paint
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::paintWindow(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;

    // Double buffer
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    {
        Gdiplus::Graphics g(memDC);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        // Background
        Gdiplus::SolidBrush bgBr(toGdipColorCR(chrome_.background));
        g.FillRectangle(&bgBr, 0, 0, w, h);

        paintTopBar(g, w);
        paintSidebar(g, w, h);

        // Clip content region
        int contentLeft = sidebarWidth_;
        int contentTop = kUsTopBarH;
        int contentBottom = h - kUsBottomBarH;
        Gdiplus::Rect clipRect(contentLeft, contentTop,
                               w - contentLeft, contentBottom - contentTop);
        g.SetClip(clipRect);
        paintContent(g, w, h);
        g.ResetClip();

        paintBottomBar(g, w, h);
    }

    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

// ---------------------------------------------------------------------------
// paintTopBar
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::paintTopBar(Gdiplus::Graphics& g, int w) {
    // Dark background
    Gdiplus::SolidBrush barBr(toGdipColorCR(chrome_.titleBar));
    g.FillRectangle(&barBr, 0, 0, w, kUsTopBarH);

    // Search field background: centered in top bar
    float searchX = (w - kUsSearchW) / 2.f;
    float searchY = (kUsTopBarH - kUsSearchH) / 2.f;
    Gdiplus::SolidBrush fieldBr(toGdipColorCR(chrome_.fieldBg));
    drawRoundedRect(g, &fieldBr, searchX, searchY,
                    (float)kUsSearchW, (float)kUsSearchH, 6.f);

    // Clear (x) button when search text is non-empty
    Gdiplus::Font font(L"Segoe UI", 10.f);
    if (!searchText_.empty()) {
        float clearX = searchX + kUsSearchW - 24.f;
        float clearY = searchY + 4.f;
        Gdiplus::SolidBrush dimBr(toGdipColorCR(chrome_.dimText));
        Gdiplus::PointF clearPt(clearX, clearY);
        g.DrawString(L"\u00D7", -1, &font, clearPt, &dimBr);
    }

    // "Open Lua" button: right-aligned
    float btnW = 80.f, btnH = 26.f;
    float btnX = w - btnW - 12.f;
    float btnY = (kUsTopBarH - btnH) / 2.f;
    Gdiplus::SolidBrush btnBr(toGdipColorCR(chrome_.btnInactive));
    drawRoundedRect(g, &btnBr, btnX, btnY, btnW, btnH, 6.f);

    Gdiplus::SolidBrush txtBr(toGdipColorCR(chrome_.textColor));
    Gdiplus::PointF btnTextPt(btnX + 10.f, btnY + 4.f);
    g.DrawString(L"Open Lua", -1, &font, btnTextPt, &txtBr);
}

// ---------------------------------------------------------------------------
// paintBottomBar
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::paintBottomBar(Gdiplus::Graphics& g, int w, int h) {
    int barY = h - kUsBottomBarH;

    // Dark background
    Gdiplus::SolidBrush barBr(toGdipColorCR(chrome_.titleBar));
    g.FillRectangle(&barBr, 0, barY, w, kUsBottomBarH);

    // "BreadTerminal" text
    Gdiplus::Font font(L"Segoe UI", 8.f);
    Gdiplus::SolidBrush dimBr(toGdipColorCR(chrome_.dimText));
    Gdiplus::PointF textPt(8.f, (float)barY + 4.f);
    g.DrawString(L"BreadTerminal", -1, &font, textPt, &dimBr);
}

} // namespace termcore

#endif
