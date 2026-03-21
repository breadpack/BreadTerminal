#if defined(_WIN32)

#include "UnifiedSettingsWindow.h"

#include <algorithm>
#include <cmath>

namespace termcore {

// ---------------------------------------------------------------------------
// Layout constants for theme cards within unified settings
// ---------------------------------------------------------------------------

static constexpr int kTcCardW       = 190;
static constexpr int kTcCardH       = 134;
static constexpr int kTcCardGap     = 12;
static constexpr int kTcFilterBtnW  = 72;
static constexpr int kTcFilterBtnH  = 26;
static constexpr int kTcFilterGap   = 6;
static constexpr int kTcSwatchSize  = 14;
static constexpr int kTcSwatchGap   = 3;
static constexpr int kTcSwatchRound = 2;
static constexpr int kTcFilterBarH  = 36;

static const wchar_t* kFilterLabels[] = { L"All", L"Dark", L"Light", L"Installed" };

// ---------------------------------------------------------------------------
// rebuildThemeFilteredList
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::rebuildThemeFilteredList() {
    filteredThemes_.clear();

    bool dark  = (activeThemeFilter_ == ThemeFilter::Dark);
    bool light = (activeThemeFilter_ == ThemeFilter::Light);
    bool inst  = (activeThemeFilter_ == ThemeFilter::Installed);

    if (dark || light || inst) {
        filteredThemes_ = themeIndex_.filterByCategory(dark, light, inst);
    } else {
        for (auto& t : themeIndex_.all())
            filteredThemes_.push_back(&t);
    }
}

// ---------------------------------------------------------------------------
// paintThemeCards
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::paintThemeCards(Gdiplus::Graphics& g,
                                             int x, int y, int w, int h) {
    // Rebuild filtered list if empty (first paint)
    if (filteredThemes_.empty() && themeIndex_.count() > 0) {
        rebuildThemeFilteredList();
    }

    float scrollOff = scrollY_;

    // --- Filter bar ---
    {
        float fx = (float)x;
        float fy = (float)y - scrollOff;

        Gdiplus::Font filterFont(L"Segoe UI", 9.f);

        for (int i = 0; i < 4; ++i) {
            bool active = (i == static_cast<int>(activeThemeFilter_));
            float bx = fx + i * (kTcFilterBtnW + kTcFilterGap);
            float by = fy;

            if (active) {
                Gdiplus::SolidBrush acBr(toGdipColorCR(chrome_.accent));
                drawRoundedRect(g, &acBr, bx, by,
                                (float)kTcFilterBtnW, (float)kTcFilterBtnH,
                                (float)kTcFilterBtnH / 2.f);
            } else {
                Gdiplus::SolidBrush btnBr(toGdipColorCR(chrome_.btnInactive));
                drawRoundedRect(g, &btnBr, bx, by,
                                (float)kTcFilterBtnW, (float)kTcFilterBtnH,
                                (float)kTcFilterBtnH / 2.f);
            }

            Gdiplus::RectF pillRect(bx, by, (float)kTcFilterBtnW, (float)kTcFilterBtnH);
            Gdiplus::StringFormat fmt;
            fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);

            Gdiplus::SolidBrush txtBr(
                active ? Gdiplus::Color(255, 255, 255, 255)
                       : toGdipColorCR(chrome_.textColor));
            g.DrawString(kFilterLabels[i], -1, &filterFont, pillRect, &fmt, &txtBr);
        }
    }

    // --- Card grid ---
    int gridTop = y + kTcFilterBarH;
    int usable = w + kTcCardGap;
    int columns = (std::max)(1, usable / (kTcCardW + kTcCardGap));

    themeCardRects_.clear();

    // Case-insensitive comparison helper
    auto iequal = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t j = 0; j < a.size(); ++j)
            if (tolower((unsigned char)a[j]) != tolower((unsigned char)b[j]))
                return false;
        return true;
    };

    for (size_t i = 0; i < filteredThemes_.size(); ++i) {
        int col = (int)(i % columns);
        int row = (int)(i / columns);
        int cx = x + col * (kTcCardW + kTcCardGap);
        int cy = gridTop + row * (kTcCardH + kTcCardGap) - (int)scrollOff;

        ThemeCardRect tcr;
        tcr.meta = filteredThemes_[i];
        tcr.cardRect = { cx, cy, cx + kTcCardW, cy + kTcCardH };
        tcr.buttonRect = { cx + kTcCardW - 68, cy + kTcCardH - 28,
                           cx + kTcCardW - 4,  cy + kTcCardH - 6 };
        tcr.isActive = false;
        if (tcr.meta) {
            if (!config_.theme.empty()) {
                tcr.isActive = iequal(tcr.meta->name, config_.theme)
                            || config_.theme.find(tcr.meta->name) != std::string::npos;
            } else {
                tcr.isActive = (tcr.meta->background == config_.background
                             && tcr.meta->foreground == config_.foreground);
            }
        }
        themeCardRects_.push_back(tcr);

        // Skip cards outside visible area
        if (tcr.cardRect.bottom < 0 || tcr.cardRect.top > y + h)
            continue;

        // --- Paint single card ---
        paintSingleThemeCard(g, tcr);
    }

    // "No themes found" message
    if (filteredThemes_.empty()) {
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font font(&ff, 14.f, Gdiplus::FontStyleRegular);
        Gdiplus::SolidBrush br(toGdipColorCR(chrome_.dimText));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF area((float)x, (float)gridTop - scrollOff,
                            (float)w, (float)h);
        g.DrawString(L"No themes found", -1, &font, area, &sf, &br);
    }
}

