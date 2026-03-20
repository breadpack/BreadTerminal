#pragma once
#if defined(_WIN32)

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
constexpr int kSetWinWidth   = 620;
constexpr int kSetWinHeight  = 520;
constexpr int kSetTitleH     = 32;
constexpr int kSetTabBarH    = 40;
constexpr int kSetPadding    = 20;
constexpr int kSetRowHeight  = 44;
constexpr int kSetLabelW     = 160;
constexpr int kSetFieldH     = 28;
constexpr int kSetFieldW     = 260;
constexpr int kSetToggleW    = 44;
constexpr int kSetToggleH    = 22;
constexpr int kSetPillBtnW   = 80;
constexpr int kSetPillBtnH   = 28;
constexpr int kSetPillGap    = 4;
constexpr int kSetSwatchSize = 28;
constexpr int kSetSliderW    = 200;
constexpr int kSetSliderH    = 20;
constexpr int kSetTabCount   = 5;

// ---------- Enums ----------
enum class SettingsTab { General, Appearance, Font, Keys, Clipboard };

// ---------- Keybinding row info (for Keys tab) ----------
struct KeyBindingRow {
    RECT triggerRect;
    RECT actionRect;
};

// ---------- SettingsWindow ----------
class SettingsWindow {
public:
    using SaveCallback = std::function<void(const Config& config)>;

    SettingsWindow();
    ~SettingsWindow();

    void setConfig(const Config& config);
    void setSaveCallback(SaveCallback cb);
    void show(HWND parent);
    void close();

    static void registerWindowClass(HINSTANCE hInstance);

private:
    // Window procedure
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handleMessage(HWND, UINT, WPARAM, LPARAM);

    // Painting (SettingsPaint.cpp)
    void paintWindow(HWND hwnd);
    void paintTitleBar(Gdiplus::Graphics& g, int w);
    void paintTabBar(Gdiplus::Graphics& g, int w);
    void paintGeneralTab(Gdiplus::Graphics& g, int x, int y, int w, int h);
    void paintAppearanceTab(Gdiplus::Graphics& g, int x, int y, int w, int h);
    void paintFontTab(Gdiplus::Graphics& g, int x, int y, int w, int h);
    void paintKeysTab(Gdiplus::Graphics& g, int x, int y, int w, int h);
    void paintClipboardTab(Gdiplus::Graphics& g, int x, int y, int w, int h);

    // Drawing helpers (SettingsPaint.cpp)
    void drawLabel(Gdiplus::Graphics& g, const wchar_t* text,
                   float x, float y);
    void drawTextField(Gdiplus::Graphics& g, const wchar_t* text,
                       float x, float y, float w, bool focused = false);
    void drawPillButtons(Gdiplus::Graphics& g, const wchar_t** labels,
                         int count, int selected, float x, float y);
    void drawToggle(Gdiplus::Graphics& g, bool on, float x, float y);
    void drawColorSwatch(Gdiplus::Graphics& g, uint32_t color,
                         float x, float y);
    void drawSlider(Gdiplus::Graphics& g, float value,
                    float x, float y, float w);
    void drawRoundedRect(Gdiplus::Graphics& g, Gdiplus::Brush* brush,
                         float x, float y, float w, float h, float r);
    void drawRoundedRectOutline(Gdiplus::Graphics& g, Gdiplus::Pen* pen,
                                float x, float y, float w, float h, float r);

    // Controls / interaction (SettingsControls.cpp)
    void onLButtonDown(int mx, int my);
    void onLButtonUp(int mx, int my);
    void onMouseMove(int mx, int my);
    int  hitTestTab(int mx, int my) const;
    bool hitTestCloseButton(int mx, int my) const;
    void handleGeneralClick(int mx, int my);
    void handleAppearanceClick(int mx, int my);
    void handleFontClick(int mx, int my);
    void handleKeysClick(int mx, int my);
    void handleClipboardClick(int mx, int my);
    void beginTextEdit(int fieldId, float x, float y, float w,
                       const std::wstring& value);
    void commitTextEdit();
    void destroyTextEdit();
    void openColorPicker(uint32_t& colorField);
    void notifySave();

    // Helpers
    Gdiplus::Color toGdipColor(uint32_t rgb, BYTE a = 255) const;
    Gdiplus::Color toGdipColorCR(COLORREF cr, BYTE a = 255) const;
    std::wstring toWide(const std::string& s) const;
    std::string toUtf8(const std::wstring& s) const;
    int contentTop() const { return kSetTitleH + kSetTabBarH; }

    // State
    HWND hwnd_       = nullptr;
    HWND parentHwnd_ = nullptr;
    SaveCallback saveCallback_;
    Config config_;
    ChromeColors chrome_{};

    // GDI+
    ULONG_PTR gdiplusToken_ = 0;
    bool gdiplusOwned_      = false;

    // Tab
    SettingsTab activeTab_ = SettingsTab::General;

    // Text editing
    HWND editCtrl_     = nullptr;
    int  editFieldId_  = -1;
    HFONT editFont_    = nullptr;
    WNDPROC origEditProc_ = nullptr;

    // Slider dragging
    bool  sliderDragging_ = false;

    // Keys tab scroll
    int keysScrollY_ = 0;
    std::vector<KeyBindingRow> keyBindingRows_;

    static const wchar_t* kClassName;
    static bool sClassRegistered;
};

} // namespace termcore

#endif
