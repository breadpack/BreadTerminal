#if defined(_WIN32)

#include "SettingsWindow.h"

#include <algorithm>

namespace termcore {

// ---------------------------------------------------------------------------
// Tab content: Keys (Keybindings)
// ---------------------------------------------------------------------------

void SettingsWindow::paintKeysTab(Gdiplus::Graphics& g,
                                  int x, int y, int w, int h) {
    float fx = (float)x;
    float fy = (float)y;
    float row = (float)kSetRowHeight;

    // Header
    {
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font font(&ff, 9.f, Gdiplus::FontStyleBold);
        Gdiplus::SolidBrush br(toGdipColorCR(chrome_.dimText));
        Gdiplus::PointF pt1(fx, fy);
        g.DrawString(L"TRIGGER", -1, &font, pt1, &br);
        Gdiplus::PointF pt2(fx + w / 2.f, fy);
        g.DrawString(L"ACTION", -1, &font, pt2, &br);
    }
    fy += 24.f;

    // Separator line
    Gdiplus::Pen sepPen(toGdipColorCR(chrome_.btnInactive), 1.f);
    g.DrawLine(&sepPen, fx, fy, fx + w, fy);
    fy += 4.f;

    keyBindingRows_.clear();
    float listTop = fy;

    // Clip to available area for scrolling
    float listH = (float)h - (fy - y) - 40.f;
    Gdiplus::Rect listClip((int)fx, (int)listTop, w, (int)listH);
    g.SetClip(listClip, Gdiplus::CombineModeIntersect);

    float scrollOff = (float)keysScrollY_;

    for (size_t i = 0; i < config_.keybindings.size(); ++i) {
        float ry = listTop + (float)(i * kSetRowHeight) - scrollOff;
        if (ry + row < listTop || ry > listTop + listH) {
            keyBindingRows_.push_back({});
            continue;
        }

        const auto& kb = config_.keybindings[i];

        // Alternating row background
        if (i % 2 == 0) {
            Gdiplus::SolidBrush rowBg(toGdipColorCR(chrome_.cardBg, 128));
            drawRoundedRect(g, &rowBg, fx, ry, (float)w, row - 2.f, 4.f);
        }

        // Trigger
        std::wstring trigW = toWide(kb.trigger);
        drawTextField(g, trigW.c_str(), fx + 4.f, ry + 6.f,
                      (float)(w / 2 - 12), false);

        // Action
        std::wstring actW = toWide(kb.action);
        drawTextField(g, actW.c_str(), fx + w / 2.f + 4.f, ry + 6.f,
                      (float)(w / 2 - 12), false);

        KeyBindingRow kbr;
        kbr.triggerRect = { (int)(fx + 4), (int)(ry + 6),
                            (int)(fx + w / 2 - 8), (int)(ry + 6 + kSetFieldH) };
        kbr.actionRect  = { (int)(fx + w / 2 + 4), (int)(ry + 6),
                            (int)(fx + w - 8), (int)(ry + 6 + kSetFieldH) };
        keyBindingRows_.push_back(kbr);
    }

    g.ResetClip();

    // Add / Remove buttons at bottom
    float btnY = listTop + listH + 8.f;

    // Add button (+)
    {
        Gdiplus::SolidBrush addBr(toGdipColorCR(chrome_.accent));
        drawRoundedRect(g, &addBr, fx, btnY, 36.f, 28.f, 14.f);
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font font(&ff, 14.f, Gdiplus::FontStyleBold);
        Gdiplus::SolidBrush txtBr(toGdipColorCR(chrome_.titleBar));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF lr(fx, btnY, 36.f, 28.f);
        g.DrawString(L"+", -1, &font, lr, &sf, &txtBr);
    }

    // Remove button (-)
    {
        Gdiplus::SolidBrush remBr(toGdipColorCR(chrome_.btnInactive));
        drawRoundedRect(g, &remBr, fx + 44.f, btnY, 36.f, 28.f, 14.f);
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font font(&ff, 14.f, Gdiplus::FontStyleBold);
        Gdiplus::SolidBrush txtBr(toGdipColorCR(chrome_.textColor));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF lr(fx + 44.f, btnY, 36.f, 28.f);
        g.DrawString(L"\x2212", -1, &font, lr, &sf, &txtBr);
    }

    // Empty state message
    if (config_.keybindings.empty()) {
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font font(&ff, 11.f, Gdiplus::FontStyleRegular);
        Gdiplus::SolidBrush br(toGdipColorCR(chrome_.dimText));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF area(fx, listTop, (float)w, listH);
        g.DrawString(L"No keybindings configured.\nClick + to add one.",
                     -1, &font, area, &sf, &br);
    }
}

// ---------------------------------------------------------------------------
// Tab content: Clipboard
// ---------------------------------------------------------------------------

static void drawDescText(SettingsWindow* /*self*/, Gdiplus::Graphics& g,
                         const wchar_t* text, float x, float y, float w,
                         COLORREF dimColor) {
    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font font(&ff, 9.f, Gdiplus::FontStyleRegular);
    Gdiplus::SolidBrush br(Gdiplus::Color(255,
        GetRValue(dimColor), GetGValue(dimColor), GetBValue(dimColor)));
    Gdiplus::RectF descRect(x, y, w, 36.f);
    Gdiplus::StringFormat sf;
    sf.SetTrimming(Gdiplus::StringTrimmingWord);
    g.DrawString(text, -1, &font, descRect, &sf, &br);
}

void SettingsWindow::paintClipboardTab(Gdiplus::Graphics& g,
                                       int x, int y, int w, int h) {
    float fx = (float)x;
    float fy = (float)y;
    float labelX = fx;
    float fieldX = fx + kSetLabelW;
    float descW = (float)(w - kSetLabelW);
    float row = (float)kSetRowHeight;

    // Paste Protection
    drawLabel(g, L"Paste Protection", labelX, fy);
    const wchar_t* pasteLabels[] = { L"Never", L"Multiline", L"Always" };
    int pasteSel = 1;
    if (config_.clipboard_paste_protection == "never") pasteSel = 0;
    else if (config_.clipboard_paste_protection == "always") pasteSel = 2;
    drawPillButtons(g, pasteLabels, 3, pasteSel, fieldX, fy);
    fy += row;

    drawDescText(this, g,
        L"Controls when a confirmation dialog appears before pasting. "
        L"\"Multiline\" warns on multi-line pastes.",
        fieldX, fy, descW, chrome_.dimText);
    fy += 44.f;

    // Bracketed Paste Trust
    drawLabel(g, L"Bracketed Paste Trust", labelX, fy);
    drawToggle(g, config_.clipboard_paste_bracketed_safe, fieldX, fy + 2.f);
    fy += row;

    drawDescText(this, g,
        L"When enabled, bracketed paste mode from applications is "
        L"trusted and bypasses paste protection warnings.",
        fieldX, fy, descW, chrome_.dimText);
    fy += 44.f;

    // Allow Clipboard Write (OSC 52)
    drawLabel(g, L"Allow Clipboard Write", labelX, fy);
    drawToggle(g, config_.allow_clipboard_write, fieldX, fy + 2.f);
    fy += row;

    drawDescText(this, g,
        L"Allow terminal applications to write to the system clipboard "
        L"via OSC 52 escape sequences.",
        fieldX, fy, descW, chrome_.dimText);
}

} // namespace termcore

#endif
