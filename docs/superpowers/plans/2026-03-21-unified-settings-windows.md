# Unified Settings Window — Core + Windows Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Settings, ThemeHub, and FontHub with a single VSCode-style settings window (Core SettingsModel + Windows GDI+ platform).

**Architecture:** Core `SettingsModel` defines categories/items/search in platform-common code. Windows `UnifiedSettingsWindow` renders using GDI+ with sidebar + content layout. Existing `ConfigApplier`, `FontIndex`, `ThemeIndex`, `FontInstaller`, and `ChromeColors` are reused unchanged.

**Tech Stack:** C++17, Win32 API, GDI+, DirectWrite (font discovery), zlib (font install)

**Spec:** `docs/superpowers/specs/2026-03-21-unified-settings-design.md`

---

## Chunk 1: Core SettingsModel

### Task 1: SettingsModel data structures and category builder

**Files:**
- Create: `core/include/termcore/settings_model.h`
- Create: `core/src/settings_model.cpp`
- Create: `tests/test_settings_model.cpp`

**Context:**
- Config struct: `core/include/termcore/config.h` — all fields listed in spec
- Default config: `core/src/config.cpp` — `Config()` constructor has all defaults
- The model defines the category tree and detects which settings differ from defaults.

- [ ] **Step 1: Write test for SettingsModel construction and category tree**

```cpp
// tests/test_settings_model.cpp
#include <TUnit/TUnit.hpp>
#include "termcore/settings_model.h"
#include "termcore/config.h"

TEST(SettingsModel, BuildsCategoryTree) {
    termcore::Config current;
    termcore::Config defaults;
    termcore::SettingsModel model(current, defaults);

    auto categories = model.topLevelCategories();
    // Expect: General, Appearance, Font, Keyboard, Clipboard
    ASSERT_EQ(categories.size(), 5u);
    ASSERT_EQ(categories[0].id, "general");
    ASSERT_EQ(categories[1].id, "appearance");
    ASSERT_EQ(categories[2].id, "font");
    ASSERT_EQ(categories[3].id, "keyboard");
    ASSERT_EQ(categories[4].id, "clipboard");
}

TEST(SettingsModel, HasSubcategories) {
    termcore::Config current;
    termcore::Config defaults;
    termcore::SettingsModel model(current, defaults);

    auto subs = model.subcategories("general");
    // Expect: Shell, Window, Scrollback
    ASSERT_EQ(subs.size(), 3u);
    ASSERT_EQ(subs[0].id, "general.shell");
    ASSERT_EQ(subs[1].id, "general.window");
    ASSERT_EQ(subs[2].id, "general.scrollback");
}

TEST(SettingsModel, SubcategoryHasItems) {
    termcore::Config current;
    termcore::Config defaults;
    termcore::SettingsModel model(current, defaults);

    auto cat = model.category("general.shell");
    ASSERT_TRUE(cat != nullptr);
    // Shell subcategory has: shell (Text)
    ASSERT_GE(cat->items.size(), 1u);
    ASSERT_EQ(cat->items[0].key, "shell");
    ASSERT_EQ(cat->items[0].type, termcore::SettingType::Text);
}
```

- [ ] **Step 2: Run test to verify it fails**

- [ ] **Step 3: Implement SettingsModel header**

```cpp
// core/include/termcore/settings_model.h
#pragma once
#include "config.h"
#include <string>
#include <vector>
#include <functional>

namespace termcore {

enum class SettingType { Toggle, Text, Number, Slider, Dropdown, ColorPicker };
enum class SectionType { Settings, CardGrid, KeybindingList };

struct SettingMeta {
    float min = 0, max = 0;
    float step = 1;
    std::vector<std::string> options;  // Dropdown choices
};

struct SettingItem {
    std::string key;
    std::string label;
    std::string description;
    SettingType type;
    SettingMeta meta;
    bool modified = false;
};

struct SettingsCategory {
    std::string id;
    std::string label;
    std::string parentId;
    SectionType sectionType = SectionType::Settings;
    std::vector<SettingItem> items;
};

struct SearchMatch {
    std::string categoryId;
    std::string itemKey;       // empty if category-level match
    size_t matchStart = 0;
    size_t matchLength = 0;
};

class SettingsModel {
public:
    SettingsModel(const Config& current, const Config& defaults);

    /// Top-level categories (General, Appearance, Font, Keyboard, Clipboard)
    std::vector<SettingsCategory> topLevelCategories() const;

    /// Subcategories of a top-level category
    std::vector<SettingsCategory> subcategories(const std::string& parentId) const;

    /// Get a specific category by id (e.g. "general.shell")
    const SettingsCategory* category(const std::string& id) const;

    /// All categories (flat list)
    const std::vector<SettingsCategory>& allCategories() const { return categories_; }

    /// Search across all items. Returns matching category IDs.
    std::vector<SearchMatch> search(const std::string& query) const;

    /// Refresh modified flags from current config
    void refreshModified(const Config& current);

private:
    void buildCategories();
    void computeModified();

    Config current_;
    Config defaults_;
    std::vector<SettingsCategory> categories_;
};

} // namespace termcore
```

- [ ] **Step 4: Implement SettingsModel category builder**

