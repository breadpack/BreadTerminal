#if defined(_WIN32)

#include "UnifiedSettingsWindow.h"

namespace termcore {

// ---------------------------------------------------------------------------
// Layout constants for keybinding list within unified settings
// ---------------------------------------------------------------------------

static constexpr int kKbRowH       = 36;   // height of each keybinding row
static constexpr int kKbRowGap     = 2;    // gap between rows
static constexpr int kKbPadX       = 12;   // horizontal padding inside row
static constexpr int kKbPadY       = 8;    // vertical padding inside row
static constexpr int kKbTriggerW   = 200;  // width reserved for trigger column
static constexpr int kKbHeaderH    = 32;   // column header row height
static constexpr float kKbRowRound = 6.f;  // row corner radius

// ---------------------------------------------------------------------------
// paintKeybindingList
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::paintKeybindingList(Gdiplus::Graphics& g,
                                                 int x, int y, int w, int h) {
    float scrollOff = scrollY_;

    // Fonts
    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font headerFont(&ff, 9.f, Gdiplus::FontStyleBold);
    Gdiplus::Font rowFont(&ff, 10.f, Gdiplus::FontStyleRegular);
    Gdiplus::Font monoFamily(L"Consolas", 10.f);

    // Brushes
    Gdiplus::SolidBrush textBr(toGdipColorCR(chrome_.textColor));
    Gdiplus::SolidBrush dimBr(toGdipColorCR(chrome_.dimText));
    Gdiplus::SolidBrush cardBr(toGdipColorCR(chrome_.cardBg));

    // --- "No keybindings" message if empty ---
    if (config_.keybindings.empty()) {
        Gdiplus::Font emptyFont(&ff, 14.f, Gdiplus::FontStyleRegular);
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF area((float)x, (float)y - scrollOff, (float)w, (float)h);
        g.DrawString(L"No keybindings configured", -1, &emptyFont, area, &sf, &dimBr);
        return;
    }

    // --- Column headers ---
    {
        float hx = (float)x;
        float hy = (float)y - scrollOff;

        Gdiplus::StringFormat leftFmt;
        leftFmt.SetAlignment(Gdiplus::StringAlignmentNear);
        leftFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        leftFmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

        Gdiplus::RectF triggerRect(hx + kKbPadX, hy, (float)kKbTriggerW, (float)kKbHeaderH);
        g.DrawString(L"Shortcut", -1, &headerFont, triggerRect, &leftFmt, &dimBr);

        Gdiplus::RectF actionRect(hx + kKbPadX + kKbTriggerW, hy,
                                  (float)(w - kKbTriggerW - kKbPadX * 2), (float)kKbHeaderH);
        g.DrawString(L"Action", -1, &headerFont, actionRect, &leftFmt, &dimBr);
    }

    // --- Separator line below header ---
    {
        float sy = (float)y + kKbHeaderH - scrollOff;
        Gdiplus::Pen sepPen(toGdipColorCR(chrome_.btnInactive), 1.f);
        g.DrawLine(&sepPen, (float)x, sy, (float)(x + w), sy);
    }

    // --- Keybinding rows ---
    int rowTop = y + kKbHeaderH + 4;

    Gdiplus::StringFormat leftFmt;
    leftFmt.SetAlignment(Gdiplus::StringAlignmentNear);
    leftFmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    leftFmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    leftFmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

    for (size_t i = 0; i < config_.keybindings.size(); ++i) {
        const auto& kb = config_.keybindings[i];

        float ry = (float)(rowTop + (int)i * (kKbRowH + kKbRowGap)) - scrollOff;

        // Skip rows outside visible area
        if (ry + kKbRowH < 0 || ry > (float)(y + h))
            continue;

        // Alternating row background for readability
        if (i % 2 == 0) {
            drawRoundedRect(g, &cardBr, (float)x, ry,
                            (float)w, (float)kKbRowH, kKbRowRound);
        }

        // Trigger (shortcut key combo) - rendered in monospace with a badge style
        {
            std::wstring triggerW = toWide(kb.trigger);

            // Draw a subtle rounded badge behind the trigger text
            Gdiplus::RectF measureRect;
            g.MeasureString(triggerW.c_str(), -1, &monoFamily,
                            Gdiplus::PointF(0, 0), &measureRect);

            float badgeW = measureRect.Width + 16.f;
            float badgeH = 22.f;
            float badgeX = (float)x + kKbPadX;
            float badgeY = ry + ((float)kKbRowH - badgeH) / 2.f;

            Gdiplus::SolidBrush badgeBr(toGdipColorCR(chrome_.fieldBg));
            drawRoundedRect(g, &badgeBr, badgeX, badgeY, badgeW, badgeH, 4.f);

            Gdiplus::RectF triggerTextRect(badgeX + 8.f, ry, badgeW - 16.f, (float)kKbRowH);
            g.DrawString(triggerW.c_str(), -1, &monoFamily,
                         triggerTextRect, &leftFmt, &textBr);
        }

        // Action name
        {
            std::wstring actionW = toWide(kb.action);
            float actionX = (float)x + kKbPadX + kKbTriggerW;
            float actionW2 = (float)(w - kKbTriggerW - kKbPadX * 2);
            Gdiplus::RectF actionRect(actionX, ry, actionW2, (float)kKbRowH);
            g.DrawString(actionW.c_str(), -1, &rowFont,
                         actionRect, &leftFmt, &textBr);
        }
    }
}

} // namespace termcore

#endif
