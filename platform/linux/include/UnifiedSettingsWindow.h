#ifndef BREAD_UNIFIED_SETTINGS_WINDOW_H
#define BREAD_UNIFIED_SETTINGS_WINDOW_H

#if defined(__linux__)

#include "termcore/settings_model.h"
#include "termcore/config.h"
#include "termcore/font_index.h"
#include "termcore/keybinding.h"
#include "termcore/theme_index.h"

#include <gtk/gtk.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace termcore {

// ---------- Layout constants (match Windows/macOS) ----------
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

// Theme card constants
constexpr int kTcCardW       = 190;
constexpr int kTcCardH       = 134;
constexpr int kTcCardGap     = 12;
constexpr int kTcFilterBtnW  = 72;
constexpr int kTcFilterBtnH  = 26;
constexpr int kTcFilterGap   = 6;
constexpr int kTcSwatchSize  = 14;
constexpr int kTcSwatchGap   = 3;
constexpr int kTcFilterBarH  = 36;

// Font card constants
constexpr int kFcCardW       = 220;
constexpr int kFcCardH       = 150;
constexpr int kFcCardGap     = 12;
constexpr int kFcFilterBtnW  = 80;
constexpr int kFcFilterBtnH  = 26;
constexpr int kFcFilterGap   = 6;
constexpr int kFcFilterBarH  = 36;

// ---------- UnifiedSettingsWindow ----------
class UnifiedSettingsWindow {
public:
    using SaveCallback = std::function<void(const Config& config)>;

    UnifiedSettingsWindow();
    ~UnifiedSettingsWindow();

    void setConfig(const Config& config);
    void setSaveCallback(SaveCallback cb);
    void show(GtkWindow* parent);
    void close();

    bool isVisible() const;

private:
    // --- Window creation helpers ---
    void buildUI();
    void applyCss();

    // --- Sidebar (UnifiedSettingsSidebar.cpp) ---
    void buildSidebar();
    void rebuildSidebarRows();
    void onSidebarRowSelected(GtkListBox* box, GtkListBoxRow* row);

    // --- Content area (UnifiedSettingsContent.cpp) ---
    void buildContentArea();
    void showCategoryContent(const std::string& categoryId);
    void showSettingsItems(const SettingsCategory* cat);
    void clearContent();

    // --- Theme cards (UnifiedSettingsThemeCards.cpp) ---
    void showThemeCards();
    void rebuildThemeFilteredList();
    static void drawThemeCard(GtkDrawingArea* area, cairo_t* cr,
                              int width, int height, gpointer data);
    void onThemeFilterChanged(int filterIndex);
    void onThemeCardApply(int idx);

    // --- Font cards (UnifiedSettingsFontCards.cpp) ---
    void showFontCards();
    void rebuildFontFilteredList();
    static void drawFontCard(GtkDrawingArea* area, cairo_t* cr,
                             int width, int height, gpointer data);
    void onFontFilterChanged(int filterIndex);
    void onFontCardClick(int idx);
    void onFontCardInstall(int idx);
    void onFontCardUninstall(int idx);

    // --- Keybinding editor (UnifiedSettingsKeybindings.cpp) ---
    void showKeybindingList();
    void onKeybindingEdit(int idx);
    void showKeyCaptureDialog(int idx);
    std::string comboToDisplayString(const KeyCombo& combo) const;
    std::string actionToDisplayString(Action action) const;
    int findConflict(const KeyCombo& combo, int excludeIdx) const;
    void syncKeybindingsToConfig();

    // --- Search ---
    void onSearchChanged(const char* text);
    void rebuildVisibleCategories();

    // --- Save ---
    void notifySave();

    // --- Helpers ---
    std::string findIndexPath(const char* filename) const;

    // Color helpers: convert 0xRRGGBB to cairo RGBA
    static void setCairoColor(cairo_t* cr, uint32_t rgb, double alpha = 1.0);
    static void setCairoColorDim(cairo_t* cr, uint32_t rgb, double alpha = 0.5);

    // --- State ---
    GtkWindow* window_       = nullptr;
    GtkWindow* parentWindow_ = nullptr;
    SaveCallback saveCallback_;
    Config config_;
    Config defaultConfig_;
    std::unique_ptr<SettingsModel> model_;

    // Index data
    FontIndex fontIndex_;
    ThemeIndex themeIndex_;
    bool fontIndexReady_ = false;

    // Sidebar
    GtkWidget* sidebar_        = nullptr; // GtkListBox
    GtkWidget* sidebarScroll_  = nullptr;
    std::string selectedCategoryId_ = "general.shell";
    std::vector<std::string> visibleCategoryIds_;
    std::vector<std::string> allCategoryIds_;

    // Content
    GtkWidget* contentScroll_  = nullptr; // GtkScrolledWindow
    GtkWidget* contentBox_     = nullptr; // GtkBox (vertical) inside scroll

    // Search
    GtkWidget* searchEntry_    = nullptr;
    std::string searchText_;
    std::vector<SettingsSearchMatch> searchMatches_;

    // Top-level layout
    GtkWidget* paned_          = nullptr; // GtkPaned
    GtkWidget* headerBar_      = nullptr;
    GtkWidget* statusLabel_    = nullptr;

    // Theme filter
    enum class ThemeFilter { All, Dark, Light, Installed };
    ThemeFilter activeThemeFilter_ = ThemeFilter::All;
    std::vector<const ThemeMetadata*> filteredThemes_;

    // Font filter
    enum class FontFilter { All, Installed, NerdFonts, Ligatures };
    FontFilter activeFontFilter_ = FontFilter::All;
    std::vector<const FontMetadata*> filteredFonts_;

    // Keybinding editor
    KeybindingManager keybindMgr_;

    // CSS provider
    GtkCssProvider* cssProvider_ = nullptr;
};

} // namespace termcore

#endif // __linux__
#endif // BREAD_UNIFIED_SETTINGS_WINDOW_H
