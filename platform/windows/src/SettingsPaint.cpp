#if defined(_WIN32)

#include "SettingsWindow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace termcore {

// ---------------------------------------------------------------------------
// paintWindow - double-buffered main paint
// ---------------------------------------------------------------------------

void SettingsWindow::paintWindow(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;

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

        paintTitleBar(g, w);
        paintTabBar(g, w);

        // Content area
        int cx = kSetPadding;
        int cy = contentTop() + kSetPadding;
        int cw = w - 2 * kSetPadding;
        int ch = h - cy - kSetPadding;

        Gdiplus::Rect clipRect(0, contentTop(), w, h - contentTop());
        g.SetClip(clipRect);

        switch (activeTab_) {
        case SettingsTab::General:    paintGeneralTab(g, cx, cy, cw, ch); break;
        case SettingsTab::Appearance: paintAppearanceTab(g, cx, cy, cw, ch); break;
        case SettingsTab::Font:       paintFontTab(g, cx, cy, cw, ch); break;
        case SettingsTab::Keys:       paintKeysTab(g, cx, cy, cw, ch); break;
        case SettingsTab::Clipboard:  paintClipboardTab(g, cx, cy, cw, ch); break;
        }

        g.ResetClip();
    }

    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

// ---------------------------------------------------------------------------
// paintTitleBar
// ---------------------------------------------------------------------------

void SettingsWindow::paintTitleBar(Gdiplus::Graphics& g, int w) {
    Gdiplus::SolidBrush br(toGdipColorCR(chrome_.titleBar));
    g.FillRectangle(&br, 0, 0, w, kSetTitleH);

    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font font(&ff, 12.f, Gdiplus::FontStyleBold);
    Gdiplus::SolidBrush txtBr(toGdipColorCR(chrome_.textColor));
    Gdiplus::PointF pt(12.f, 7.f);
    g.DrawString(L"Settings", -1, &font, pt, &txtBr);

    // Close button (X)
    Gdiplus::Pen pen(toGdipColorCR(chrome_.dimText), 1.5f);
    float cx = (float)(w - 20), cy = 10.f;
    g.DrawLine(&pen, cx, cy, cx + 12.f, cy + 12.f);
    g.DrawLine(&pen, cx + 12.f, cy, cx, cy + 12.f);
}

// ---------------------------------------------------------------------------
// paintTabBar
// ---------------------------------------------------------------------------