```cpp
// core/src/settings_model.cpp
#include "termcore/settings_model.h"
#include <algorithm>
#include <cctype>

namespace termcore {

SettingsModel::SettingsModel(const Config& current, const Config& defaults)
    : current_(current), defaults_(defaults)
{
    buildCategories();
    computeModified();
}

void SettingsModel::buildCategories() {
    categories_.clear();

    // === General ===
    categories_.push_back({"general", "General", "", SectionType::Settings, {}});

    categories_.push_back({"general.shell", "Shell", "general", SectionType::Settings, {
        {"shell", "Shell", "Shell program to launch (empty = system default)",
         SettingType::Text, {}, false},
    }});

    categories_.push_back({"general.window", "Window", "general", SectionType::Settings, {
        {"window_width", "Window Width", "Default window width in pixels",
         SettingType::Number, {200, 4000, 10}, false},
        {"window_height", "Window Height", "Default window height in pixels",
         SettingType::Number, {150, 3000, 10}, false},
        {"window_padding", "Window Padding", "Padding around terminal content in pixels",
         SettingType::Number, {0, 100, 1}, false},
    }});

    categories_.push_back({"general.scrollback", "Scrollback", "general", SectionType::Settings, {
        {"scrollback_limit", "Scrollback Lines", "Maximum number of lines kept in scrollback buffer",
         SettingType::Number, {0, 1000000, 1000}, false},
    }});

    // === Appearance ===
    categories_.push_back({"appearance", "Appearance", "", SectionType::Settings, {}});

    categories_.push_back({"appearance.theme", "Theme", "appearance", SectionType::CardGrid, {}});

    categories_.push_back({"appearance.opacity", "Opacity & Blur", "appearance", SectionType::Settings, {
        {"background_opacity", "Background Opacity", "Window background transparency (0.0 = fully transparent, 1.0 = opaque)",
         SettingType::Slider, {0.0f, 1.0f, 0.05f}, false},
        {"background_blur", "Background Blur", "Blur effect behind transparent background",
         SettingType::Dropdown, {0, 3, 1, {"None", "Low", "Medium", "High"}}, false},
    }});

    categories_.push_back({"appearance.cursor", "Cursor", "appearance", SectionType::Settings, {
        {"cursor_style", "Cursor Style", "Shape of the terminal cursor",
         SettingType::Dropdown, {0, 0, 0, {"block", "underline", "bar"}}, false},
        {"cursor_blink", "Cursor Blink", "Whether the cursor blinks",
         SettingType::Toggle, {}, false},
        {"cursor_blink_interval", "Blink Interval", "Cursor blink speed in seconds",
         SettingType::Slider, {0.1f, 2.0f, 0.1f}, false},
    }});

    categories_.push_back({"appearance.colors", "Colors", "appearance", SectionType::Settings, {
        {"background", "Background", "Terminal background color",
         SettingType::ColorPicker, {}, false},
        {"foreground", "Foreground", "Terminal text color",
         SettingType::ColorPicker, {}, false},
        {"cursor_color", "Cursor Color", "Cursor color",
         SettingType::ColorPicker, {}, false},
        {"selection_background", "Selection Background", "Text selection background",
         SettingType::ColorPicker, {}, false},
        {"selection_foreground", "Selection Foreground", "Text selection foreground",
         SettingType::ColorPicker, {}, false},
        {"minimum_contrast", "Minimum Contrast", "WCAG 2.0 minimum contrast ratio (1.0–21.0)",
         SettingType::Slider, {1.0f, 21.0f, 0.5f}, false},
    }});

    // === Font ===
    categories_.push_back({"font", "Font", "", SectionType::Settings, {}});

    categories_.push_back({"font.family", "Font Family", "font", SectionType::CardGrid, {}});

    categories_.push_back({"font.size", "Font Size & Features", "font", SectionType::Settings, {
        {"font_size", "Font Size", "Font size in points",
         SettingType::Number, {6, 72, 0.5f}, false},
    }});

    // === Keyboard ===
    categories_.push_back({"keyboard", "Keyboard", "", SectionType::Settings, {}});

    categories_.push_back({"keyboard.bindings", "Keybindings", "keyboard",
                           SectionType::KeybindingList, {}});

    // === Clipboard ===
    categories_.push_back({"clipboard", "Clipboard", "", SectionType::Settings, {}});

    categories_.push_back({"clipboard.paste", "Paste Protection", "clipboard", SectionType::Settings, {
        {"clipboard_paste_protection", "Paste Protection", "When to show paste confirmation dialog",
         SettingType::Dropdown, {0, 0, 0, {"never", "multiline", "always"}}, false},
        {"clipboard_paste_bracketed_safe", "Bracketed Paste Trust",
         "Trust bracketed paste mode to safely handle pasted content",
         SettingType::Toggle, {}, false},
    }});

    categories_.push_back({"clipboard.permissions", "Permissions", "clipboard", SectionType::Settings, {
        {"allow_clipboard_write", "Allow Clipboard Write",
         "Allow terminal applications to write to the system clipboard (OSC 52)",
         SettingType::Toggle, {}, false},
    }});
}

void SettingsModel::computeModified() {
    for (auto& cat : categories_) {
        for (auto& item : cat.items) {
            item.modified = false;
            const auto& k = item.key;
            if (k == "shell") item.modified = (current_.shell != defaults_.shell);
            else if (k == "window_width") item.modified = (current_.window_width != defaults_.window_width);
            else if (k == "window_height") item.modified = (current_.window_height != defaults_.window_height);
            else if (k == "window_padding") item.modified = (current_.window_padding != defaults_.window_padding);
            else if (k == "scrollback_limit") item.modified = (current_.scrollback_limit != defaults_.scrollback_limit);
            else if (k == "background_opacity") item.modified = (current_.background_opacity != defaults_.background_opacity);
            else if (k == "background_blur") item.modified = (current_.background_blur != defaults_.background_blur);
            else if (k == "cursor_style") item.modified = (current_.cursor_style != defaults_.cursor_style);
            else if (k == "cursor_blink") item.modified = (current_.cursor_blink != defaults_.cursor_blink);
            else if (k == "cursor_blink_interval") item.modified = (current_.cursor_blink_interval != defaults_.cursor_blink_interval);
            else if (k == "background") item.modified = (current_.background != defaults_.background);
            else if (k == "foreground") item.modified = (current_.foreground != defaults_.foreground);
            else if (k == "cursor_color") item.modified = (current_.cursor_color != defaults_.cursor_color);
            else if (k == "selection_background") item.modified = (current_.selection_background != defaults_.selection_background);
            else if (k == "selection_foreground") item.modified = (current_.selection_foreground != defaults_.selection_foreground);
            else if (k == "minimum_contrast") item.modified = (current_.minimum_contrast != defaults_.minimum_contrast);
            else if (k == "font_size") item.modified = (current_.font_size != defaults_.font_size);
            else if (k == "clipboard_paste_protection") item.modified = (current_.clipboard_paste_protection != defaults_.clipboard_paste_protection);
            else if (k == "clipboard_paste_bracketed_safe") item.modified = (current_.clipboard_paste_bracketed_safe != defaults_.clipboard_paste_bracketed_safe);
            else if (k == "allow_clipboard_write") item.modified = (current_.allow_clipboard_write != defaults_.allow_clipboard_write);
        }
    }
}

std::vector<SettingsCategory> SettingsModel::topLevelCategories() const {
    std::vector<SettingsCategory> result;
    for (const auto& cat : categories_) {
        if (cat.parentId.empty()) result.push_back(cat);
    }
    return result;
}

std::vector<SettingsCategory> SettingsModel::subcategories(const std::string& parentId) const {
    std::vector<SettingsCategory> result;
    for (const auto& cat : categories_) {
        if (cat.parentId == parentId) result.push_back(cat);
    }
    return result;
}

const SettingsCategory* SettingsModel::category(const std::string& id) const {
    for (const auto& cat : categories_) {
        if (cat.id == id) return &cat;
    }
    return nullptr;
}

std::vector<SearchMatch> SettingsModel::search(const std::string& query) const {
    std::vector<SearchMatch> matches;
    if (query.empty()) return matches;

    std::string lowerQuery = query;
    std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    for (const auto& cat : categories_) {
        // Match category label
        std::string lowerLabel = cat.label;
        std::transform(lowerLabel.begin(), lowerLabel.end(), lowerLabel.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        auto catPos = lowerLabel.find(lowerQuery);
        if (catPos != std::string::npos) {
            matches.push_back({cat.id, "", catPos, lowerQuery.size()});
        }

        // Match items
        for (const auto& item : cat.items) {
            std::string lk = item.key, ll = item.label, ld = item.description;
            std::transform(lk.begin(), lk.end(), lk.begin(), [](unsigned char c) { return std::tolower(c); });
            std::transform(ll.begin(), ll.end(), ll.begin(), [](unsigned char c) { return std::tolower(c); });
            std::transform(ld.begin(), ld.end(), ld.begin(), [](unsigned char c) { return std::tolower(c); });

            auto pos = ll.find(lowerQuery);
            if (pos != std::string::npos) {
                matches.push_back({cat.id, item.key, pos, lowerQuery.size()});
            } else {
                pos = ld.find(lowerQuery);
                if (pos != std::string::npos) {
                    matches.push_back({cat.id, item.key, pos, lowerQuery.size()});
                } else {
                    pos = lk.find(lowerQuery);
                    if (pos != std::string::npos) {
                        matches.push_back({cat.id, item.key, pos, lowerQuery.size()});
                    }
                }
            }
        }
    }

    return matches;
}

void SettingsModel::refreshModified(const Config& current) {
    current_ = current;
    computeModified();
}

} // namespace termcore
```

- [ ] **Step 5: Run tests to verify they pass**

