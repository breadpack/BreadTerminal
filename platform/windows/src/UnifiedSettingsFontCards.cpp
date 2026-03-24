#if defined(_WIN32)

#include "UnifiedSettingsWindow.h"
#include "FontInstaller.h"

#include <algorithm>
#include <cmath>
#include <thread>
#include <commctrl.h>

namespace termcore {

// ---------------------------------------------------------------------------
// Layout constants for font cards within unified settings
// ---------------------------------------------------------------------------

static constexpr int kFcCardW       = 220;
static constexpr int kFcCardH       = 150;
static constexpr int kFcCardGap     = 12;
static constexpr int kFcFilterBtnW  = 80;
static constexpr int kFcFilterBtnH  = 26;
static constexpr int kFcFilterGap   = 6;
static constexpr int kFcFilterBarH  = 36;
static constexpr int kFcPreviewH    = 80;
static constexpr int kFcBadgeStripH = 22;

static const wchar_t* kFontFilterLabels[] = { L"All", L"Installed", L"NerdFonts", L"Ligatures" };
static constexpr int kFcSearchW     = 180;
static constexpr int kFcSearchH     = 26;
static constexpr int kFcFallbackBtnW = 18;
static constexpr int kFcFallbackBtnH = 18;

// ---------------------------------------------------------------------------
// rebuildFontFilteredList
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::rebuildFontFilteredList() {
    filteredFonts_.clear();

    bool installed = (activeFontFilter_ == FontFilter::Installed);
    bool nerd      = (activeFontFilter_ == FontFilter::NerdFonts);
    bool liga      = (activeFontFilter_ == FontFilter::Ligatures);

    if (!fontSearchText_.empty()) {
        // Convert search text to narrow for FontIndex::search
        int len = WideCharToMultiByte(CP_UTF8, 0, fontSearchText_.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string query(len - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, fontSearchText_.c_str(), -1, query.data(), len, nullptr, nullptr);
        filteredFonts_ = fontIndex_.search(query);
    } else if (installed || nerd || liga) {
        filteredFonts_ = fontIndex_.filter(installed, nerd, liga);
    } else {
        for (auto& f : fontIndex_.all())
            filteredFonts_.push_back(&f);
    }
}

// ---------------------------------------------------------------------------
// paintFontCards
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::paintFontCards(Gdiplus::Graphics& g,
                                            int x, int y, int w, int h) {
    if (filteredFonts_.empty() && fontIndex_.count() > 0) {
        rebuildFontFilteredList();
    }

    float scrollOff = scrollY_;

    // --- Filter bar ---
    {
        float fx = (float)x;
        float fy = (float)y - scrollOff;

        Gdiplus::Font filterFont(L"Segoe UI", 9.f);

        for (int i = 0; i < 4; ++i) {
            bool active = (i == static_cast<int>(activeFontFilter_));
            float bx = fx + i * (kFcFilterBtnW + kFcFilterGap);
            float by = fy;

            if (active) {
                Gdiplus::SolidBrush acBr(toGdipColorCR(chrome_.accent));
                drawRoundedRect(g, &acBr, bx, by,
                                (float)kFcFilterBtnW, (float)kFcFilterBtnH,
                                (float)kFcFilterBtnH / 2.f);
            } else {
                Gdiplus::SolidBrush btnBr(toGdipColorCR(chrome_.btnInactive));
                drawRoundedRect(g, &btnBr, bx, by,
                                (float)kFcFilterBtnW, (float)kFcFilterBtnH,
                                (float)kFcFilterBtnH / 2.f);
            }

            Gdiplus::RectF pillRect(bx, by, (float)kFcFilterBtnW, (float)kFcFilterBtnH);
            Gdiplus::StringFormat fmt;
            fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);

            Gdiplus::SolidBrush txtBr(
                active ? Gdiplus::Color(255, 255, 255, 255)
                       : toGdipColorCR(chrome_.textColor));
            g.DrawString(kFontFilterLabels[i], -1, &filterFont, pillRect, &fmt, &txtBr);
        }
    }

    // --- Card grid ---
    int gridTop = y + kFcFilterBarH;
    int usable = w + kFcCardGap;
    int columns = (std::max)(1, usable / (kFcCardW + kFcCardGap));

    fontCardRects_.clear();

    auto iequal = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t j = 0; j < a.size(); ++j)
            if (tolower((unsigned char)a[j]) != tolower((unsigned char)b[j]))
                return false;
        return true;
    };

    for (size_t i = 0; i < filteredFonts_.size(); ++i) {
        int col = (int)(i % columns);
        int row = (int)(i / columns);
        int cx = x + col * (kFcCardW + kFcCardGap);
        int cy = gridTop + row * (kFcCardH + kFcCardGap) - (int)scrollOff;

        UsFontCardInfo card;
        card.meta = filteredFonts_[i];
        card.cardRect = { cx, cy, cx + kFcCardW, cy + kFcCardH };
        card.buttonRect = { cx + kFcCardW - 72, cy + kFcCardH - 28,
                            cx + kFcCardW - 4,  cy + kFcCardH - 6 };
        // Fallback star button: left of the action button
        card.fallbackRect = { cx + kFcCardW - 94, cy + kFcCardH - 25,
                              cx + kFcCardW - 76, cy + kFcCardH - 7 };
        card.uninstallRect = { cx + kFcCardW - 22, cy + 4, cx + kFcCardW - 4, cy + 22 };
        card.isActive = false;
        card.isFallback = (card.meta && card.meta->installed && isFontFallback(card.meta->name));
        card.isInstalling = ((int)i == fontInstallingCard_);
        card.isFailed = ((int)i == fontFailedCard_);

        if (card.meta && !config_.font_family.empty()) {
            card.isActive = iequal(card.meta->name, config_.font_family)
                         || iequal(card.meta->postscript_name, config_.font_family);
        }

        fontCardRects_.push_back(card);

        if (card.cardRect.bottom < 0 || card.cardRect.top > y + h)
            continue;

        paintFontSingleCard(g, card);
    }

    if (filteredFonts_.empty()) {
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font font(&ff, 14.f, Gdiplus::FontStyleRegular);
        Gdiplus::SolidBrush br(toGdipColorCR(chrome_.dimText));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF area((float)x, (float)gridTop - scrollOff,
                            (float)w, (float)h);
        g.DrawString(L"No fonts found", -1, &font, area, &sf, &br);
    }
}

