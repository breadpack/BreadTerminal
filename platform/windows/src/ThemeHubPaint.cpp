#if defined(_WIN32)

#include "ThemeHubWindow.h"

namespace termcore {

// ---------------------------------------------------------------------------
// paintWindow - double-buffered main paint
// ---------------------------------------------------------------------------

void ThemeHubWindow::paintWindow(HWND hwnd) {
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

        paintTitleBar(g, w);
        paintToolbar(g, w);

        // Clip card region
        Gdiplus::Rect clipRect(0, kThTitleH + kThToolbarH,
                               w, h - kThTitleH - kThToolbarH);
        g.SetClip(clipRect);
        paintCards(g, w, h);
        g.ResetClip();
    }

    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    EndPaint(hwnd, &ps);
}

// ---------------------------------------------------------------------------
// paintTitleBar - custom dark title bar with close button
// ---------------------------------------------------------------------------

void ThemeHubWindow::paintTitleBar(Gdiplus::Graphics& g, int w) {
    Gdiplus::SolidBrush br(toGdipColorCR(chrome_.titleBar));
    g.FillRectangle(&br, 0, 0, w, kThTitleH);

    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font font(&ff, 12.f, Gdiplus::FontStyleBold);
    Gdiplus::SolidBrush txtBr(toGdipColorCR(chrome_.textColor));
    Gdiplus::PointF pt(12.f, 7.f);
    g.DrawString(L"Theme Hub", -1, &font, pt, &txtBr);

    // Close button (X)
    Gdiplus::Pen pen(toGdipColorCR(chrome_.dimText), 1.5f);
    float cx = (float)(w - 20), cy = 10.f;
    g.DrawLine(&pen, cx, cy, cx + 12.f, cy + 12.f);
    g.DrawLine(&pen, cx + 12.f, cy, cx, cy + 12.f);
}

// ---------------------------------------------------------------------------
// paintToolbar - search background + filter pill buttons
// ---------------------------------------------------------------------------

void ThemeHubWindow::paintToolbar(Gdiplus::Graphics& g, int w) {
    int y0 = kThTitleH;

    // Search field background rounded rect
    Gdiplus::GraphicsPath searchPath;
    float sx = (float)kThGridPad, sy = (float)(y0 + 8);
    float sw = (float)kThSearchW, sh = (float)kThSearchH;
    float r = 6.f;
    searchPath.AddArc(sx, sy, r * 2, r * 2, 180.f, 90.f);
    searchPath.AddArc(sx + sw - r * 2, sy, r * 2, r * 2, 270.f, 90.f);
    searchPath.AddArc(sx + sw - r * 2, sy + sh - r * 2, r * 2, r * 2, 0.f, 90.f);
    searchPath.AddArc(sx, sy + sh - r * 2, r * 2, r * 2, 90.f, 90.f);
    searchPath.CloseFigure();

    Gdiplus::SolidBrush searchBr(toGdipColorCR(chrome_.fieldBg));
    g.FillPath(&searchBr, &searchPath);

    // Magnifying glass icon (simple circle + line)
    float ix = sx + 6.f, iy = sy + 6.f;
    Gdiplus::Pen iconPen(toGdipColorCR(chrome_.dimText), 1.5f);
    g.DrawEllipse(&iconPen, ix, iy, 10.f, 10.f);
    g.DrawLine(&iconPen, ix + 9.f, iy + 9.f, ix + 13.f, iy + 13.f);

    // Filter buttons
    const wchar_t* labels[] = { L"All", L"Dark", L"Light", L"Installed" };
    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font btnFont(&ff, 10.f, Gdiplus::FontStyleRegular);

    int bx = w - kThGridPad - 4 * (kThFilterBtnW + kThFilterGap) + kThFilterGap;
    int by = y0 + 9;

    for (int i = 0; i < 4; ++i) {
        bool active = ((int)activeFilter_ == i);
        Gdiplus::Color fill = active ? toGdipColorCR(chrome_.accent)
                                     : toGdipColorCR(chrome_.btnInactive);
        float fx = (float)(bx + i * (kThFilterBtnW + kThFilterGap));
        float fy = (float)by;
        float fw = (float)kThFilterBtnW;
        float fh = (float)kThFilterBtnH;
        float br2 = fh / 2.f;

        Gdiplus::GraphicsPath pill;
        pill.AddArc(fx, fy, br2 * 2, br2 * 2, 180.f, 90.f);
        pill.AddArc(fx + fw - br2 * 2, fy, br2 * 2, br2 * 2, 270.f, 90.f);
        pill.AddArc(fx + fw - br2 * 2, fy + fh - br2 * 2, br2 * 2, br2 * 2, 0.f, 90.f);
        pill.AddArc(fx, fy + fh - br2 * 2, br2 * 2, br2 * 2, 90.f, 90.f);
        pill.CloseFigure();

        Gdiplus::SolidBrush pillBr(fill);
        g.FillPath(&pillBr, &pill);

        // Label
        Gdiplus::SolidBrush txtBr(active
            ? toGdipColorCR(chrome_.titleBar)
            : toGdipColorCR(chrome_.textColor));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF lr(fx, fy, fw, fh);
        g.DrawString(labels[i], -1, &btnFont, lr, &sf, &txtBr);
    }
}

// ---------------------------------------------------------------------------
// Helper color conversions
// ---------------------------------------------------------------------------

Gdiplus::Color ThemeHubWindow::toGdipColor(uint32_t rgb, BYTE a) const {
    return Gdiplus::Color(a,
        (BYTE)((rgb >> 16) & 0xFF),
        (BYTE)((rgb >> 8) & 0xFF),
        (BYTE)(rgb & 0xFF));
}

Gdiplus::Color ThemeHubWindow::toGdipColorCR(COLORREF cr, BYTE a) const {
    return Gdiplus::Color(a, GetRValue(cr), GetGValue(cr), GetBValue(cr));
}

} // namespace termcore

#endif