- [ ] **Step 6: Write test for search**

```cpp
TEST(SettingsModel, SearchFindsMatchingItems) {
    termcore::Config current;
    termcore::Config defaults;
    termcore::SettingsModel model(current, defaults);

    auto matches = model.search("cursor");
    // Should find: cursor_style, cursor_blink, cursor_blink_interval, cursor_color
    ASSERT_GE(matches.size(), 4u);
}

TEST(SettingsModel, SearchCaseInsensitive) {
    termcore::Config current;
    termcore::Config defaults;
    termcore::SettingsModel model(current, defaults);

    auto matches = model.search("CLIPBOARD");
    ASSERT_GE(matches.size(), 1u);
}

TEST(SettingsModel, SearchEmptyReturnsNothing) {
    termcore::Config current;
    termcore::Config defaults;
    termcore::SettingsModel model(current, defaults);

    auto matches = model.search("");
    ASSERT_EQ(matches.size(), 0u);
}
```

- [ ] **Step 7: Run search tests**

- [ ] **Step 8: Write test for modified detection**

```cpp
TEST(SettingsModel, DetectsModifiedFields) {
    termcore::Config current;
    termcore::Config defaults;
    current.font_size = 18.0f;  // changed from default 14.0f
    current.cursor_blink = false;  // changed from default true

    termcore::SettingsModel model(current, defaults);

    auto fontCat = model.category("font.size");
    ASSERT_TRUE(fontCat != nullptr);
    bool fontSizeModified = false;
    for (const auto& item : fontCat->items) {
        if (item.key == "font_size") fontSizeModified = item.modified;
    }
    ASSERT_TRUE(fontSizeModified);

    auto cursorCat = model.category("appearance.cursor");
    ASSERT_TRUE(cursorCat != nullptr);
    bool cursorBlinkModified = false;
    for (const auto& item : cursorCat->items) {
        if (item.key == "cursor_blink") cursorBlinkModified = item.modified;
    }
    ASSERT_TRUE(cursorBlinkModified);
}

TEST(SettingsModel, DefaultConfigNotModified) {
    termcore::Config defaults;
    termcore::SettingsModel model(defaults, defaults);

    for (const auto& cat : model.allCategories()) {
        for (const auto& item : cat.items) {
            ASSERT_FALSE(item.modified) << "Item " << item.key << " should not be modified";
        }
    }
}
```

- [ ] **Step 9: Run modified tests**

- [ ] **Step 10: Add settings_model.cpp to core CMakeLists.txt**

Modify: `core/CMakeLists.txt` — add `src/settings_model.cpp` to the source list.

- [ ] **Step 11: Commit**

```bash
git add core/include/termcore/settings_model.h core/src/settings_model.cpp tests/test_settings_model.cpp core/CMakeLists.txt
git commit -m "feat: add SettingsModel for unified settings window"
```

---

## Chunk 2: Windows UnifiedSettingsWindow — Window shell + Sidebar

### Task 2: Window class registration, WndProc, and basic layout

**Files:**
- Create: `platform/windows/include/UnifiedSettingsWindow.h`
- Create: `platform/windows/src/UnifiedSettingsWindow.cpp`
- Modify: `platform/windows/CMakeLists.txt` — add new source files

**Context:**
- Follow existing window pattern from `SettingsWindow.h`/`ThemeHubWindow.h`
- Use `ChromeColors` for theming (see `platform/windows/include/ChromeColors.h`)
- Window style: `WS_POPUP | WS_CLIPCHILDREN`, `WS_EX_TOOLWINDOW`
- The window owns a `SettingsModel` instance, `FontIndex`, `ThemeIndex`

- [ ] **Step 1: Create UnifiedSettingsWindow header**

```cpp
// platform/windows/include/UnifiedSettingsWindow.h
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
#include <objidl.h>
#include <gdiplus.h>

#include <functional>
#include <string>
#include <vector>
#include <memory>

#pragma comment(lib, "gdiplus.lib")

namespace termcore {

// Layout constants
constexpr int kUsWinWidth      = 800;
constexpr int kUsWinHeight     = 600;
constexpr int kUsMinWidth      = 640;
constexpr int kUsMinHeight     = 480;
constexpr int kUsTopBarH       = 40;
constexpr int kUsBottomBarH    = 24;
constexpr int kUsSidebarDef    = 200;
constexpr int kUsSidebarMin    = 140;
constexpr int kUsSidebarMax    = 320;
constexpr int kUsContentPad    = 24;
constexpr int kUsItemSpacing   = 16;
constexpr int kUsSearchW       = 280;
constexpr int kUsSearchH       = 28;
constexpr int kUsCatRowH       = 28;
constexpr int kUsSubCatRowH    = 26;
constexpr int kUsItemRowH      = 72;
constexpr int kUsModBarW       = 3;

constexpr UINT_PTR kUsSearchTimerId = 300;
constexpr UINT kUsSearchDelay      = 200;

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
    void paintSidebar(Gdiplus::Graphics& g, int h);
    void paintContent(Gdiplus::Graphics& g, int w, int h);

    // Content sections (separate .cpp files)
    void paintSettingsItems(Gdiplus::Graphics& g, const SettingsCategory& cat,
                            float x, float y, float w, float& outY);
    void paintThemeCards(Gdiplus::Graphics& g, float x, float y, float w, float& outY);
    void paintFontCards(Gdiplus::Graphics& g, float x, float y, float w, float& outY);
    void paintKeybindingList(Gdiplus::Graphics& g, float x, float y, float w, float& outY);

    // Sidebar interaction
    int hitTestSidebar(int mx, int my) const;
    void onSidebarClick(int index);

    // Search
    void onSearchChanged();
    void rebuildFilteredView();

    // Sidebar resize
    void beginSidebarResize(int mx);
    void updateSidebarResize(int mx);
    void endSidebarResize();

    // Config helpers
    void notifySave();

    // Helpers
    std::wstring toWide(const std::string& utf8) const;
    Gdiplus::Color toGdipColor(uint32_t rgb, BYTE a = 255) const;
    Gdiplus::Color toGdipColorCR(COLORREF cr, BYTE a = 255) const;
    void drawRoundedRect(Gdiplus::GraphicsPath& path,
                         float x, float y, float w, float h, float r) const;

    // State
    HWND hwnd_       = nullptr;
    HWND parentHwnd_ = nullptr;
    SaveCallback saveCallback_;
    Config config_;
    Config defaultConfig_;
    ChromeColors chrome_{};

    // GDI+
    ULONG_PTR gdiplusToken_ = 0;
    bool gdiplusOwned_      = false;

    // Model
    std::unique_ptr<SettingsModel> model_;

    // Font/Theme data
    FontIndex fontIndex_;
    ThemeIndex themeIndex_;
    bool fontIndexLoaded_  = false;
    bool themeIndexLoaded_ = false;

    // Sidebar
    int sidebarWidth_ = kUsSidebarDef;
    std::string selectedCategoryId_ = "general.shell";
    std::vector<std::string> visibleCategoryIds_;
    bool sidebarResizing_ = false;
    int sidebarResizeStartX_ = 0;
    int sidebarResizeStartW_ = 0;

    // Search
    HWND searchEdit_ = nullptr;
    std::wstring searchText_;
    std::vector<SearchMatch> searchMatches_;

    // Content scroll
    float scrollY_ = 0.f;
    int hoveredItem_ = -1;

    // Text edit
    HWND activeEdit_ = nullptr;
    std::string editingKey_;

    static const wchar_t* kClassName;
    static bool sClassRegistered;
};

} // namespace termcore

#endif
```