// ---------------------------------------------------------------------------
// paintFontSingleCard
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::paintFontSingleCard(Gdiplus::Graphics& g,
                                                 const UsFontCardInfo& card) {
    if (!card.meta) return;

    float cx = (float)card.cardRect.left;
    float cy = (float)card.cardRect.top;
    float cw = (float)(card.cardRect.right - card.cardRect.left);
    float ch = (float)(card.cardRect.bottom - card.cardRect.top);
    float r = 8.f;

    Gdiplus::GraphicsPath cardPath;
    drawRoundedRectPath(cardPath, cx, cy, cw, ch, r);

    COLORREF cardColor = card.meta->installed
        ? chrome_.cardBg
        : darkenCR(chrome_.cardBg, 0.82f);
    Gdiplus::SolidBrush cardBr(toGdipColorCR(cardColor));
    g.FillPath(&cardBr, &cardPath);

    if (card.meta->installed) {
        Gdiplus::Pen greenPen(toGdipColorCR(chrome_.activeGreen), 3.f);
        g.DrawLine(&greenPen, cx + 1.5f, cy + r, cx + 1.5f, cy + ch - r);
    }

    paintFontPreview(g, card, cx, cy, cw);

    float badgeY = cy + (float)kFcPreviewH;
    Gdiplus::SolidBrush stripBr(Gdiplus::Color(255, 20, 20, 34));
    g.FillRectangle(&stripBr, cx, badgeY, cw, (float)kFcBadgeStripH);
    paintFontBadges(g, *card.meta, cx + 8.f, badgeY + 3.f, cw - 16.f);

    {
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font nameFont(&ff, 9.5f, Gdiplus::FontStyleRegular);
        Gdiplus::SolidBrush nameBr(toGdipColorCR(chrome_.textColor));

        std::wstring nameW = toWide(card.meta->name);
        float nameY = cy + (float)kFcPreviewH + (float)kFcBadgeStripH;
        Gdiplus::RectF nameRect(cx + 8.f, nameY + 4.f, cw - 80.f, 20.f);
        Gdiplus::StringFormat nsf;
        nsf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        nsf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
        nsf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(nameW.c_str(), -1, &nameFont, nameRect, &nsf, &nameBr);
    }

    paintFontCardButton(g, card);

    if (card.meta->installed && !card.isActive && !card.isInstalling) {
        float ux = (float)card.uninstallRect.left;
        float uy = (float)card.uninstallRect.top;
        float uw = (float)(card.uninstallRect.right - card.uninstallRect.left);
        float uh = (float)(card.uninstallRect.bottom - card.uninstallRect.top);

        Gdiplus::SolidBrush circleBr(Gdiplus::Color(160, 60, 60, 60));
        g.FillEllipse(&circleBr, ux, uy, uw, uh);

        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font xFont(&ff, 9.f, Gdiplus::FontStyleBold);
        Gdiplus::SolidBrush xBr(Gdiplus::Color(200, 220, 220, 220));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF xRect(ux, uy, uw, uh);
        g.DrawString(L"\x00D7", -1, &xFont, xRect, &sf, &xBr);
    }

    // Fallback star button (installed fonts only, not the active primary font)
    if (card.meta->installed && !card.isActive) {
        float fx = (float)card.fallbackRect.left;
        float fy = (float)card.fallbackRect.top;
        float fw = (float)(card.fallbackRect.right - card.fallbackRect.left);
        float fh = (float)(card.fallbackRect.bottom - card.fallbackRect.top);

        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font starFont(&ff, 10.f, Gdiplus::FontStyleRegular);
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF starRect(fx, fy, fw, fh);

        if (card.isFallback) {
            // Filled star — accent color
            Gdiplus::SolidBrush starBr(toGdipColorCR(chrome_.accent));
            g.DrawString(L"\x2605", -1, &starFont, starRect, &sf, &starBr);
        } else {
            // Outline star — dim
            Gdiplus::SolidBrush starBr(toGdipColorCR(chrome_.dimText));
            g.DrawString(L"\x2606", -1, &starFont, starRect, &sf, &starBr);
        }
    }
}

