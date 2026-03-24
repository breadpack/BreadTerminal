#if defined(_WIN32)

#include "UnifiedSettingsWindow.h"

#include <set>

namespace termcore {

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
        repositionFontSearchEdit();
        InvalidateRect(hwnd_, nullptr, FALSE);
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