- [ ] **Step 2: Implement UnifiedSettingsWindow.cpp — WndProc shell, show/close, sidebar resize**

```cpp
// platform/windows/src/UnifiedSettingsWindow.cpp
#if defined(_WIN32)

#include "UnifiedSettingsWindow.h"
#include <windowsx.h>

namespace termcore {

const wchar_t* UnifiedSettingsWindow::kClassName = L"BreadTermUnifiedSettings";
bool UnifiedSettingsWindow::sClassRegistered = false;

UnifiedSettingsWindow::UnifiedSettingsWindow() = default;

UnifiedSettingsWindow::~UnifiedSettingsWindow() {
    close();
}

void UnifiedSettingsWindow::registerWindowClass(HINSTANCE hInstance) {
    if (sClassRegistered) return;
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    sClassRegistered = true;
}

void UnifiedSettingsWindow::setConfig(const Config& config) {
    config_ = config;
    defaultConfig_ = Config{};  // default-constructed = defaults
    chrome_ = deriveChrome(config_.background, config_.foreground, config_.palette);
    model_ = std::make_unique<SettingsModel>(config_, defaultConfig_);

    // Build visible category list
    visibleCategoryIds_.clear();
    for (const auto& cat : model_->allCategories()) {
        if (!cat.parentId.empty()) {
            visibleCategoryIds_.push_back(cat.id);
        }
    }

    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void UnifiedSettingsWindow::setSaveCallback(SaveCallback cb) {
    saveCallback_ = std::move(cb);
}

void UnifiedSettingsWindow::show(HWND parent) {
    if (hwnd_) {
        SetForegroundWindow(hwnd_);
        return;
    }
    parentHwnd_ = parent;

    registerWindowClass(GetModuleHandle(nullptr));

    // GDI+
    Gdiplus::GdiplusStartupInput gdipInput;
    Gdiplus::Status st = Gdiplus::GdiplusStartup(&gdiplusToken_, &gdipInput, nullptr);
    gdiplusOwned_ = (st == Gdiplus::Ok);

    // Center on parent
    RECT parentRect;
    GetWindowRect(parent, &parentRect);
    int px = parentRect.left + (parentRect.right - parentRect.left - kUsWinWidth) / 2;
    int py = parentRect.top + (parentRect.bottom - parentRect.top - kUsWinHeight) / 2;

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kClassName, L"Settings",
        WS_POPUP | WS_CLIPCHILDREN | WS_THICKFRAME,
        px, py, kUsWinWidth, kUsWinHeight,
        parent, nullptr, GetModuleHandle(nullptr), this);

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
}

void UnifiedSettingsWindow::close() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (gdiplusOwned_) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
        gdiplusOwned_ = false;
    }
}

LRESULT CALLBACK UnifiedSettingsWindow::WndProc(HWND hwnd, UINT msg,
                                                  WPARAM wParam, LPARAM lParam) {
    UnifiedSettingsWindow* self = nullptr;
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = static_cast<UnifiedSettingsWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<UnifiedSettingsWindow*>(
            GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT UnifiedSettingsWindow::handleMessage(HWND hwnd, UINT msg,
                                              WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT:
        paintWindow(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = kUsMinWidth;
        mmi->ptMinTrackSize.y = kUsMinHeight;
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        // Sidebar resize handle (5px around border)
        if (mx >= sidebarWidth_ - 3 && mx <= sidebarWidth_ + 3
            && my > kUsTopBarH) {
            beginSidebarResize(mx);
            return 0;
        }

        // Sidebar click
        if (mx < sidebarWidth_ && my > kUsTopBarH) {
            int idx = hitTestSidebar(mx, my);
            if (idx >= 0) onSidebarClick(idx);
            return 0;
        }

        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        if (sidebarResizing_) {
            updateSidebarResize(mx);
            return 0;
        }

        // Change cursor near sidebar edge
        if (mx >= sidebarWidth_ - 3 && mx <= sidebarWidth_ + 3
            && my > kUsTopBarH) {
            SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
        }

        return 0;
    }

    case WM_LBUTTONUP:
        if (sidebarResizing_) {
            endSidebarResize();
            return 0;
        }
        return 0;

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        scrollY_ -= delta * 0.5f;
        if (scrollY_ < 0) scrollY_ = 0;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            close();
            return 0;
        }
        break;

    case WM_DESTROY:
        hwnd_ = nullptr;
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void UnifiedSettingsWindow::beginSidebarResize(int mx) {
    sidebarResizing_ = true;
    sidebarResizeStartX_ = mx;
    sidebarResizeStartW_ = sidebarWidth_;
    SetCapture(hwnd_);
}

void UnifiedSettingsWindow::updateSidebarResize(int mx) {
    int delta = mx - sidebarResizeStartX_;
    int newW = sidebarResizeStartW_ + delta;
    newW = (std::max)(kUsSidebarMin, (std::min)(kUsSidebarMax, newW));
    if (newW != sidebarWidth_) {
        sidebarWidth_ = newW;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void UnifiedSettingsWindow::endSidebarResize() {
    sidebarResizing_ = false;
    ReleaseCapture();
}

int UnifiedSettingsWindow::hitTestSidebar(int mx, int my) const {
    int y = kUsTopBarH + 8;
    for (int i = 0; i < static_cast<int>(visibleCategoryIds_.size()); ++i) {
        int rowH = kUsSubCatRowH;
        if (my >= y && my < y + rowH) return i;
        y += rowH;
    }
    return -1;
}

void UnifiedSettingsWindow::onSidebarClick(int index) {
    if (index >= 0 && index < static_cast<int>(visibleCategoryIds_.size())) {
        selectedCategoryId_ = visibleCategoryIds_[index];
        scrollY_ = 0;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void UnifiedSettingsWindow::onSearchChanged() {
    // Will be implemented in UnifiedSettingsSearch.cpp
}

void UnifiedSettingsWindow::rebuildFilteredView() {
    // Will be implemented in UnifiedSettingsSearch.cpp
}

void UnifiedSettingsWindow::notifySave() {
    if (saveCallback_) saveCallback_(config_);
}

std::wstring UnifiedSettingsWindow::toWide(const std::string& utf8) const {
    if (utf8.empty()) return {};
    int sz = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring w(sz - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, w.data(), sz);
    return w;
}

Gdiplus::Color UnifiedSettingsWindow::toGdipColor(uint32_t rgb, BYTE a) const {
    return Gdiplus::Color(a,
        static_cast<BYTE>((rgb >> 16) & 0xFF),
        static_cast<BYTE>((rgb >> 8) & 0xFF),
        static_cast<BYTE>(rgb & 0xFF));
}

Gdiplus::Color UnifiedSettingsWindow::toGdipColorCR(COLORREF cr, BYTE a) const {
    return Gdiplus::Color(a, GetRValue(cr), GetGValue(cr), GetBValue(cr));
}

void UnifiedSettingsWindow::drawRoundedRect(Gdiplus::GraphicsPath& path,
    float x, float y, float w, float h, float r) const {
    path.Reset();
    float d = r * 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
}

} // namespace termcore

#endif
```

- [ ] **Step 3: Implement paintWindow, paintTopBar, paintBottomBar, paintSidebar stubs**

These paint methods go in `UnifiedSettingsWindow.cpp` (same file for now, paint sidebar in separate file in Task 3).

