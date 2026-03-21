#if defined(_WIN32)

#include "UnifiedSettingsWindow.h"

namespace termcore {

// ---------------------------------------------------------------------------
// Stub implementations for font card methods (to be fully implemented later)
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::paintFontCards(Gdiplus::Graphics& g,
                                            int x, int y, int w, int h) {
    Gdiplus::Font font(L"Segoe UI", 10.f);
    Gdiplus::SolidBrush dimBr(toGdipColorCR(chrome_.dimText));
    Gdiplus::PointF pt((float)x, (float)y - scrollY_);
    g.DrawString(L"[Font cards will appear here]", -1, &font, pt, &dimBr);
}

void UnifiedSettingsWindow::paintFontSingleCard(Gdiplus::Graphics& /*g*/,
                                                 const UsFontCardInfo& /*card*/) {}
void UnifiedSettingsWindow::paintFontPreview(Gdiplus::Graphics& /*g*/,
                                              const UsFontCardInfo& /*card*/,
                                              float, float, float) {}
void UnifiedSettingsWindow::paintFontBadges(Gdiplus::Graphics& /*g*/,
                                             const FontMetadata& /*meta*/,
                                             float, float, float) {}
void UnifiedSettingsWindow::paintFontCardButton(Gdiplus::Graphics& /*g*/,
                                                 const UsFontCardInfo& /*card*/) {}
void UnifiedSettingsWindow::rebuildFontFilteredList() {}

int UnifiedSettingsWindow::hitTestFontCard(int, int) const { return -1; }
bool UnifiedSettingsWindow::hitTestFontCardButton(int, int, int) const { return false; }
bool UnifiedSettingsWindow::hitTestFontUninstallButton(int, int, int) const { return false; }
int UnifiedSettingsWindow::hitTestFontFilterButton(int, int, int) const { return -1; }

void UnifiedSettingsWindow::onFontCardClick(int) {}
void UnifiedSettingsWindow::onFontCardInstall(int) {}
void UnifiedSettingsWindow::onFontCardUninstall(int) {}
bool UnifiedSettingsWindow::isFontInstalled(const std::wstring&) const { return false; }

} // namespace termcore

#endif
