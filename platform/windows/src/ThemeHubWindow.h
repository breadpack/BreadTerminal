#pragma once
#if defined(_WIN32)

#include "termcore/theme_index.h"
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
// Access via chrome_ member in ThemeHubWindow.

// ---------- Layout constants ----------
constexpr int kThWinWidth    = 780;
constexpr int kThWinHeight   = 560;
constexpr int kThTitleH      = 32;
constexpr int kThToolbarH    = 44;
constexpr int kThCardW       = 190;
constexpr int kThCardH       = 134;
constexpr int kThCardGap     = 12;
constexpr int kThGridPad     = 16;
constexpr int kThSearchW     = 240;
constexpr int kThSearchH     = 28;
constexpr int kThFilterBtnW  = 62;
constexpr int kThFilterBtnH  = 26;
constexpr int kThFilterGap   = 6;
constexpr int kThSwatchSize  = 14;
constexpr int kThSwatchGap   = 3;
constexpr int kThSwatchRound = 2;

constexpr UINT_PTR kThSearchTimerId = 100;
constexpr UINT kThSearchDelay       = 200; // ms

// ---------- Filter enum ----------
enum class ThemeFilter { All, Dark, Light, Installed };

// ---------- Visible card info ----------
struct ThemeCardInfo {
    const ThemeMetadata* meta = nullptr;
    RECT cardRect    = {};
    RECT buttonRect  = {};
    bool isActive    = false; // current theme
};

// ---------- ThemeHubWindow ----------
class ThemeHubWindow {
public:
    using ApplyCallback = std::function<void(const std::string& themeName,
                                            const ThemeMetadata* meta)>;

    ThemeHubWindow();
    ~ThemeHubWindow();

    void setConfig(const Config& config);
    void setApplyCallback(ApplyCallback cb);
    void show(HWND parent);
    void close();

    static void registerWindowClass(HINSTANCE hInstance);

private:
    // Window procedure
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage(HWND, UINT, WPARAM, LPARAM);

    // Painting
    void paintWindow(HWND hwnd);
    void paintTitleBar(Gdiplus::Graphics& g, int w);
    void paintToolbar(Gdiplus::Graphics& g, int w);
    void paintCards(Gdiplus::Graphics& g, int w, int h);

    // Card drawing (ThemeHubCards.cpp)
    void paintSingleCard(Gdiplus::Graphics& g, const ThemeCardInfo& card);
    void paintPalette(Gdiplus::Graphics& g, const ThemeMetadata& meta,
                      int x, int y);
    void paintCardButton(Gdiplus::Graphics& g, const ThemeCardInfo& card);

    // Search / filter
    void rebuildFilteredList();
    void onSearchChanged();

    // Layout
    void recalcCardLayout(int clientW);
    int  totalContentHeight() const;
    void clampScroll(int clientH);

    // Hit testing (ThemeHubCards.cpp)
    int  hitTestCard(int mx, int my) const;
    bool hitTestCardButton(int idx, int mx, int my) const;
    bool hitTestCloseButton(int mx, int my) const;
    int  hitTestFilterButton(int mx, int my) const;

    // Helpers
    std::string findThemeIndexPath() const;
    Gdiplus::Color toGdipColor(uint32_t rgb, BYTE a = 255) const;
    Gdiplus::Color toGdipColorCR(COLORREF cr, BYTE a = 255) const;

    // State
    HWND hwnd_       = nullptr;
    HWND parentHwnd_ = nullptr;
    ApplyCallback applyCallback_;
    Config config_;
    ChromeColors chrome_{};  // theme-derived UI colors
    std::string activeThemeName_;

    // GDI+
    ULONG_PTR gdiplusToken_ = 0;
    bool gdiplusOwned_      = false;

    // Theme data
    ThemeIndex themeIndex_;
    bool indexLoaded_ = false;
    std::vector<const ThemeMetadata*> filteredThemes_;
    std::vector<ThemeCardInfo> visibleCards_;

    // Interaction state
    ThemeFilter activeFilter_ = ThemeFilter::All;
    std::wstring searchText_;
    int hoveredCard_  = -1;
    float scrollY_    = 0.f;
    int gridColumns_  = 1;

    // Search edit
    HWND searchEdit_  = nullptr;

    static const wchar_t* kClassName;
    static bool sClassRegistered;
};

} // namespace termcore

#endif