```cpp
void UnifiedSettingsWindow::paintWindow(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;

    // Double buffer
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    Gdiplus::Graphics g(memDC);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    // Background
    Gdiplus::SolidBrush bgBrush(toGdipColorCR(chrome_.background));
    g.FillRectangle(&bgBrush, 0, 0, w, h);

    paintTopBar(g, w);
    paintSidebar(g, h);
    paintContent(g, w, h);
    paintBottomBar(g, w, h);

    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);

    EndPaint(hwnd, &ps);
}

void UnifiedSettingsWindow::paintTopBar(Gdiplus::Graphics& g, int w) {
    Gdiplus::SolidBrush barBg(toGdipColorCR(chrome_.titleBar));
    g.FillRectangle(&barBg, 0, 0, w, kUsTopBarH);

    // Search placeholder
    Gdiplus::Font font(L"Segoe UI", 11);
    Gdiplus::SolidBrush dimBrush(toGdipColorCR(chrome_.dimText));
    Gdiplus::StringFormat sf;
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    float searchX = static_cast<float>(sidebarWidth_) + 16.0f;
    Gdiplus::RectF searchRect(searchX, 6, static_cast<float>(kUsSearchW),
                               static_cast<float>(kUsSearchH));
    // Search field background
    Gdiplus::SolidBrush fieldBg(toGdipColorCR(chrome_.fieldBg));
    Gdiplus::GraphicsPath searchPath;
    drawRoundedRect(searchPath, searchRect.X, searchRect.Y,
                    searchRect.Width, searchRect.Height, 4);
    g.FillPath(&fieldBg, &searchPath);

    if (searchText_.empty()) {
        g.DrawString(L"Search settings...", -1, &font, searchRect, &sf, &dimBrush);
    }

    // Open Lua button
    Gdiplus::SolidBrush textBrush(toGdipColorCR(chrome_.textColor));
    Gdiplus::Font btnFont(L"Segoe UI", 9);
    float btnX = static_cast<float>(w) - 100.0f;
    Gdiplus::RectF btnRect(btnX, 8, 84, 24);
    Gdiplus::GraphicsPath btnPath;
    drawRoundedRect(btnPath, btnRect.X, btnRect.Y, btnRect.Width, btnRect.Height, 4);
    Gdiplus::SolidBrush btnBg(toGdipColorCR(chrome_.btnInactive));
    g.FillPath(&btnBg, &btnPath);
    Gdiplus::StringFormat btnSf;
    btnSf.SetAlignment(Gdiplus::StringAlignmentCenter);
    btnSf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    g.DrawString(L"Open Lua", -1, &btnFont, btnRect, &btnSf, &textBrush);
}

void UnifiedSettingsWindow::paintBottomBar(Gdiplus::Graphics& g, int w, int h) {
    float y = static_cast<float>(h - kUsBottomBarH);
    Gdiplus::SolidBrush barBg(toGdipColorCR(chrome_.titleBar));
    g.FillRectangle(&barBg, 0.0f, y, static_cast<float>(w),
                    static_cast<float>(kUsBottomBarH));

    Gdiplus::Font font(L"Segoe UI", 8);
    Gdiplus::SolidBrush dimBrush(toGdipColorCR(chrome_.dimText));
    Gdiplus::StringFormat sf;
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::RectF textRect(8, y, 300, static_cast<float>(kUsBottomBarH));
    g.DrawString(L"BreadTerminal", -1, &font, textRect, &sf, &dimBrush);
}
```

- [ ] **Step 4: Add new files to CMakeLists.txt**

Modify: `platform/windows/CMakeLists.txt` — add `UnifiedSettingsWindow.cpp` to `termcore_windows` sources.

- [ ] **Step 5: Build and verify window opens**

- [ ] **Step 6: Commit**

```bash
git add platform/windows/include/UnifiedSettingsWindow.h platform/windows/src/UnifiedSettingsWindow.cpp platform/windows/CMakeLists.txt
git commit -m "feat: add UnifiedSettingsWindow shell with sidebar resize"
```

---

### Task 3: Sidebar rendering

**Files:**
- Create: `platform/windows/src/UnifiedSettingsSidebar.cpp`
- Modify: `platform/windows/CMakeLists.txt`

**Context:**
- Sidebar shows top-level categories as bold headers, subcategories as indented items
- Selected item highlighted with accent color background
- CardGrid/KeybindingList sections show an icon or indicator

- [ ] **Step 1: Implement paintSidebar**

```cpp
// platform/windows/src/UnifiedSettingsSidebar.cpp
#if defined(_WIN32)

#include "UnifiedSettingsWindow.h"

namespace termcore {

void UnifiedSettingsWindow::paintSidebar(Gdiplus::Graphics& g, int h) {
    float sw = static_cast<float>(sidebarWidth_);
    float topY = static_cast<float>(kUsTopBarH);
    float bottomY = static_cast<float>(h - kUsBottomBarH);

    // Sidebar background (slightly different from main)
    Gdiplus::SolidBrush sidebarBg(toGdipColorCR(chrome_.titleBar));
    g.FillRectangle(&sidebarBg, 0.0f, topY, sw, bottomY - topY);

    // Separator line
    Gdiplus::Pen sepPen(toGdipColorCR(chrome_.btnInactive), 1.0f);
    g.DrawLine(&sepPen, sw, topY, sw, bottomY);

    Gdiplus::Font catFont(L"Segoe UI Semibold", 10);
    Gdiplus::Font subFont(L"Segoe UI", 10);
    Gdiplus::SolidBrush textBrush(toGdipColorCR(chrome_.textColor));
    Gdiplus::SolidBrush dimBrush(toGdipColorCR(chrome_.dimText));
    Gdiplus::SolidBrush accentBrush(toGdipColorCR(chrome_.accent));
    Gdiplus::StringFormat sf;
    sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);

    float y = topY + 8;

    auto topCats = model_->topLevelCategories();
    for (const auto& topCat : topCats) {
        // Top-level category header
        Gdiplus::RectF catRect(12, y, sw - 16, static_cast<float>(kUsCatRowH));
        g.DrawString(toWide(topCat.label).c_str(), -1, &catFont, catRect, &sf, &dimBrush);
        y += kUsCatRowH;

        // Subcategories
        auto subs = model_->subcategories(topCat.id);
        for (const auto& sub : subs) {
            bool selected = (sub.id == selectedCategoryId_);

            if (selected) {
                // Accent highlight bar
                Gdiplus::SolidBrush selBg(toGdipColorCR(chrome_.accent, 40));
                g.FillRectangle(&selBg, 0.0f, y, sw, static_cast<float>(kUsSubCatRowH));
                // Left accent bar
                g.FillRectangle(&accentBrush, 0.0f, y, 3.0f,
                                static_cast<float>(kUsSubCatRowH));
            }

            Gdiplus::RectF subRect(28, y, sw - 32, static_cast<float>(kUsSubCatRowH));
            g.DrawString(toWide(sub.label).c_str(), -1, &subFont, subRect, &sf,
                         selected ? &textBrush : &dimBrush);
            y += kUsSubCatRowH;
        }

        y += 4;  // gap between top-level groups
    }
}

} // namespace termcore

#endif
```

- [ ] **Step 2: Add to CMakeLists.txt, build, verify**

- [ ] **Step 3: Commit**

```bash
git add platform/windows/src/UnifiedSettingsSidebar.cpp platform/windows/CMakeLists.txt
git commit -m "feat: add sidebar rendering for unified settings"
```

---

