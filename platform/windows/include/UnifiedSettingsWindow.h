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

    // Helpers
    std::wstring toWide(const std::string& s) const;
    Gdiplus::Color toGdipColor(uint32_t rgb, BYTE a = 255) const;
    Gdiplus::Color toGdipColorCR(COLORREF cr, BYTE a = 255) const;
    void drawRoundedRect(Gdiplus::Graphics& g, Gdiplus::Brush* brush,
                         float x, float y, float w, float h, float r);

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

    // Scroll
    float scrollY_ = 0.f;

    // GDI+
    ULONG_PTR gdiplusToken_ = 0;
    bool gdiplusOwned_      = false;

    // Search
    HWND searchEdit_         = nullptr;
    std::wstring searchText_;

    static const wchar_t* kClassName;
    static bool sClassRegistered;
};

} // namespace termcore

#endif
