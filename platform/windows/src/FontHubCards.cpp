#if defined(_WIN32)

#include "FontHubWindow.h"

#include <algorithm>
#include <cmath>

namespace termcore {

// ---------------------------------------------------------------------------
// paintCards - iterate visible cards
// ---------------------------------------------------------------------------

void FontHubWindow::paintCards(Gdiplus::Graphics& g, int w, int h) {
    recalcCardLayout(w);

    int gridTop = kFhTitleH + kFhToolbarH;
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
            Gdiplus::GraphicsPath border;
            drawRoundedRect(border, cx, cy, cw, ch, 8.f);
            g.DrawPath(&pen, &border);
        }
    }

    // "No fonts found" message
    if (visibleCards_.empty()) {
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font font(&ff, 14.f, Gdiplus::FontStyleRegular);
        Gdiplus::SolidBrush br(toGdipColorCR(chrome_.dimText));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF area(0.f, (float)gridTop, (float)w,
                            (float)(gridBot - gridTop));
        g.DrawString(L"No fonts found", -1, &font, area, &sf, &br);
    }
}

// ---------------------------------------------------------------------------
// paintSingleCard
// ---------------------------------------------------------------------------

void FontHubWindow::paintSingleCard(Gdiplus::Graphics& g,
                                     const FontCardInfo& card) {
    if (!card.meta) return;

    float cx = (float)card.cardRect.left;
    float cy = (float)card.cardRect.top;
    float cw = (float)(card.cardRect.right - card.cardRect.left);
    float ch = (float)(card.cardRect.bottom - card.cardRect.top);
    float r = 8.f;

    // Card background rounded rect
    Gdiplus::GraphicsPath cardPath;
    drawRoundedRect(cardPath, cx, cy, cw, ch, r);

    // Installed cards get normal card bg; uninstalled get dimmed bg
    COLORREF cardColor = (card.meta->installed)
        ? chrome_.cardBg
        : darkenCR(chrome_.cardBg, 0.82f);
    Gdiplus::SolidBrush cardBr(toGdipColorCR(cardColor));
    g.FillPath(&cardBr, &cardPath);

    // Installed indicator: green left border
    if (card.meta->installed) {
        Gdiplus::Pen greenPen(toGdipColorCR(chrome_.activeGreen), 3.f);
        g.DrawLine(&greenPen, cx + 1.5f, cy + r, cx + 1.5f, cy + ch - r);
    }

    // -- Top preview area (80px) --
    paintFontPreview(g, card, cx, cy, cw);

    // -- Badge strip (22px, starting at cy+80) --
    float badgeY = cy + (float)kFhPreviewH;
    Gdiplus::SolidBrush stripBr(Gdiplus::Color(255, 20, 20, 34));
    g.FillRectangle(&stripBr, cx, badgeY, cw, (float)kFhBadgeStripH);
    paintBadges(g, *card.meta, cx + 8.f, badgeY + 2.f, cw - 16.f);

    // -- Bottom bar (28px): font name + button --
    {
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font nameFont(&ff, 9.5f, Gdiplus::FontStyleRegular);
        Gdiplus::SolidBrush nameBr(toGdipColorCR(chrome_.textColor));

        std::wstring nameW = toWide(card.meta->name);
        float nameY = cy + (float)kFhPreviewH + (float)kFhBadgeStripH;
        Gdiplus::RectF nameRect(cx + 8.f, nameY + 4.f, cw - 80.f, 20.f);
        Gdiplus::StringFormat nsf;
        nsf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        nsf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        nsf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(nameW.c_str(), -1, &nameFont, nameRect, &nsf, &nameBr);
    }

    // Action button
    paintCardButton(g, card);

    // Uninstall "×" button for installed (non-active) fonts
    if (card.meta->installed && !card.isActive && !card.isInstalling) {
        float ux = (float)card.uninstallRect.left;
        float uy = (float)card.uninstallRect.top;
        float uw = (float)(card.uninstallRect.right - card.uninstallRect.left);
        float uh = (float)(card.uninstallRect.bottom - card.uninstallRect.top);

        // Semi-transparent circle background
        Gdiplus::SolidBrush circleBr(Gdiplus::Color(160, 60, 60, 60));
        g.FillEllipse(&circleBr, ux, uy, uw, uh);

        // "×" text
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font xFont(&ff, 9.f, Gdiplus::FontStyleBold);
        Gdiplus::SolidBrush xBr(Gdiplus::Color(200, 220, 220, 220));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF xRect(ux, uy, uw, uh);
        g.DrawString(L"\x00D7", -1, &xFont, xRect, &sf, &xBr);
    }
}