## Chunk 3: Content area — Settings items rendering

### Task 4: Settings items rendering (Toggle, Text, Number, Slider, Dropdown, ColorPicker)

**Files:**
- Create: `platform/windows/src/UnifiedSettingsContent.cpp`
- Modify: `platform/windows/CMakeLists.txt`

**Context:**
- Reads the selected category from `model_` and renders its items
- Each item shows: modified indicator, label, description, control widget
- Reuse widget patterns from existing `SettingsControls.cpp` and `SettingsPaint.cpp`

- [ ] **Step 1: Implement paintContent dispatching to section type**

```cpp
// platform/windows/src/UnifiedSettingsContent.cpp
#if defined(_WIN32)

#include "UnifiedSettingsWindow.h"

namespace termcore {

void UnifiedSettingsWindow::paintContent(Gdiplus::Graphics& g, int w, int h) {
    float contentX = static_cast<float>(sidebarWidth_) + 1;
    float contentW = static_cast<float>(w) - contentX;
    float topY = static_cast<float>(kUsTopBarH);
    float bottomY = static_cast<float>(h - kUsBottomBarH);

    // Clip to content area
    Gdiplus::RectF clipRect(contentX, topY, contentW, bottomY - topY);
    g.SetClip(clipRect);

    float y = topY + kUsContentPad - scrollY_;
    float innerX = contentX + kUsContentPad;
    float innerW = contentW - kUsContentPad * 2;

    const auto* cat = model_->category(selectedCategoryId_);
    if (!cat) {
        g.ResetClip();
        return;
    }

    // Section title
    Gdiplus::Font titleFont(L"Segoe UI Semibold", 16);
    Gdiplus::SolidBrush textBrush(toGdipColorCR(chrome_.textColor));
    g.DrawString(toWide(cat->label).c_str(), -1, &titleFont,
                 Gdiplus::PointF(innerX, y), &textBrush);
    y += 36;

    // Render based on section type
    switch (cat->sectionType) {
    case SectionType::CardGrid:
        if (cat->id == "appearance.theme")
            paintThemeCards(g, innerX, y, innerW, y);
        else if (cat->id == "font.family")
            paintFontCards(g, innerX, y, innerW, y);
        break;

    case SectionType::KeybindingList:
        paintKeybindingList(g, innerX, y, innerW, y);
        break;

    case SectionType::Settings:
        paintSettingsItems(g, *cat, innerX, y, innerW, y);
        break;
    }

    g.ResetClip();
}

void UnifiedSettingsWindow::paintSettingsItems(Gdiplus::Graphics& g,
    const SettingsCategory& cat, float x, float y, float w, float& outY)
{
    Gdiplus::Font labelFont(L"Segoe UI Semibold", 11);
    Gdiplus::Font descFont(L"Segoe UI", 9);
    Gdiplus::SolidBrush textBrush(toGdipColorCR(chrome_.textColor));
    Gdiplus::SolidBrush dimBrush(toGdipColorCR(chrome_.dimText));
    Gdiplus::SolidBrush accentBrush(toGdipColorCR(chrome_.accent));
    Gdiplus::SolidBrush modBrush(Gdiplus::Color(255, 0, 122, 204)); // #007ACC

    for (const auto& item : cat.items) {
        float itemTop = y;

        // Modified indicator
        if (item.modified) {
            g.FillRectangle(&modBrush, x - 8, y + 2, 3.0f,
                            static_cast<float>(kUsItemRowH) - 4);
        }

        // Label
        g.DrawString(toWide(item.label).c_str(), -1, &labelFont,
                     Gdiplus::PointF(x, y), &textBrush);
        y += 20;

        // Description
        Gdiplus::RectF descRect(x, y, w, 16);
        g.DrawString(toWide(item.description).c_str(), -1, &descFont,
                     descRect, nullptr, &dimBrush);
        y += 18;

        // Control widget
        float controlX = x;
        float controlW = (std::min)(w, 300.0f);

        switch (item.type) {
        case SettingType::Toggle: {
            // Toggle switch 44x22
            float tw = 44, th = 22;
            bool on = false;
            if (item.key == "cursor_blink") on = config_.cursor_blink;
            else if (item.key == "clipboard_paste_bracketed_safe") on = config_.clipboard_paste_bracketed_safe;
            else if (item.key == "allow_clipboard_write") on = config_.allow_clipboard_write;

            Gdiplus::GraphicsPath trackPath;
            drawRoundedRect(trackPath, controlX, y, tw, th, th / 2);
            if (on) {
                g.FillPath(&accentBrush, &trackPath);
            } else {
                Gdiplus::SolidBrush offBrush(toGdipColorCR(chrome_.btnInactive));
                g.FillPath(&offBrush, &trackPath);
            }
            // Knob
            float knobX = on ? (controlX + tw - th + 2) : (controlX + 2);
            Gdiplus::SolidBrush knobBrush(Gdiplus::Color(255, 255, 255, 255));
            g.FillEllipse(&knobBrush, knobX, y + 2, th - 4, th - 4);
            y += th + 4;
            break;
        }
        case SettingType::Text: {
            // Text field
            Gdiplus::GraphicsPath fieldPath;
            drawRoundedRect(fieldPath, controlX, y, controlW, 28, 4);
            Gdiplus::SolidBrush fieldBg(toGdipColorCR(chrome_.fieldBg));
            g.FillPath(&fieldBg, &fieldPath);

            std::string val;
            if (item.key == "shell") val = config_.shell;
            Gdiplus::Font fieldFont(L"Segoe UI", 10);
            Gdiplus::RectF fieldRect(controlX + 8, y + 4, controlW - 16, 20);
            g.DrawString(toWide(val).c_str(), -1, &fieldFont,
                         fieldRect, nullptr, &textBrush);
            y += 32;
            break;
        }
        case SettingType::Number: {
            // Number field with stepper
            Gdiplus::GraphicsPath fieldPath;
            drawRoundedRect(fieldPath, controlX, y, 120, 28, 4);
            Gdiplus::SolidBrush fieldBg(toGdipColorCR(chrome_.fieldBg));
            g.FillPath(&fieldBg, &fieldPath);

            // Get value as string
            std::string val;
            if (item.key == "font_size") val = std::to_string(static_cast<int>(config_.font_size * 10) / 10.0f).substr(0, 4);
            else if (item.key == "window_width") val = std::to_string(config_.window_width);
            else if (item.key == "window_height") val = std::to_string(config_.window_height);
            else if (item.key == "window_padding") val = std::to_string(config_.window_padding);
            else if (item.key == "scrollback_limit") val = std::to_string(config_.scrollback_limit);

            Gdiplus::Font fieldFont(L"Segoe UI", 10);
            Gdiplus::RectF fieldRect(controlX + 8, y + 4, 96, 20);
            g.DrawString(toWide(val).c_str(), -1, &fieldFont,
                         fieldRect, nullptr, &textBrush);

            // +/- buttons
            Gdiplus::Font btnFont(L"Segoe UI", 12);
            Gdiplus::SolidBrush btnBg(toGdipColorCR(chrome_.btnInactive));
            Gdiplus::GraphicsPath minusPath, plusPath;
            drawRoundedRect(minusPath, controlX + 124, y, 28, 28, 4);
            drawRoundedRect(plusPath, controlX + 156, y, 28, 28, 4);
            g.FillPath(&btnBg, &minusPath);
            g.FillPath(&btnBg, &plusPath);
            Gdiplus::StringFormat cSf;
            cSf.SetAlignment(Gdiplus::StringAlignmentCenter);
            cSf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::RectF minusR(controlX + 124, y, 28, 28);
            Gdiplus::RectF plusR(controlX + 156, y, 28, 28);
            g.DrawString(L"\u2212", -1, &btnFont, minusR, &cSf, &textBrush);
            g.DrawString(L"+", -1, &btnFont, plusR, &cSf, &textBrush);
            y += 32;
            break;
        }
        case SettingType::Slider: {
            // Slider
            float sliderW = controlW;
            float trackY = y + 8;
            float trackH = 4;

            // Get normalized value
            float val = 0, minV = item.meta.min, maxV = item.meta.max;
            if (item.key == "background_opacity") val = config_.background_opacity;
            else if (item.key == "cursor_blink_interval") val = config_.cursor_blink_interval;
            else if (item.key == "minimum_contrast") val = config_.minimum_contrast;

            float ratio = (maxV > minV) ? (val - minV) / (maxV - minV) : 0;
            float fillW = ratio * sliderW;

            // Track background
            Gdiplus::SolidBrush trackBg(toGdipColorCR(chrome_.btnInactive));
            g.FillRectangle(&trackBg, controlX, trackY, sliderW, trackH);
            // Track fill
            g.FillRectangle(&accentBrush, controlX, trackY, fillW, trackH);
            // Handle
            float handleX = controlX + fillW - 8;
            g.FillEllipse(&accentBrush, handleX, trackY - 6, 16, 16);

            // Value label
            wchar_t valBuf[32];
            swprintf(valBuf, 32, L"%.2f", static_cast<double>(val));
            Gdiplus::Font valFont(L"Segoe UI", 9);
            g.DrawString(valBuf, -1, &valFont,
                         Gdiplus::PointF(controlX + sliderW + 8, y + 2), &dimBrush);
            y += 24;
            break;
        }
        case SettingType::Dropdown: {
            // Dropdown (rendered as pill buttons)
            float pillX = controlX;
            for (const auto& opt : item.meta.options) {
                std::string currentVal;
                if (item.key == "cursor_style") currentVal = config_.cursor_style;
                else if (item.key == "clipboard_paste_protection") currentVal = config_.clipboard_paste_protection;
                else if (item.key == "background_blur") currentVal = std::to_string(static_cast<int>(pillX - controlX) / 84);  // index

                bool active = (opt == currentVal);
                // For background_blur, compare by index
                if (item.key == "background_blur") {
                    int idx = static_cast<int>(&opt - &item.meta.options[0]);
                    active = (idx == config_.background_blur);
                }

                float pillW = 72;
                Gdiplus::GraphicsPath pillPath;
                drawRoundedRect(pillPath, pillX, y, pillW, 26, 13);
                if (active) {
                    g.FillPath(&accentBrush, &pillPath);
                } else {
                    Gdiplus::SolidBrush pillBg(toGdipColorCR(chrome_.btnInactive));
                    g.FillPath(&pillBg, &pillPath);
                }
                Gdiplus::Font pillFont(L"Segoe UI", 9);
                Gdiplus::StringFormat pillSf;
                pillSf.SetAlignment(Gdiplus::StringAlignmentCenter);
                pillSf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                Gdiplus::RectF pillRect(pillX, y, pillW, 26);
                g.DrawString(toWide(opt).c_str(), -1, &pillFont, pillRect, &pillSf,
                             active ? &Gdiplus::SolidBrush(Gdiplus::Color(255, 255, 255, 255))
                                    : textBrush);
                pillX += pillW + 6;
            }
            y += 30;
            break;
        }
        case SettingType::ColorPicker: {
            // Color swatch
            uint32_t color = 0;
            if (item.key == "background") color = config_.background;
            else if (item.key == "foreground") color = config_.foreground;
            else if (item.key == "cursor_color") color = config_.cursor_color;
            else if (item.key == "selection_background") color = config_.selection_background;
            else if (item.key == "selection_foreground") color = config_.selection_foreground;

            Gdiplus::SolidBrush swatchBrush(toGdipColor(color));
            Gdiplus::GraphicsPath swatchPath;
            drawRoundedRect(swatchPath, controlX, y, 28, 28, 4);
            g.FillPath(&swatchBrush, &swatchPath);
            Gdiplus::Pen border(toGdipColorCR(chrome_.dimText), 1);
            g.DrawPath(&border, &swatchPath);

            // Hex label
            wchar_t hexBuf[16];
            swprintf(hexBuf, 16, L"#%06X", color);
            Gdiplus::Font hexFont(L"Segoe UI", 9);
            g.DrawString(hexBuf, -1, &hexFont,
                         Gdiplus::PointF(controlX + 36, y + 6), &dimBrush);
            y += 32;
            break;
        }
        }

        y += kUsItemSpacing;
    }

    outY = y;
}

// Stubs for card/keybinding sections (will be implemented in later tasks)
void UnifiedSettingsWindow::paintThemeCards(Gdiplus::Graphics& g,
    float x, float y, float w, float& outY) {
    Gdiplus::Font font(L"Segoe UI", 11);
    Gdiplus::SolidBrush dim(toGdipColorCR(chrome_.dimText));
    g.DrawString(L"Theme cards will appear here", -1, &font,
                 Gdiplus::PointF(x, y), &dim);
    outY = y + 40;
}

void UnifiedSettingsWindow::paintFontCards(Gdiplus::Graphics& g,
    float x, float y, float w, float& outY) {
    Gdiplus::Font font(L"Segoe UI", 11);
    Gdiplus::SolidBrush dim(toGdipColorCR(chrome_.dimText));
    g.DrawString(L"Font cards will appear here", -1, &font,
                 Gdiplus::PointF(x, y), &dim);
    outY = y + 40;
}

void UnifiedSettingsWindow::paintKeybindingList(Gdiplus::Graphics& g,
    float x, float y, float w, float& outY) {
    Gdiplus::Font font(L"Segoe UI", 11);
    Gdiplus::SolidBrush dim(toGdipColorCR(chrome_.dimText));
    g.DrawString(L"Keybindings will appear here", -1, &font,
                 Gdiplus::PointF(x, y), &dim);
    outY = y + 40;
}

} // namespace termcore

#endif
```

