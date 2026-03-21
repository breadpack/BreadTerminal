#if defined(_WIN32)

#include "UnifiedSettingsWindow.h"

#include <algorithm>
#include <set>

namespace termcore {

// ---------------------------------------------------------------------------
// paintSidebar
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::paintSidebar(Gdiplus::Graphics& g, int w, int h) {
    int sbW = sidebarWidth_;
    int sbTop = kUsTopBarH;
    int sbBottom = h - kUsBottomBarH;
    int sbH = sbBottom - sbTop;

    // Background
    Gdiplus::SolidBrush bgBr(toGdipColorCR(chrome_.titleBar));
    g.FillRectangle(&bgBr, 0, sbTop, sbW, sbH);

    // Right border: 1px line in btnInactive
    Gdiplus::Pen borderPen(toGdipColorCR(chrome_.btnInactive), 1.f);
    g.DrawLine(&borderPen,
               (float)(sbW - 1), (float)sbTop,
               (float)(sbW - 1), (float)sbBottom);

    if (!model_) return;

    // Fonts
    Gdiplus::Font catFont(L"Segoe UI Semibold", 10.f);
    Gdiplus::Font subFont(L"Segoe UI", 10.f);
    Gdiplus::SolidBrush dimBr(toGdipColorCR(chrome_.dimText));
    Gdiplus::SolidBrush textBr(toGdipColorCR(chrome_.textColor));
    Gdiplus::SolidBrush accentBr(toGdipColorCR(chrome_.accent, 60));
    Gdiplus::SolidBrush accentBarBr(toGdipColorCR(chrome_.accent));

    // Clip sidebar region
    Gdiplus::Rect clipRect(0, sbTop, sbW, sbH);
    g.SetClip(clipRect);

    // Build set of visible IDs for fast lookup
    std::set<std::string> visibleSet(visibleCategoryIds_.begin(),
                                     visibleCategoryIds_.end());

    int y = sbTop + 8;

    auto topCats = model_->topLevelCategories();
    for (auto* top : topCats) {
        // Check if this top-level group has any visible subcategories
        auto subs = model_->subcategories(top->id);
        bool hasVisible = false;
        for (auto* sub : subs) {
            if (visibleSet.count(sub->id)) { hasVisible = true; break; }
        }
        if (!hasVisible) continue;

        // Draw category label (bold, dim)
        Gdiplus::PointF catPt(12.f, (float)y + 4.f);
        std::wstring catLabel = toWide(top->label);
        g.DrawString(catLabel.c_str(), -1, &catFont, catPt, &dimBr);
        y += kUsCatRowH;

        // Draw subcategories
        for (auto* sub : subs) {
            if (!visibleSet.count(sub->id)) continue;

            bool selected = (sub->id == selectedCategoryId_);

            if (selected) {
                // Accent-color translucent background
                g.FillRectangle(&accentBr, 0, y, sbW - 1, kUsSubCatRowH);

                // 3px left accent bar
                g.FillRectangle(&accentBarBr, 0.f, (float)y,
                                3.f, (float)kUsSubCatRowH);
            }

            // Subcategory label, indented 28px
            Gdiplus::PointF subPt(28.f, (float)y + 4.f);
            std::wstring subLabel = toWide(sub->label);
            Gdiplus::SolidBrush& br = selected ? textBr : dimBr;
            g.DrawString(subLabel.c_str(), -1, &subFont, subPt, &br);

            y += kUsSubCatRowH;
        }

        // 4px gap between top-level groups
        y += 4;
    }

    g.ResetClip();
}

} // namespace termcore

#endif