// ---------------------------------------------------------------------------
// paintFontPreview - top 80px of card
// ---------------------------------------------------------------------------

void FontHubWindow::paintFontPreview(Gdiplus::Graphics& g,
                                      const FontCardInfo& card,
                                      float cx, float cy, float cw) {
    float r = 8.f;
    float ph = (float)kFhPreviewH;

    // Clip preview to top rounded corners
    Gdiplus::GraphicsPath previewClip;
    previewClip.AddArc(cx, cy, r * 2, r * 2, 180.f, 90.f);
    previewClip.AddArc(cx + cw - r * 2, cy, r * 2, r * 2, 270.f, 90.f);
    previewClip.AddLine(cx + cw, cy + ph, cx, cy + ph);
    previewClip.CloseFigure();

    // Dark preview background
    Gdiplus::SolidBrush previewBg(toGdipColorCR(chrome_.previewBg));
    g.FillPath(&previewBg, &previewClip);

    if (card.meta->installed) {
        // Try to render with the actual font
        std::wstring fontNameW = toWide(card.meta->name);
        Gdiplus::FontFamily family(fontNameW.c_str());

        // Fallback to Consolas if font family not available in GDI+
        bool useFallback = !family.IsAvailable();
        Gdiplus::FontFamily fallback(L"Consolas");
        Gdiplus::FontFamily& usedFamily = useFallback ? fallback : family;

        Gdiplus::Font previewFont(&usedFamily, 14.f, Gdiplus::FontStyleRegular);
        Gdiplus::Font previewFontSm(&usedFamily, 12.f, Gdiplus::FontStyleRegular);
        Gdiplus::SolidBrush fgBr(Gdiplus::Color(255, 220, 220, 240));

        // Line 1: "AaBb 0123"
        Gdiplus::PointF pt1(cx + 12.f, cy + 12.f);
        g.DrawString(L"AaBb 0123", -1, &previewFont, pt1, &fgBr);

        // Line 2: "!= => ->"
        Gdiplus::PointF pt2(cx + 12.f, cy + 36.f);
        g.DrawString(L"!= => ->", -1, &previewFont, pt2, &fgBr);

        // "Not Installed" overlay if using fallback
        if (useFallback) {
            Gdiplus::SolidBrush dimBr(toGdipColorCR(chrome_.dimText, 160));
            Gdiplus::FontFamily uiFF(L"Segoe UI");
            Gdiplus::Font dimFont(&uiFF, 8.f, Gdiplus::FontStyleItalic);
            Gdiplus::PointF ptDim(cx + cw - 80.f, cy + ph - 16.f);
            g.DrawString(L"(fallback)", -1, &dimFont, ptDim, &dimBr);
        }
    } else {
        // Gradient placeholder with font name
        // Simulate gradient with two rectangles
        Gdiplus::SolidBrush grad1(Gdiplus::Color(255, 38, 38, 58));
        Gdiplus::SolidBrush grad2(Gdiplus::Color(255, 28, 28, 48));
        g.FillRectangle(&grad1, cx, cy, cw, ph / 2.f);
        g.FillRectangle(&grad2, cx, cy + ph / 2.f, cw, ph / 2.f);

        // Font name centered
        std::wstring nameW = toWide(card.meta->name);
        Gdiplus::FontFamily uiFF(L"Segoe UI");
        Gdiplus::Font nameFont(&uiFF, 13.f, Gdiplus::FontStyleRegular);
        Gdiplus::SolidBrush nameBr(Gdiplus::Color(255, 160, 160, 180));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF previewArea(cx, cy, cw, ph);
        g.DrawString(nameW.c_str(), -1, &nameFont, previewArea, &sf, &nameBr);

        // Small "Not Installed" text below
        Gdiplus::Font smallFont(&uiFF, 8.f, Gdiplus::FontStyleItalic);
        Gdiplus::SolidBrush dimBr(toGdipColorCR(chrome_.dimText, 140));
        Gdiplus::RectF subArea(cx, cy + ph * 0.55f, cw, ph * 0.4f);
        g.DrawString(L"Not Installed", -1, &smallFont, subArea, &sf, &dimBr);
    }
}

// ---------------------------------------------------------------------------
// paintBadges - ligatures + nerd font pills
// ---------------------------------------------------------------------------