- [ ] **Step 2: Add to CMakeLists.txt, build, verify**

- [ ] **Step 3: Commit**

```bash
git add platform/windows/src/UnifiedSettingsContent.cpp platform/windows/CMakeLists.txt
git commit -m "feat: add settings content rendering with all widget types"
```

---

### Task 5: Wire up UnifiedSettingsWindow to IPlatformHost

**Files:**
- Modify: `core/include/termcore/platform_host.h` — remove `openThemeHub`, `openFontHub`
- Modify: `core/include/termcore/keybinding.h` — merge OpenThemeHub/OpenFontHub into OpenSettings
- Modify: `core/src/terminal_controller.cpp` — update action dispatch
- Modify: `platform/windows/include/TerminalWindowState.h` — replace 3 windows with 1
- Modify: `platform/windows/src/TerminalWindowState.cpp` — implement openSettingsWindow with UnifiedSettingsWindow

- [ ] **Step 1: Update IPlatformHost**

Remove `openThemeHub` and `openFontHub`. Keep `openSettingsWindow`.

- [ ] **Step 2: Update Action enum and TerminalController dispatch**

Map `OpenThemeHub` and `OpenFontHub` actions to `openSettingsWindow` (they now all open the same window, but can navigate to a specific section).

- [ ] **Step 3: Update TerminalWindowState**

