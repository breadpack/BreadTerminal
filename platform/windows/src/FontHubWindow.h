#pragma once
#if defined(_WIN32)

#include "termcore/font_index.h"
#include "termcore/config.h"
#include "ChromeColors.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <functional>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

namespace termcore {

// Color constants removed — now derived from theme via ChromeColors.

// ---------- Layout constants ----------
constexpr int kFhWinWidth    = 800;
constexpr int kFhWinHeight   = 580;
constexpr int kFhTitleH      = 32;
constexpr int kFhToolbarH    = 44;
constexpr int kFhCardW       = 220;
constexpr int kFhCardH       = 150;
constexpr int kFhCardGap     = 12;
constexpr int kFhGridPad     = 16;
constexpr int kFhSearchW     = 240;
constexpr int kFhSearchH     = 28;
constexpr int kFhFilterBtnW  = 72;
constexpr int kFhFilterBtnH  = 26;
constexpr int kFhFilterGap   = 6;
constexpr int kFhPreviewH    = 80;
constexpr int kFhBadgeStripH = 22;
constexpr int kFhBottomBarH  = 28;

constexpr UINT_PTR kFhSearchTimerId = 200;
constexpr UINT kFhSearchDelay       = 200; // ms

// ---------- Filter enum ----------
enum class FontFilter { All, Installed, NerdFonts, Ligatures };

// ---------- Visible card info ----------
struct FontCardInfo {
    const FontMetadata* meta = nullptr;
    RECT cardRect    = {};
    RECT buttonRect  = {};
    RECT uninstallRect = {}; // small "×" button for installed fonts
    bool isActive    = false; // currently applied font
    bool isInstalling = false; // download in progress
    bool isFailed    = false; // install failed
};

// ---------- FontHubWindow ----------
class FontHubWindow {
public:
    using ApplyCallback = std::function<void(const std::string& fontName)>;

    FontHubWindow();
    ~FontHubWindow();

    void setConfig(const Config& config);
    void setApplyCallback(ApplyCallback cb);
    void show(HWND parent);
    void close();

    static void registerWindowClass(HINSTANCE hInstance);

private:
    // Window procedure
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage(HWND, UINT, WPARAM, LPARAM);

    // Painting (FontHubPaint.cpp)
    void paintWindow(HWND hwnd);
    void paintTitleBar(Gdiplus::Graphics& g, int w);
    void paintToolbar(Gdiplus::Graphics& g, int w);

    // Card drawing (FontHubCards.cpp)
    void paintCards(Gdiplus::Graphics& g, int w, int h);
    void paintSingleCard(Gdiplus::Graphics& g, const FontCardInfo& card);
    void paintFontPreview(Gdiplus::Graphics& g, const FontCardInfo& card,
                          float cx, float cy, float cw);
    void paintBadges(Gdiplus::Graphics& g, const FontMetadata& meta,
                     float x, float y, float maxW);
    void paintCardButton(Gdiplus::Graphics& g, const FontCardInfo& card);

    // Search / filter
    void rebuildFilteredList();
    void onSearchChanged();

    // Layout
    void recalcCardLayout(int clientW);
    int  totalContentHeight() const;
    void clampScroll(int clientH);

    // Hit testing (FontHubCards.cpp)
    int  hitTestCard(int mx, int my) const;
    bool hitTestCardButton(int idx, int mx, int my) const;
    bool hitTestUninstallButton(int idx, int mx, int my) const;
    bool hitTestCloseButton(int mx, int my) const;
    int  hitTestFilterButton(int mx, int my) const;

    // Helpers
    std::string findFontIndexPath() const;
    bool isFontInstalled(const std::wstring& fontName) const;
    Gdiplus::Color toGdipColor(uint32_t rgb, BYTE a = 255) const;
    Gdiplus::Color toGdipColorCR(COLORREF cr, BYTE a = 255) const;
    void drawRoundedRect(Gdiplus::GraphicsPath& path,
                         float x, float y, float w, float h, float r) const;
    std::wstring toWide(const std::string& utf8) const;

    // State
    HWND hwnd_       = nullptr;
    HWND parentHwnd_ = nullptr;
    ApplyCallback applyCallback_;
    Config config_;
    ChromeColors chrome_{};
    std::string activeFontName_;

    // GDI+
    ULONG_PTR gdiplusToken_ = 0;
    bool gdiplusOwned_      = false;

    // Font data
    FontIndex fontIndex_;
    bool indexLoaded_ = false;
    std::vector<const FontMetadata*> filteredFonts_;
    std::vector<FontCardInfo> visibleCards_;

    // Interaction state
    FontFilter activeFilter_ = FontFilter::All;
    std::wstring searchText_;
    int hoveredCard_  = -1;
    float scrollY_    = 0.f;
    int gridColumns_  = 1;

    // Install state
    int installingCard_ = -1;  // index of card currently installing (-1 = none)
    int failedCard_     = -1;  // index of card that failed install (-1 = none)

    // Search edit
    HWND searchEdit_  = nullptr;

    static const wchar_t* kClassName;
    static bool sClassRegistered;
};

} // namespace termcore

#endif