void SettingsWindow::paintTabBar(Gdiplus::Graphics& g, int w) {
    int y0 = kSetTitleH;

    // Tab bar background (slightly darker)
    Gdiplus::SolidBrush barBr(toGdipColorCR(chrome_.titleBar, 200));
    g.FillRectangle(&barBr, 0, y0, w, kSetTabBarH);

    // Bottom line
    Gdiplus::Pen linePen(toGdipColorCR(chrome_.btnInactive), 1.f);
    g.DrawLine(&linePen, 0.f, (float)(y0 + kSetTabBarH - 1),
               (float)w, (float)(y0 + kSetTabBarH - 1));

    const wchar_t* tabLabels[] = {
        L"General", L"Appearance", L"Font", L"Keys", L"Clipboard"
    };

    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font tabFont(&ff, 10.f, Gdiplus::FontStyleRegular);
    float tabW = (float)w / kSetTabCount;

    for (int i = 0; i < kSetTabCount; ++i) {
        bool active = (i == (int)activeTab_);
        float tx = i * tabW;

        // Tab text
        Gdiplus::SolidBrush txtBr(active
            ? toGdipColorCR(chrome_.textColor)
            : toGdipColorCR(chrome_.dimText));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF tabRect(tx, (float)y0, tabW, (float)kSetTabBarH);
        g.DrawString(tabLabels[i], -1, &tabFont, tabRect, &sf, &txtBr);

        // Active tab underline
        if (active) {
            Gdiplus::SolidBrush accentBr(toGdipColorCR(chrome_.accent));
            float lineY = (float)(y0 + kSetTabBarH - 3);
            float pad = tabW * 0.15f;
            g.FillRectangle(&accentBr, tx + pad, lineY,
                            tabW - 2 * pad, 3.f);
        }
    }
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

void SettingsWindow::drawLabel(Gdiplus::Graphics& g, const wchar_t* text,
                               float x, float y) {
    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font font(&ff, 10.f, Gdiplus::FontStyleRegular);
    Gdiplus::SolidBrush br(toGdipColorCR(chrome_.textColor));
    Gdiplus::PointF pt(x, y + 4.f);
    g.DrawString(text, -1, &font, pt, &br);
}

void SettingsWindow::drawTextField(Gdiplus::Graphics& g, const wchar_t* text,
                                   float x, float y, float w, bool focused) {
    float h = (float)kSetFieldH;
    float r = 6.f;

    // Background
    Gdiplus::SolidBrush bgBr(toGdipColorCR(chrome_.fieldBg));
    drawRoundedRect(g, &bgBr, x, y, w, h, r);

    // Focus border
    if (focused) {
        Gdiplus::Pen pen(toGdipColorCR(chrome_.accent), 1.5f);
        drawRoundedRectOutline(g, &pen, x, y, w, h, r);
    }

    // Text
    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font font(&ff, 10.f, Gdiplus::FontStyleRegular);
    Gdiplus::SolidBrush txtBr(toGdipColorCR(chrome_.textColor));
    Gdiplus::StringFormat sf;
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    Gdiplus::RectF textRect(x + 8.f, y, w - 16.f, h);
    g.DrawString(text, -1, &font, textRect, &sf, &txtBr);
}

void SettingsWindow::drawPillButtons(Gdiplus::Graphics& g,
                                     const wchar_t** labels, int count,
                                     int selected, float x, float y) {
    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font font(&ff, 9.f, Gdiplus::FontStyleRegular);
    float pw = (float)kSetPillBtnW;
    float ph = (float)kSetPillBtnH;
    float gap = (float)kSetPillGap;
    float r = ph / 2.f;

    for (int i = 0; i < count; ++i) {
        bool active = (i == selected);
        float px = x + i * (pw + gap);

        Gdiplus::Color fill = active
            ? toGdipColorCR(chrome_.accent)
            : toGdipColorCR(chrome_.btnInactive);
        Gdiplus::SolidBrush fillBr(fill);
        drawRoundedRect(g, &fillBr, px, y, pw, ph, r);

        Gdiplus::SolidBrush txtBr(active
            ? toGdipColorCR(chrome_.titleBar)
            : toGdipColorCR(chrome_.textColor));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF lr(px, y, pw, ph);
        g.DrawString(labels[i], -1, &font, lr, &sf, &txtBr);
    }
}

void SettingsWindow::drawToggle(Gdiplus::Graphics& g, bool on,
                                float x, float y) {
    float w = (float)kSetToggleW;
    float h = (float)kSetToggleH;
    float r = h / 2.f;

    // Track
    Gdiplus::Color trackColor = on
        ? toGdipColorCR(chrome_.accent)
        : toGdipColorCR(chrome_.btnInactive);
    Gdiplus::SolidBrush trackBr(trackColor);
    drawRoundedRect(g, &trackBr, x, y, w, h, r);

    // Circle knob
    float circR = h * 0.38f;
    float cx = on ? (x + w - r) : (x + r);
    float cy = y + h / 2.f;
    Gdiplus::SolidBrush knobBr(Gdiplus::Color(255, 255, 255, 255));
    g.FillEllipse(&knobBr, cx - circR, cy - circR, circR * 2, circR * 2);
}

void SettingsWindow::drawColorSwatch(Gdiplus::Graphics& g, uint32_t color,
                                     float x, float y) {
    float sz = (float)kSetSwatchSize;
    float r = 4.f;

    Gdiplus::SolidBrush br(toGdipColor(color));
    drawRoundedRect(g, &br, x, y, sz, sz, r);

    // Border
    Gdiplus::Pen pen(toGdipColorCR(chrome_.dimText), 1.f);
    drawRoundedRectOutline(g, &pen, x, y, sz, sz, r);
}

void SettingsWindow::drawSlider(Gdiplus::Graphics& g, float value,
                                float x, float y, float w) {
    float h = (float)kSetSliderH;
    float trackH = 4.f;
    float trackY = y + h / 2.f - trackH / 2.f;
    float r = trackH / 2.f;

    // Track background
    Gdiplus::SolidBrush trackBg(toGdipColorCR(chrome_.btnInactive));
    drawRoundedRect(g, &trackBg, x, trackY, w, trackH, r);

    // Filled portion
    float fillW = w * value;
    if (fillW > 0) {
        Gdiplus::SolidBrush fillBr(toGdipColorCR(chrome_.accent));
        drawRoundedRect(g, &fillBr, x, trackY, fillW, trackH, r);
    }

    // Handle circle
    float handleR = 8.f;
    float handleX = x + fillW;
    float handleY = y + h / 2.f;
    Gdiplus::SolidBrush handleBr(toGdipColorCR(chrome_.accent));
    g.FillEllipse(&handleBr, handleX - handleR, handleY - handleR,
                  handleR * 2, handleR * 2);

    // White inner circle
    Gdiplus::SolidBrush innerBr(Gdiplus::Color(255, 255, 255, 255));
    g.FillEllipse(&innerBr, handleX - 3.f, handleY - 3.f, 6.f, 6.f);
}

void SettingsWindow::drawRoundedRect(Gdiplus::Graphics& g,
                                     Gdiplus::Brush* brush,
                                     float x, float y, float w, float h,
                                     float r) {
    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, r * 2, r * 2, 180.f, 90.f);
    path.AddArc(x + w - r * 2, y, r * 2, r * 2, 270.f, 90.f);
    path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0.f, 90.f);
    path.AddArc(x, y + h - r * 2, r * 2, r * 2, 90.f, 90.f);
    path.CloseFigure();
    g.FillPath(brush, &path);
}