Replace `themeHub`, `fontHub`, `settingsWin` members with single `unifiedSettings` member. Update `openSettingsWindow` to create and show `UnifiedSettingsWindow`.

- [ ] **Step 4: Build and verify all three old keybindings open the new unified window**

- [ ] **Step 5: Commit**

```bash
git add core/include/termcore/platform_host.h core/include/termcore/keybinding.h core/src/terminal_controller.cpp platform/windows/include/TerminalWindowState.h platform/windows/src/TerminalWindowState.cpp
git commit -m "feat: wire UnifiedSettingsWindow to IPlatformHost, replace 3 windows"
```

---

## Chunk 4: Theme cards, Font cards, Keybinding list

### Task 6: Theme card grid

**Files:**
- Create: `platform/windows/src/UnifiedSettingsThemeCards.cpp`
- Modify: `platform/windows/CMakeLists.txt`

**Context:**
- Port from existing `ThemeHubCards.cpp` card rendering
- Add filter bar (All, Dark, Light, Installed)
- Add local search field
- Reuse `ThemeIndex` for data, existing apply logic

- [ ] **Step 1: Implement paintThemeCards with filter bar and card grid**

Port the card rendering code from `ThemeHubCards.cpp`, adapting it to work within the content area of the unified window. Include filter toggle buttons and a local search field.

- [ ] **Step 2: Add click handling for theme cards (apply/install)**

Add hit testing and click handling in `handleMessage` for theme card buttons. Reuse existing theme apply callback pattern.

- [ ] **Step 3: Build and verify theme section**

- [ ] **Step 4: Commit**

```bash
git add platform/windows/src/UnifiedSettingsThemeCards.cpp platform/windows/CMakeLists.txt
git commit -m "feat: add theme card grid to unified settings"
```

---

### Task 7: Font card grid

**Files:**
- Create: `platform/windows/src/UnifiedSettingsFontCards.cpp`
- Modify: `platform/windows/CMakeLists.txt`

**Context:**
- Port from existing `FontHubCards.cpp`
- Add filter bar (All, Installed, NerdFonts, Ligatures)
- Add local search, install/apply/uninstall buttons
- Reuse `FontIndex`, `FontInstaller`

- [ ] **Step 1: Implement paintFontCards with filter bar and card grid**

Port from `FontHubCards.cpp`. Include badges, preview rendering, install state handling.

- [ ] **Step 2: Add click handling for font cards (install/apply/uninstall)**

- [ ] **Step 3: Build and verify font section**

- [ ] **Step 4: Commit**

```bash
git add platform/windows/src/UnifiedSettingsFontCards.cpp platform/windows/CMakeLists.txt
git commit -m "feat: add font card grid to unified settings"
```

---

### Task 8: Keybinding list

**Files:**
- Create: `platform/windows/src/UnifiedSettingsKeybindings.cpp`
- Modify: `platform/windows/CMakeLists.txt`

**Context:**
- Port from existing `SettingsControls.cpp` keybinding tab
- Scrollable list of trigger+action pairs
- Add/edit/delete functionality
- Inline editor with validation

- [ ] **Step 1: Implement paintKeybindingList with add/edit/delete**

- [ ] **Step 2: Add click handling and inline editor**

- [ ] **Step 3: Build and verify keybindings section**

- [ ] **Step 4: Commit**

```bash
git add platform/windows/src/UnifiedSettingsKeybindings.cpp platform/windows/CMakeLists.txt
git commit -m "feat: add keybinding list to unified settings"
```

---

## Chunk 5: Search, interaction, and cleanup

### Task 9: Global search

**Files:**
- Create: `platform/windows/src/UnifiedSettingsSearch.cpp`
- Modify: `platform/windows/CMakeLists.txt`

**Context:**
- Top search bar with native EDIT control
- Filters sidebar to show only matching categories
- Highlights matched text in content area

- [ ] **Step 1: Implement search edit control and filtering logic**

Create the HWND search edit in WM_CREATE. On text change (with 200ms debounce timer), call `model_->search()` and filter `visibleCategoryIds_` to only show matching categories.

- [ ] **Step 2: Implement search highlight rendering**

In `paintSettingsItems`, check if any `SearchMatch` applies to the current item and draw a semi-transparent accent background behind the matched text range.

- [ ] **Step 3: Build and verify search**

- [ ] **Step 4: Commit**

```bash
git add platform/windows/src/UnifiedSettingsSearch.cpp platform/windows/CMakeLists.txt
git commit -m "feat: add global search to unified settings"
```

---

### Task 10: Settings item interaction (click handlers for all widget types)

**Files:**
- Modify: `platform/windows/src/UnifiedSettingsWindow.cpp`
- Modify: `platform/windows/src/UnifiedSettingsContent.cpp`

**Context:**
- Toggle: click toggles value
- Text/Number: click opens inline EDIT control
- Slider: click/drag changes value
- Dropdown/pills: click selects option
- ColorPicker: click opens ChooseColorW dialog
- All changes: update `config_`, call `notifySave()`, refresh `model_`

- [ ] **Step 1: Add content area hit testing**

Map mouse coordinates to setting items and their controls. Track item rects during paint pass.

- [ ] **Step 2: Implement click handlers for each widget type**

- [ ] **Step 3: Implement inline text editor (reuse existing pattern from SettingsWindow)**

- [ ] **Step 4: Build and verify all interactions**

- [ ] **Step 5: Commit**

```bash
git add platform/windows/src/UnifiedSettingsWindow.cpp platform/windows/src/UnifiedSettingsContent.cpp
git commit -m "feat: add interaction handlers for all settings widget types"
```

---

### Task 11: Open Lua button + cleanup

**Files:**
- Modify: `platform/windows/src/UnifiedSettingsWindow.cpp` — Open Lua click handler
- Delete old files (after verifying everything works):
  - `platform/windows/src/SettingsWindow.cpp`
  - `platform/windows/src/SettingsPaint.cpp`
  - `platform/windows/src/SettingsControls.cpp`
  - `platform/windows/src/SettingsTabPanels.cpp`
  - `platform/windows/include/SettingsWindow.h`
  - `platform/windows/src/ThemeHubWindow.cpp`
  - `platform/windows/src/ThemeHubPaint.cpp`
  - `platform/windows/src/ThemeHubCards.cpp`
  - `platform/windows/include/ThemeHubWindow.h`
  - `platform/windows/src/FontHubWindow.cpp`
  - `platform/windows/src/FontHubPaint.cpp`
  - `platform/windows/src/FontHubCards.cpp`
  - `platform/windows/include/FontHubWindow.h`
- Modify: `platform/windows/CMakeLists.txt` — remove old files, verify new files

- [ ] **Step 1: Implement Open Lua button**

Click handler in `handleMessage` WM_LBUTTONDOWN: detect click on "Open Lua" button rect. If config file doesn't exist, create with defaults. Open with `ShellExecuteW(nullptr, L"open", path, ...)`.

- [ ] **Step 2: Full build and manual test of entire window**

Test all sections: General, Appearance (Theme cards, Opacity, Cursor, Colors), Font (Font cards, Size), Keyboard (Keybindings), Clipboard. Verify search, sidebar resize, modified indicators, all widget interactions.

- [ ] **Step 3: Delete old files**

- [ ] **Step 4: Update CMakeLists.txt**

- [ ] **Step 5: Final build and verify**

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: complete unified settings window, remove old Settings/ThemeHub/FontHub"
```