// ---------------------------------------------------------------------------
// paintSingleThemeCard
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::paintSingleThemeCard(Gdiplus::Graphics& g,
                                                  const ThemeCardRect& card) {
    if (!card.meta) return;

    float cx = (float)card.cardRect.left;
    float cy = (float)card.cardRect.top;
    float cw = (float)(card.cardRect.right - card.cardRect.left);
    float ch = (float)(card.cardRect.bottom - card.cardRect.top);
    float r = 8.f;

    // Card background rounded rect
    Gdiplus::GraphicsPath cardPath;
    cardPath.AddArc(cx, cy, r * 2, r * 2, 180.f, 90.f);
    cardPath.AddArc(cx + cw - r * 2, cy, r * 2, r * 2, 270.f, 90.f);
    cardPath.AddArc(cx + cw - r * 2, cy + ch - r * 2, r * 2, r * 2, 0.f, 90.f);
    cardPath.AddArc(cx, cy + ch - r * 2, r * 2, r * 2, 90.f, 90.f);
    cardPath.CloseFigure();

    Gdiplus::SolidBrush cardBr(toGdipColorCR(chrome_.cardBg));
    g.FillPath(&cardBr, &cardPath);

    // -- Top preview area (60px): fill with theme background + sample text --
    {
        Gdiplus::GraphicsPath previewClip;
        float ph = 60.f;
        previewClip.AddArc(cx, cy, r * 2, r * 2, 180.f, 90.f);
        previewClip.AddArc(cx + cw - r * 2, cy, r * 2, r * 2, 270.f, 90.f);
        previewClip.AddLine(cx + cw, cy + ph, cx, cy + ph);
        previewClip.CloseFigure();

        Gdiplus::SolidBrush bgBr(toGdipColor(card.meta->background));
        g.FillPath(&bgBr, &previewClip);

        // Sample text
        Gdiplus::FontFamily mono(L"Consolas");
        Gdiplus::Font monoFont(&mono, 9.f, Gdiplus::FontStyleRegular);
        Gdiplus::SolidBrush fgBr(toGdipColor(card.meta->foreground));

        Gdiplus::PointF tp1(cx + 8.f, cy + 8.f);
        Gdiplus::PointF tp2(cx + 8.f, cy + 24.f);
        Gdiplus::PointF tp3(cx + 8.f, cy + 40.f);
        g.DrawString(L"$ hello world", -1, &monoFont, tp1, &fgBr);
        g.DrawString(L"> echo $PATH", -1, &monoFont, tp2, &fgBr);

        // Use palette colors for a colorful line
        const auto& pal = card.meta->palette;
        Gdiplus::SolidBrush c1(toGdipColor(pal[1])); // red
        Gdiplus::SolidBrush c2(toGdipColor(pal[2])); // green
        Gdiplus::SolidBrush c4(toGdipColor(pal[4])); // blue
        g.DrawString(L"err", -1, &monoFont, tp3, &c1);
        Gdiplus::PointF tp3b(cx + 34.f, cy + 40.f);
        g.DrawString(L" ok", -1, &monoFont, tp3b, &c2);
        Gdiplus::PointF tp3c(cx + 58.f, cy + 40.f);
        g.DrawString(L" info", -1, &monoFont, tp3c, &c4);
    }

    // -- Middle: palette swatches (2 rows of 8) --
    {
        int sx0 = card.cardRect.left + 8;
        int sy0 = card.cardRect.top + 62;
        for (int i = 0; i < 16; ++i) {
            int col = i % 8;
            int row = i / 8;
            float sx = (float)(sx0 + col * (kTcSwatchSize + kTcSwatchGap));
            float sy = (float)(sy0 + row * (kTcSwatchSize + kTcSwatchGap));
            float ss = (float)kTcSwatchSize;
            float sr = (float)kTcSwatchRound;

            Gdiplus::GraphicsPath sp;
            sp.AddArc(sx, sy, sr * 2, sr * 2, 180.f, 90.f);
            sp.AddArc(sx + ss - sr * 2, sy, sr * 2, sr * 2, 270.f, 90.f);
            sp.AddArc(sx + ss - sr * 2, sy + ss - sr * 2, sr * 2, sr * 2, 0.f, 90.f);
            sp.AddArc(sx, sy + ss - sr * 2, sr * 2, sr * 2, 90.f, 90.f);
            sp.CloseFigure();

            Gdiplus::SolidBrush br(toGdipColor(card.meta->palette[i]));
            g.FillPath(&br, &sp);
        }
    }

    // -- Bottom: theme name --
    {
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font nameFont(&ff, 9.5f, Gdiplus::FontStyleRegular);
        Gdiplus::SolidBrush nameBr(toGdipColorCR(chrome_.textColor));

        std::wstring nameW = toWide(card.meta->name);
        Gdiplus::RectF nameRect(cx + 6.f, cy + ch - 28.f, cw - 74.f, 20.f);
        Gdiplus::StringFormat nsf;
        nsf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        nsf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        nsf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(nameW.c_str(), -1, &nameFont, nameRect, &nsf, &nameBr);
    }

    // -- Action button --
    {
        float bx = (float)card.buttonRect.left;
        float by = (float)card.buttonRect.top;
        float bw = (float)(card.buttonRect.right - card.buttonRect.left);
        float bh = (float)(card.buttonRect.bottom - card.buttonRect.top);
        float br = bh / 2.f;

        Gdiplus::GraphicsPath pill;
        pill.AddArc(bx, by, br * 2, br * 2, 180.f, 90.f);
        pill.AddArc(bx + bw - br * 2, by, br * 2, br * 2, 270.f, 90.f);
        pill.AddArc(bx + bw - br * 2, by + bh - br * 2, br * 2, br * 2, 0.f, 90.f);
        pill.AddArc(bx, by + bh - br * 2, br * 2, br * 2, 90.f, 90.f);
        pill.CloseFigure();

        const wchar_t* label;
        Gdiplus::Color fillColor;
        Gdiplus::Color textColor;

        if (card.isActive) {
            fillColor = toGdipColorCR(chrome_.activeGreen);
            textColor = Gdiplus::Color(255, 255, 255, 255);
            label = L"\x2713 Applied";
        } else {
            fillColor = toGdipColorCR(chrome_.accent);
            textColor = toGdipColorCR(chrome_.titleBar);
            label = L"Apply";
        }

        Gdiplus::SolidBrush fillBr(fillColor);
        g.FillPath(&fillBr, &pill);

        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font font(&ff, 8.5f, Gdiplus::FontStyleRegular);
        Gdiplus::SolidBrush txtBr(textColor);
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF lr(bx, by, bw, bh);
        g.DrawString(label, -1, &font, lr, &sf, &txtBr);
    }
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------

int UnifiedSettingsWindow::hitTestThemeCard(int mx, int my) const {
    for (int i = 0; i < (int)themeCardRects_.size(); ++i) {
        const RECT& r = themeCardRects_[i].cardRect;
        if (mx >= r.left && mx < r.right && my >= r.top && my < r.bottom)
            return i;
    }
    return -1;
}

bool UnifiedSettingsWindow::hitTestThemeCardButton(int idx, int mx, int my) const {
    if (idx < 0 || idx >= (int)themeCardRects_.size()) return false;
    const RECT& r = themeCardRects_[idx].buttonRect;
    return mx >= r.left && mx < r.right && my >= r.top && my < r.bottom;
}

int UnifiedSettingsWindow::hitTestThemeFilterButton(int mx, int my,
                                                     int contentX) const {
    // Filter buttons start at contentX, in the filter bar area
    int bx0 = contentX;
    // Vertical area: the filter bar is at the top of the theme content area
    // We need the caller to provide the y coordinate range

    for (int i = 0; i < 4; ++i) {
        int fx = bx0 + i * (kTcFilterBtnW + kTcFilterGap);
        if (mx >= fx && mx < fx + kTcFilterBtnW)
            return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// onThemeCardApply - apply a theme from the card grid
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::onThemeCardApply(int idx) {
    if (idx < 0 || idx >= (int)themeCardRects_.size()) return;
    const ThemeMetadata* meta = themeCardRects_[idx].meta;
    if (!meta) return;

    // Update config with theme colors
    config_.theme = meta->name;
    config_.background = meta->background;
    config_.foreground = meta->foreground;
    for (int i = 0; i < 16; ++i)
        config_.palette[i] = meta->palette[i];

    // Re-derive chrome colors
    chrome_ = deriveChrome(config_.background, config_.foreground, config_.palette);

    notifySave();

    if (hwnd_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

} // namespace termcore

#endif