// ---------------------------------------------------------------------------
// paintFontPreview
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::paintFontPreview(Gdiplus::Graphics& g,
                                              const UsFontCardInfo& card,
                                              float cx, float cy, float cw) {
    float r = 8.f;
    float ph = (float)kFcPreviewH;

    Gdiplus::GraphicsPath previewClip;
    previewClip.AddArc(cx, cy, r * 2, r * 2, 180.f, 90.f);
    previewClip.AddArc(cx + cw - r * 2, cy, r * 2, r * 2, 270.f, 90.f);
    previewClip.AddLine(cx + cw, cy + ph, cx, cy + ph);
    previewClip.CloseFigure();

    Gdiplus::SolidBrush previewBg(toGdipColorCR(chrome_.previewBg));
    g.FillPath(&previewBg, &previewClip);

    if (card.meta->installed) {
        std::wstring fontNameW = toWide(card.meta->name);
        Gdiplus::FontFamily family(fontNameW.c_str());

        bool useFallback = !family.IsAvailable();
        Gdiplus::FontFamily fallback(L"Consolas");
        Gdiplus::FontFamily& usedFamily = useFallback ? fallback : family;

        Gdiplus::Font previewFont(&usedFamily, 14.f, Gdiplus::FontStyleRegular);
        Gdiplus::SolidBrush fgBr(Gdiplus::Color(255, 220, 220, 240));

        Gdiplus::PointF pt1(cx + 12.f, cy + 12.f);
        g.DrawString(L"AaBb 0123", -1, &previewFont, pt1, &fgBr);

        Gdiplus::PointF pt2(cx + 12.f, cy + 36.f);
        g.DrawString(L"!= => ->", -1, &previewFont, pt2, &fgBr);

        if (useFallback) {
            Gdiplus::SolidBrush dimBr(toGdipColorCR(chrome_.dimText, 160));
            Gdiplus::FontFamily uiFF(L"Segoe UI");
            Gdiplus::Font dimFont(&uiFF, 8.f, Gdiplus::FontStyleItalic);
            Gdiplus::PointF ptDim(cx + cw - 80.f, cy + ph - 16.f);
            g.DrawString(L"(fallback)", -1, &dimFont, ptDim, &dimBr);
        }
    } else {
        Gdiplus::SolidBrush grad1(Gdiplus::Color(255, 38, 38, 58));
        Gdiplus::SolidBrush grad2(Gdiplus::Color(255, 28, 28, 48));
        g.FillRectangle(&grad1, cx, cy, cw, ph / 2.f);
        g.FillRectangle(&grad2, cx, cy + ph / 2.f, cw, ph / 2.f);

        std::wstring nameW = toWide(card.meta->name);
        Gdiplus::FontFamily uiFF(L"Segoe UI");
        Gdiplus::Font nameFont(&uiFF, 13.f, Gdiplus::FontStyleRegular);
        Gdiplus::SolidBrush nameBr(Gdiplus::Color(255, 160, 160, 180));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF previewArea(cx, cy, cw, ph);
        g.DrawString(nameW.c_str(), -1, &nameFont, previewArea, &sf, &nameBr);

        Gdiplus::Font smallFont(&uiFF, 8.f, Gdiplus::FontStyleItalic);
        Gdiplus::SolidBrush dimBr(toGdipColorCR(chrome_.dimText, 140));
        Gdiplus::RectF subArea(cx, cy + ph * 0.55f, cw, ph * 0.4f);
        g.DrawString(L"Not Installed", -1, &smallFont, subArea, &sf, &dimBr);
    }
}