void FontHubWindow::paintBadges(Gdiplus::Graphics& g,
                                 const FontMetadata& meta,
                                 float x, float y, float maxW) {
    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font badgeFont(&ff, 8.f, Gdiplus::FontStyleBold);

    float curX = x;
    float badgePadH = 8.f;
    float badgePadV = 2.f;
    float badgeH = 16.f;
    float badgeGap = 4.f;

    auto drawBadge = [&](const wchar_t* text, COLORREF color) {
        // Measure text
        Gdiplus::RectF measureRect;
        g.MeasureString(text, -1, &badgeFont, Gdiplus::PointF(0, 0), &measureRect);
        float badgeW = measureRect.Width + badgePadH * 2;

        if (curX + badgeW > x + maxW) return; // doesn't fit

        // Pill background
        Gdiplus::GraphicsPath pill;
        drawRoundedRect(pill, curX, y, badgeW, badgeH, badgeH / 2.f);
        Gdiplus::SolidBrush pillBr(toGdipColorCR(color, 200));
        g.FillPath(&pillBr, &pill);

        // Text
        Gdiplus::SolidBrush txtBr(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF textArea(curX, y, badgeW, badgeH);
        g.DrawString(text, -1, &badgeFont, textArea, &sf, &txtBr);

        curX += badgeW + badgeGap;
    };

    if (meta.installed) {
        drawBadge(L"\x2713 Installed", chrome_.activeGreen);
    }
    if (meta.has_ligatures) {
        drawBadge(L"Ligatures", chrome_.accent);
    }
    if (meta.has_nerd_font_variant) {
        drawBadge(L"Nerd Font", chrome_.badgeSecondary);
    }
}

// ---------------------------------------------------------------------------
// paintCardButton
// ---------------------------------------------------------------------------

void FontHubWindow::paintCardButton(Gdiplus::Graphics& g,
                                     const FontCardInfo& card) {
    float bx = (float)card.buttonRect.left;
    float by = (float)card.buttonRect.top;
    float bw = (float)(card.buttonRect.right - card.buttonRect.left);
    float bh = (float)(card.buttonRect.bottom - card.buttonRect.top);

    // Button pill path
    Gdiplus::GraphicsPath pill;
    drawRoundedRect(pill, bx, by, bw, bh, bh / 2.f);

    const wchar_t* label;
    Gdiplus::Color fillColor;
    Gdiplus::Color textColor;

    if (card.isFailed) {
        fillColor = Gdiplus::Color(255, 180, 60, 60);
        textColor = Gdiplus::Color(255, 255, 255, 255);
        label = L"Failed";
    } else if (card.isInstalling) {
        fillColor = toGdipColorCR(chrome_.dimText);
        textColor = Gdiplus::Color(255, 255, 255, 255);
        label = L"Installing...";
    } else if (card.isActive) {
        fillColor = toGdipColorCR(chrome_.activeGreen);
        textColor = Gdiplus::Color(255, 255, 255, 255);
        label = L"\x2713 Applied";
    } else if (card.meta && card.meta->installed) {
        fillColor = toGdipColorCR(chrome_.accent);
        textColor = Gdiplus::Color(255, 255, 255, 255);
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

int FontHubWindow::hitTestCard(int mx, int my) const {
    for (int i = 0; i < (int)visibleCards_.size(); ++i) {
        const RECT& r = visibleCards_[i].cardRect;
        if (mx >= r.left && mx < r.right && my >= r.top && my < r.bottom)
            return i;
    }
    return -1;
}

bool FontHubWindow::hitTestCardButton(int idx, int mx, int my) const {
    if (idx < 0 || idx >= (int)visibleCards_.size()) return false;
    const RECT& r = visibleCards_[idx].buttonRect;
    return mx >= r.left && mx < r.right && my >= r.top && my < r.bottom;
}

bool FontHubWindow::hitTestUninstallButton(int idx, int mx, int my) const {
    if (idx < 0 || idx >= (int)visibleCards_.size()) return false;
    const auto& card = visibleCards_[idx];
    if (!card.meta || !card.meta->installed || card.isActive) return false;
    const RECT& r = card.uninstallRect;
    return mx >= r.left && mx < r.right && my >= r.top && my < r.bottom;
}

bool FontHubWindow::hitTestCloseButton(int mx, int my) const {
    int bx = kFhWinWidth - 32;
    return mx >= bx && mx < kFhWinWidth && my >= 4 && my < 28;
}

int FontHubWindow::hitTestFilterButton(int mx, int my) const {
    int w = kFhWinWidth;
    int bx0 = w - kFhGridPad - 4 * (kFhFilterBtnW + kFhFilterGap) + kFhFilterGap;
    int by0 = kFhTitleH + 9;
    int byEnd = by0 + kFhFilterBtnH;

    if (my < by0 || my > byEnd) return -1;

    for (int i = 0; i < 4; ++i) {
        int fx = bx0 + i * (kFhFilterBtnW + kFhFilterGap);
        if (mx >= fx && mx < fx + kFhFilterBtnW)
            return i;
    }
    return -1;
}

} // namespace termcore

#endif