void SettingsWindow::drawRoundedRectOutline(Gdiplus::Graphics& g,
                                            Gdiplus::Pen* pen,
                                            float x, float y, float w,
                                            float h, float r) {
    Gdiplus::GraphicsPath path;
    path.AddArc(x, y, r * 2, r * 2, 180.f, 90.f);
    path.AddArc(x + w - r * 2, y, r * 2, r * 2, 270.f, 90.f);
    path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0.f, 90.f);
    path.AddArc(x, y + h - r * 2, r * 2, r * 2, 90.f, 90.f);
    path.CloseFigure();
    g.DrawPath(pen, &path);
}

// ---------------------------------------------------------------------------
// Tab content: General
// ---------------------------------------------------------------------------

void SettingsWindow::paintGeneralTab(Gdiplus::Graphics& g,
                                     int x, int y, int w, int h) {
    float fx = (float)x;
    float fy = (float)y;
    float labelX = fx;
    float fieldX = fx + kSetLabelW;
    float fieldW = (float)kSetFieldW;
    float row = (float)kSetRowHeight;

    // Shell
    drawLabel(g, L"Shell", labelX, fy);
    std::wstring shellVal = config_.shell.empty()
        ? L"default" : toWide(config_.shell);
    drawTextField(g, shellVal.c_str(), fieldX, fy, fieldW,
                  editFieldId_ == 0);
    fy += row;

    // Scrollback
    drawLabel(g, L"Scrollback Lines", labelX, fy);
    wchar_t scrollBuf[32];
    swprintf(scrollBuf, 32, L"%d", config_.scrollback_limit);
    drawTextField(g, scrollBuf, fieldX, fy, fieldW,
                  editFieldId_ == 1);
    fy += row;

    // Cursor Style
    drawLabel(g, L"Cursor Style", labelX, fy);
    const wchar_t* cursorLabels[] = { L"Block", L"Underline", L"Bar" };
    int cursorSel = 0;
    if (config_.cursor_style == "underline") cursorSel = 1;
    else if (config_.cursor_style == "bar") cursorSel = 2;
    drawPillButtons(g, cursorLabels, 3, cursorSel, fieldX, fy);
    fy += row;

    // Cursor Blink
    drawLabel(g, L"Cursor Blink", labelX, fy);
    drawToggle(g, config_.cursor_blink, fieldX, fy + 2.f);
}

// ---------------------------------------------------------------------------
// Tab content: Appearance
// ---------------------------------------------------------------------------

void SettingsWindow::paintAppearanceTab(Gdiplus::Graphics& g,
                                        int x, int y, int w, int h) {
    float fx = (float)x;
    float fy = (float)y;
    float labelX = fx;
    float fieldX = fx + kSetLabelW;
    float row = (float)kSetRowHeight;

    // Background Opacity
    drawLabel(g, L"Background Opacity", labelX, fy);
    drawSlider(g, config_.background_opacity, fieldX, fy + 2.f,
               (float)kSetSliderW);
    // Value label
    {
        wchar_t valBuf[16];
        swprintf(valBuf, 16, L"%d%%",
                 (int)(config_.background_opacity * 100.f + 0.5f));
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font font(&ff, 10.f, Gdiplus::FontStyleRegular);
        Gdiplus::SolidBrush br(toGdipColorCR(chrome_.textColor));
        Gdiplus::PointF pt(fieldX + kSetSliderW + 12.f, fy + 2.f);
        g.DrawString(valBuf, -1, &font, pt, &br);
    }
    fy += row;

    // Background Blur
    drawLabel(g, L"Background Blur", labelX, fy);
    const wchar_t* blurLabels[] = { L"None", L"Low", L"Medium", L"High" };
    drawPillButtons(g, blurLabels, 4, config_.background_blur, fieldX, fy);
    fy += row;

    // Background Color
    drawLabel(g, L"Background Color", labelX, fy);
    drawColorSwatch(g, config_.background, fieldX, fy);
    fy += row;

    // Foreground Color
    drawLabel(g, L"Foreground Color", labelX, fy);
    drawColorSwatch(g, config_.foreground, fieldX, fy);
    fy += row;

    // Cursor Color
    drawLabel(g, L"Cursor Color", labelX, fy);
    drawColorSwatch(g, config_.cursor_color, fieldX, fy);
}

