#pragma once
#if defined(_WIN32)

#include "termcore/settings_model.h"
#include "termcore/config.h"
#include "termcore/font_index.h"
#include "termcore/theme_index.h"
#include "ChromeColors.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

namespace termcore {

// ---------- Layout constants ----------
constexpr int kUsWinWidth     = 800;
constexpr int kUsWinHeight    = 600;
constexpr int kUsMinWidth     = 640;
constexpr int kUsMinHeight    = 480;
constexpr int kUsTopBarH      = 40;
constexpr int kUsBottomBarH   = 24;
constexpr int kUsSidebarDef   = 200;
constexpr int kUsSidebarMin   = 140;
constexpr int kUsSidebarMax   = 320;
constexpr int kUsContentPad   = 24;
constexpr int kUsItemSpacing  = 16;
constexpr int kUsSearchW      = 280;
constexpr int kUsSearchH      = 28;
constexpr int kUsCatRowH      = 28;
constexpr int kUsSubCatRowH   = 26;

// ---------- UnifiedSettingsWindow ----------
class UnifiedSettingsWindow {
public:
    using SaveCallback = std::function<void(const Config& config)>;

    UnifiedSettingsWindow();
    ~UnifiedSettingsWindow();

    void setConfig(const Config& config);
    void setSaveCallback(SaveCallback cb);
    void show(HWND parent);
    void close();

    static void registerWindowClass(HINSTANCE hInstance);

private:
    // Window procedure
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage(HWND, UINT, WPARAM, LPARAM);

    // Painting
    void paintWindow(HWND hwnd);
    void paintTopBar(Gdiplus::Graphics& g, int w);
    void paintBottomBar(Gdiplus::Graphics& g, int w, int h);
    void paintSidebar(Gdiplus::Graphics& g, int w, int h);
    void paintContent(Gdiplus::Graphics& g, int w, int h);

    // Content section stubs (to be implemented in later tasks)
    void paintSettingsItems(Gdiplus::Graphics& g, int x, int y, int w, int h);
    void paintThemeCards(Gdiplus::Graphics& g, int x, int y, int w, int h);
    void paintFontCards(Gdiplus::Graphics& g, int x, int y, int w, int h);
    void paintKeybindingList(Gdiplus::Graphics& g, int x, int y, int w, int h);

    // Sidebar interaction
    int  hitTestSidebar(int mx, int my) const;
    void onSidebarClick(int mx, int my);

    // Sidebar resize
    void beginSidebarResize(int mx);
    void updateSidebarResize(int mx);
    void endSidebarResize();

    // Save
    void notifySave();

    // Font card struct (must be defined before methods that reference it)
    struct UsFontCardInfo {
        const FontMetadata* meta = nullptr;
        RECT cardRect      = {};
        RECT buttonRect    = {};
        RECT uninstallRect = {};
        bool isActive      = false;
        bool isInstalling  = false;
        bool isFailed      = false;
    };

    // Font card helpers (UnifiedSettingsFontCards.cpp)
    void paintFontSingleCard(Gdiplus::Graphics& g, const UsFontCardInfo& card);
    void paintFontPreview(Gdiplus::Graphics& g, const UsFontCardInfo& card,
                          float cx, float cy, float cw);
    void paintFontBadges(Gdiplus::Graphics& g, const FontMetadata& meta,
                         float x, float y, float maxW);
    void paintFontCardButton(Gdiplus::Graphics& g, const UsFontCardInfo& card);
    void rebuildFontFilteredList();
    int  hitTestFontCard(int mx, int my) const;
    bool hitTestFontCardButton(int idx, int mx, int my) const;
    bool hitTestFontUninstallButton(int idx, int mx, int my) const;
    int  hitTestFontFilterButton(int mx, int my, int contentX) const;
    void onFontCardClick(int idx);
    void onFontCardInstall(int idx);
    void onFontCardUninstall(int idx);
    bool isFontInstalled(const std::wstring& fontName) const;

    // Search (UnifiedSettingsSearch.cpp)
    void createSearchEdit();
    void repositionSearchEdit(int windowWidth);
    void onSearchTextChanged();
    void clearSearch();
    void rebuildVisibleCategories();

    // Helpers
    std::wstring toWide(const std::string& s) const;
    Gdiplus::Color toGdipColor(uint32_t rgb, BYTE a = 255) const;
    Gdiplus::Color toGdipColorCR(COLORREF cr, BYTE a = 255) const;
    void drawRoundedRect(Gdiplus::Graphics& g, Gdiplus::Brush* brush,
                         float x, float y, float w, float h, float r);
    void drawRoundedRectPath(Gdiplus::GraphicsPath& path,
                             float x, float y, float w, float h, float r) const;

    // Index file discovery
    std::string findFontIndexPath() const;
    std::string findThemeIndexPath() const;

    // State
    HWND hwnd_       = nullptr;
    HWND parentHwnd_ = nullptr;
    SaveCallback saveCallback_;
    Config config_;
    Config defaultConfig_;
    ChromeColors chrome_{};
    std::unique_ptr<SettingsModel> model_;

    // Index data
    FontIndex fontIndex_;
    ThemeIndex themeIndex_;

    // Sidebar
    int sidebarWidth_          = kUsSidebarDef;
    std::string selectedCategoryId_ = "general.shell";
    std::vector<std::string> visibleCategoryIds_;

    // Sidebar resize
    bool sidebarResizing_      = false;
    int  sidebarResizeStartX_  = 0;
    int  sidebarResizeStartW_  = 0;

    // Theme cards
    enum class ThemeFilter { All, Dark, Light, Installed };
    ThemeFilter activeThemeFilter_ = ThemeFilter::All;
    std::vector<const ThemeMetadata*> filteredThemes_;

    struct ThemeCardRect {
        const ThemeMetadata* meta = nullptr;
        RECT cardRect   = {};
        RECT buttonRect = {};
        bool isActive   = false;
    };
    std::vector<ThemeCardRect> themeCardRects_;

    void paintSingleThemeCard(Gdiplus::Graphics& g, const ThemeCardRect& card);
    void rebuildThemeFilteredList();
    int  hitTestThemeCard(int mx, int my) const;
    bool hitTestThemeCardButton(int idx, int mx, int my) const;
    int  hitTestThemeFilterButton(int mx, int my, int contentX) const;
    void onThemeCardApply(int idx);

    // Font cards
    enum class FontFilter { All, Installed, NerdFonts, Ligatures };
    FontFilter activeFontFilter_ = FontFilter::All;
    std::vector<const FontMetadata*> filteredFonts_;
    std::vector<UsFontCardInfo> fontCardRects_;
    int fontInstallingCard_ = -1;
    int fontFailedCard_     = -1;
    bool fontIndexReady_    = false;

    // Scroll
    float scrollY_ = 0.f;

    // GDI+
    ULONG_PTR gdiplusToken_ = 0;
    bool gdiplusOwned_      = false;

    // Search
    HWND searchEdit_         = nullptr;
    std::wstring searchText_;
    std::vector<SettingsSearchMatch> searchMatches_;
    std::vector<std::string> allCategoryIds_;

    static const wchar_t* kClassName;
    static bool sClassRegistered;
};

} // namespace termcore

#endif
