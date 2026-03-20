#if defined(_WIN32)

#include "ThemeHubWindow.h"

#include <algorithm>
#include <cmath>

namespace termcore {

// ---------------------------------------------------------------------------
// paintCards - iterate visible cards
// ---------------------------------------------------------------------------

void ThemeHubWindow::paintCards(Gdiplus::Graphics& g, int w, int h) {
    recalcCardLayout(w);

    int gridTop = kThTitleH + kThToolbarH;
    int gridBot = h;

    for (int i = 0; i < (int)visibleCards_.size(); ++i) {
        const auto& card = visibleCards_[i];
        // Skip if entirely outside visible area
        if (card.cardRect.bottom < gridTop || card.cardRect.top > gridBot)
            continue;
        paintSingleCard(g, card);

        // Hover border
        if (i == hoveredCard_) {
            Gdiplus::Pen pen(toGdipColorCR(chrome_.accent), 2.f);
            float cx = (float)card.cardRect.left;
            float cy = (float)card.cardRect.top;
            float cw = (float)(card.cardRect.right - card.cardRect.left);
            float ch = (float)(card.cardRect.bottom - card.cardRect.top);
            float r = 8.f;
            Gdiplus::GraphicsPath border;
            border.AddArc(cx, cy, r * 2, r * 2, 180.f, 90.f);
            border.AddArc(cx + cw - r * 2, cy, r * 2, r * 2, 270.f, 90.f);
            border.AddArc(cx + cw - r * 2, cy + ch - r * 2, r * 2, r * 2, 0.f, 90.f);
            border.AddArc(cx, cy + ch - r * 2, r * 2, r * 2, 90.f, 90.f);
            border.CloseFigure();
            g.DrawPath(&pen, &border);
        }
    }

    // "No themes found" message
    if (visibleCards_.empty()) {
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font font(&ff, 14.f, Gdiplus::FontStyleRegular);
        Gdiplus::SolidBrush br(toGdipColorCR(chrome_.dimText));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF area(0.f, (float)gridTop, (float)w, (float)(gridBot - gridTop));
        g.DrawString(L"No themes found", -1, &font, area, &sf, &br);
    }
}

// ---------------------------------------------------------------------------
// paintSingleCard
// ---------------------------------------------------------------------------

void ThemeHubWindow::paintSingleCard(Gdiplus::Graphics& g,
                                      const ThemeCardInfo& card) {
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

    // -- Middle: palette swatches (42px area, starting at cy+60) --
    paintPalette(g, *card.meta,
                 card.cardRect.left + 8, card.cardRect.top + 62);

    // -- Bottom: theme name + button --
    {
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font nameFont(&ff, 9.5f, Gdiplus::FontStyleRegular);
        Gdiplus::SolidBrush nameBr(toGdipColorCR(chrome_.textColor));

        // Theme name - truncate to fit
        std::wstring nameW;
        {
            const auto& n = card.meta->name;
            int sz = MultiByteToWideChar(CP_UTF8, 0, n.c_str(), -1, nullptr, 0);
            nameW.resize(sz - 1);
            MultiByteToWideChar(CP_UTF8, 0, n.c_str(), -1, nameW.data(), sz);
        }
        Gdiplus::RectF nameRect(cx + 6.f, cy + ch - 28.f, cw - 74.f, 20.f);
        Gdiplus::StringFormat nsf;
        nsf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        nsf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        nsf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(nameW.c_str(), -1, &nameFont, nameRect, &nsf, &nameBr);
    }

    // Action button
    paintCardButton(g, card);
}

// ---------------------------------------------------------------------------
// paintPalette - 2 rows of 8 color swatches
// ---------------------------------------------------------------------------

void ThemeHubWindow::paintPalette(Gdiplus::Graphics& g,
                                   const ThemeMetadata& meta,
                                   int x, int y) {
    for (int i = 0; i < 16; ++i) {
        int col = i % 8;
        int row = i / 8;
        float sx = (float)(x + col * (kThSwatchSize + kThSwatchGap));
        float sy = (float)(y + row * (kThSwatchSize + kThSwatchGap));
        float ss = (float)kThSwatchSize;
        float sr = (float)kThSwatchRound;

        Gdiplus::GraphicsPath sp;
        sp.AddArc(sx, sy, sr * 2, sr * 2, 180.f, 90.f);
        sp.AddArc(sx + ss - sr * 2, sy, sr * 2, sr * 2, 270.f, 90.f);
        sp.AddArc(sx + ss - sr * 2, sy + ss - sr * 2, sr * 2, sr * 2, 0.f, 90.f);
        sp.AddArc(sx, sy + ss - sr * 2, sr * 2, sr * 2, 90.f, 90.f);
        sp.CloseFigure();

        Gdiplus::SolidBrush br(toGdipColor(meta.palette[i]));
        g.FillPath(&br, &sp);
    }
}

// ---------------------------------------------------------------------------
// paintCardButton
// ---------------------------------------------------------------------------

void ThemeHubWindow::paintCardButton(Gdiplus::Graphics& g,
                                      const ThemeCardInfo& card) {
    float bx = (float)card.buttonRect.left;
    float by = (float)card.buttonRect.top;
    float bw = (float)(card.buttonRect.right - card.buttonRect.left);
    float bh = (float)(card.buttonRect.bottom - card.buttonRect.top);
    float br = bh / 2.f;

    // Button pill path
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
        label = L"\x2713 Active";
    } else if (card.meta && card.meta->installed) {
        fillColor = toGdipColorCR(chrome_.accent);
        textColor = toGdipColorCR(chrome_.titleBar);
        label = L"Apply";
    } else {
        fillColor = toGdipColorCR(chrome_.btnInactive);
        textColor = toGdipColorCR(chrome_.textColor);
        label = L"Install";
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

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------

int ThemeHubWindow::hitTestCard(int mx, int my) const {
    for (int i = 0; i < (int)visibleCards_.size(); ++i) {
        const RECT& r = visibleCards_[i].cardRect;
        if (mx >= r.left && mx < r.right && my >= r.top && my < r.bottom)
            return i;
    }
    return -1;
}

bool ThemeHubWindow::hitTestCardButton(int idx, int mx, int my) const {
    if (idx < 0 || idx >= (int)visibleCards_.size()) return false;
    const RECT& r = visibleCards_[idx].buttonRect;
    return mx >= r.left && mx < r.right && my >= r.top && my < r.bottom;
}

bool ThemeHubWindow::hitTestCloseButton(int mx, int my) const {
    // Close button is at top-right, roughly a 24x24 area
    int bx = kThWinWidth - 32;
    return mx >= bx && mx < kThWinWidth && my >= 4 && my < 28;
}

int ThemeHubWindow::hitTestFilterButton(int mx, int my) const {
    int w = kThWinWidth;
    int bx0 = w - kThGridPad - 4 * (kThFilterBtnW + kThFilterGap) + kThFilterGap;
    int by0 = kThTitleH + 9;
    int byEnd = by0 + kThFilterBtnH;

    if (my < by0 || my > byEnd) return -1;

    for (int i = 0; i < 4; ++i) {
        int fx = bx0 + i * (kThFilterBtnW + kThFilterGap);
        if (mx >= fx && mx < fx + kThFilterBtnW)
            return i;
    }
    return -1;
}

} // namespace termcore

#endif