// ---------------------------------------------------------------------------
// Tab content: Font
// ---------------------------------------------------------------------------

void SettingsWindow::paintFontTab(Gdiplus::Graphics& g,
                                  int x, int y, int w, int h) {
    float fx = (float)x;
    float fy = (float)y;
    float labelX = fx;
    float fieldX = fx + kSetLabelW;
    float fieldW = (float)kSetFieldW;
    float row = (float)kSetRowHeight;

    // Font Family
    drawLabel(g, L"Font Family", labelX, fy);
    std::wstring fontVal = toWide(config_.font_family);
    drawTextField(g, fontVal.c_str(), fieldX, fy, fieldW,
                  editFieldId_ == 10);
    fy += row;

    // Font Size with +/- buttons
    drawLabel(g, L"Font Size", labelX, fy);
    wchar_t sizeBuf[16];
    swprintf(sizeBuf, 16, L"%.1f", config_.font_size);
    drawTextField(g, sizeBuf, fieldX, fy, 80.f, editFieldId_ == 11);

    // Minus button
    {
        float bx = fieldX + 88.f;
        float by = fy;
        Gdiplus::SolidBrush btnBr(toGdipColorCR(chrome_.btnInactive));
        drawRoundedRect(g, &btnBr, bx, by, 28.f, (float)kSetFieldH, 6.f);
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font font(&ff, 12.f, Gdiplus::FontStyleBold);
        Gdiplus::SolidBrush txtBr(toGdipColorCR(chrome_.textColor));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF lr(bx, by, 28.f, (float)kSetFieldH);
        g.DrawString(L"\x2212", -1, &font, lr, &sf, &txtBr);
    }
    // Plus button
    {
        float bx = fieldX + 120.f;
        float by = fy;
        Gdiplus::SolidBrush btnBr(toGdipColorCR(chrome_.btnInactive));
        drawRoundedRect(g, &btnBr, bx, by, 28.f, (float)kSetFieldH, 6.f);
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font font(&ff, 12.f, Gdiplus::FontStyleBold);
        Gdiplus::SolidBrush txtBr(toGdipColorCR(chrome_.textColor));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF lr(bx, by, 28.f, (float)kSetFieldH);
        g.DrawString(L"+", -1, &font, lr, &sf, &txtBr);
    }
    fy += row;

    // Font Features
    drawLabel(g, L"Font Features", labelX, fy);
    std::wstring featStr;
    for (size_t i = 0; i < config_.font_features.size(); ++i) {
        if (i > 0) featStr += L", ";
        featStr += toWide(config_.font_features[i]);
    }
    if (featStr.empty()) featStr = L"none";
    drawTextField(g, featStr.c_str(), fieldX, fy, fieldW,
                  editFieldId_ == 12);
    fy += row + 10.f;

    // Preview area
    drawLabel(g, L"Preview", labelX, fy);
    fy += 22.f;
    float previewH = 80.f;
    Gdiplus::SolidBrush previewBg(toGdipColorCR(chrome_.cardBg));
    drawRoundedRect(g, &previewBg, fx, fy, (float)w, previewH, 8.f);

    // Sample text in current font
    std::wstring fontNameW = toWide(config_.font_family);
    Gdiplus::FontFamily previewFF(fontNameW.c_str());
    // Fallback to Consolas if font family not found
    Gdiplus::FontFamily* pFF = &previewFF;
    Gdiplus::FontFamily fallbackFF(L"Consolas");
    if (!previewFF.IsAvailable()) pFF = &fallbackFF;

    Gdiplus::Font previewFont(pFF, config_.font_size, Gdiplus::FontStyleRegular);
    Gdiplus::SolidBrush previewTxt(toGdipColorCR(chrome_.textColor));
    Gdiplus::StringFormat sf;
    sf.SetAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::RectF previewRect(fx, fy, (float)w, previewH);
    g.DrawString(L"AaBb 0123 != =>", -1, &previewFont, previewRect,
                 &sf, &previewTxt);
}

} // namespace termcore

#endif