// ---------------------------------------------------------------------------
// paintFontBadges
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::paintFontBadges(Gdiplus::Graphics& g,
                                             const FontMetadata& meta,
                                             float x, float y, float maxW) {
    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font badgeFont(&ff, 8.f, Gdiplus::FontStyleBold);

    float curX = x;
    float badgePadH = 8.f;
    float badgeH = 16.f;
    float badgeGap = 4.f;

    auto drawBadge = [&](const wchar_t* text, COLORREF color) {
        Gdiplus::RectF measureRect;
        g.MeasureString(text, -1, &badgeFont, Gdiplus::PointF(0, 0), &measureRect);
        float badgeW = measureRect.Width + badgePadH * 2;

        if (curX + badgeW > x + maxW) return;

        Gdiplus::GraphicsPath pill;
        drawRoundedRectPath(pill, curX, y, badgeW, badgeH, badgeH / 2.f);
        Gdiplus::SolidBrush pillBr(toGdipColorCR(color, 200));
        g.FillPath(&pillBr, &pill);

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
    if (meta.category == "system") {
        drawBadge(L"System", chrome_.dimText);
    }
    if (meta.has_ligatures) {
        drawBadge(L"Ligatures", chrome_.accent);
    }
    if (meta.has_nerd_font_variant) {
        drawBadge(L"Nerd Font", chrome_.badgeSecondary);
    }
}

// ---------------------------------------------------------------------------
// paintFontCardButton
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::paintFontCardButton(Gdiplus::Graphics& g,
                                                 const UsFontCardInfo& card) {
    float bx = (float)card.buttonRect.left;
    float by = (float)card.buttonRect.top;
    float bw = (float)(card.buttonRect.right - card.buttonRect.left);
    float bh = (float)(card.buttonRect.bottom - card.buttonRect.top);

    Gdiplus::GraphicsPath pill;
    drawRoundedRectPath(pill, bx, by, bw, bh, bh / 2.f);

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
    } else if (card.meta && card.meta->download_url.empty()) {
        // System-bundled font without download URL — no install possible
        fillColor = toGdipColorCR(darkenCR(chrome_.btnInactive, 0.6f));
        textColor = toGdipColorCR(chrome_.dimText);
        label = L"System";
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

int UnifiedSettingsWindow::hitTestFontCard(int mx, int my) const {
    for (int i = 0; i < (int)fontCardRects_.size(); ++i) {
        const RECT& r = fontCardRects_[i].cardRect;
        if (mx >= r.left && mx < r.right && my >= r.top && my < r.bottom)
            return i;
    }
    return -1;
}

bool UnifiedSettingsWindow::hitTestFontCardButton(int idx, int mx, int my) const {
    if (idx < 0 || idx >= (int)fontCardRects_.size()) return false;
    const RECT& r = fontCardRects_[idx].buttonRect;
    return mx >= r.left && mx < r.right && my >= r.top && my < r.bottom;
}

bool UnifiedSettingsWindow::hitTestFontUninstallButton(int idx, int mx, int my) const {
    if (idx < 0 || idx >= (int)fontCardRects_.size()) return false;
    const auto& card = fontCardRects_[idx];
    if (!card.meta || !card.meta->installed || card.isActive) return false;
    const RECT& r = card.uninstallRect;
    return mx >= r.left && mx < r.right && my >= r.top && my < r.bottom;
}

int UnifiedSettingsWindow::hitTestFontFilterButton(int mx, int my,
                                                    int contentX) const {
    // Filter buttons are below the section title (40px below content top)
    int by0 = kUsTopBarH + kUsContentPad + 40 - (int)scrollY_;
    int byEnd = by0 + kFcFilterBtnH;

    if (my < by0 || my > byEnd) return -1;

    for (int i = 0; i < 4; ++i) {
        int fx = contentX + i * (kFcFilterBtnW + kFcFilterGap);
        if (mx >= fx && mx < fx + kFcFilterBtnW)
            return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// onFontCardClick
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::onFontCardClick(int idx) {
    if (idx < 0 || idx >= (int)fontCardRects_.size()) return;
    const FontMetadata* meta = fontCardRects_[idx].meta;
    if (!meta) return;

    if (meta->installed) {
        // Debounce: skip if already applying this font
        if (meta->name == config_.font_family) return;
        config_.font_family = meta->name;
        notifySave();
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    } else {
        onFontCardInstall(idx);
    }
}

// ---------------------------------------------------------------------------
// onFontCardInstall
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::onFontCardInstall(int idx) {
    if (idx < 0 || idx >= (int)fontCardRects_.size()) return;
    const FontMetadata* meta = fontCardRects_[idx].meta;
    if (!meta || meta->download_url.empty()) return;
    if (fontInstallingCard_ >= 0) return; // already installing

    fontInstallingCard_ = idx;
    fontFailedCard_ = -1;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);

    std::string dlUrl = meta->download_url;
    std::string dlName = meta->name;
    HWND hWnd = hwnd_;

    std::thread([dlUrl, dlName, hWnd]() {
        bool ok = installFontFromUrl(dlUrl, dlName,
            [](const std::string&) { /* progress */ });
        PostMessageW(hWnd, WM_APP + 10, ok ? 1 : 0, 0);
    }).detach();
}

// ---------------------------------------------------------------------------
// onFontCardUninstall
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::onFontCardUninstall(int idx) {
    if (idx < 0 || idx >= (int)fontCardRects_.size()) return;
    const FontMetadata* meta = fontCardRects_[idx].meta;
    if (!meta || !meta->installed) return;

    std::string name = meta->name;
    std::thread([name]() {
        uninstallFont(name);
    }).detach();

    // Refresh after a short delay to let the uninstall thread finish
    if (hwnd_) SetTimer(hwnd_, 301, 500, nullptr);
}

// ---------------------------------------------------------------------------
// Font search edit
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::createFontSearchEdit() {
    if (fontSearchEdit_) return;
    fontSearchEdit_ = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        0, 0, kFcSearchW, kFcSearchH,
        hwnd_, (HMENU)1003, nullptr, nullptr);

    // Set font
    HFONT hFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    SendMessageW(fontSearchEdit_, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Placeholder via cue banner
    SendMessageW(fontSearchEdit_, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search fonts...");
}

void UnifiedSettingsWindow::repositionFontSearchEdit() {
    if (!fontSearchEdit_) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int contentX = sidebarWidth_ + kUsContentPad;
    int filterBarRight = contentX + 4 * (kFcFilterBtnW + kFcFilterGap);
    int searchX = filterBarRight + 12;
    int searchY = kUsTopBarH + kUsContentPad + 40; // same Y as filter bar
    SetWindowPos(fontSearchEdit_, nullptr, searchX, searchY,
                 kFcSearchW, kFcSearchH, SWP_NOZORDER);

    bool isFontPage = (selectedCategoryId_.find("font.family") != std::string::npos);
    ShowWindow(fontSearchEdit_, isFontPage ? SW_SHOW : SW_HIDE);
}

void UnifiedSettingsWindow::onFontSearchChanged() {
    if (!fontSearchEdit_) return;
    wchar_t buf[256] = {};
    GetWindowTextW(fontSearchEdit_, buf, 255);
    fontSearchText_ = buf;
    scrollY_ = 0.f;
    rebuildFontFilteredList();
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// Fallback font helpers
// ---------------------------------------------------------------------------

bool UnifiedSettingsWindow::isFontFallback(const std::string& name) const {
    for (const auto& fb : config_.font_fallback) {
        if (fb == name) return true;
    }
    return false;
}

void UnifiedSettingsWindow::toggleFontFallback(const std::string& name) {
    auto it = std::find(config_.font_fallback.begin(), config_.font_fallback.end(), name);
    if (it != config_.font_fallback.end()) {
        config_.font_fallback.erase(it);
    } else {
        config_.font_fallback.push_back(name);
    }
    notifySave();
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

} // namespace termcore

#endif
