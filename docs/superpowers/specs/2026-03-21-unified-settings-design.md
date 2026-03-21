# Unified Settings Window — VSCode-Style

## Goal

Replace the three separate settings windows (Settings, ThemeHub, FontHub) with a single unified settings window following VSCode's settings UI pattern. All OS platforms (Windows, macOS, Linux) share the same layout and design spec, rendered with platform-native toolkits.

## Architecture

### Core Layer (platform-common)

**SettingsModel** — defines categories, items, search, and modified-state detection.

```cpp
enum class SettingType { Toggle, Text, Number, Slider, Dropdown, ColorPicker };
enum class SectionType { Settings, CardGrid, KeybindingList };

struct SettingMeta {
    float min = 0, max = 0;                       // Number, Slider
    float step = 1;                                // Number, Slider
    std::vector<std::string> options;              // Dropdown
};

struct SettingItem {
    std::string key;           // config field name, e.g. "font_size"
    std::string label;         // display name
    std::string description;   // help text shown below label
    SettingType type;
    SettingMeta meta;          // type-dependent constraints
    bool modified;             // differs from default value
};

struct SettingsCategory {
    std::string id;            // e.g. "appearance.theme"
    std::string label;         // e.g. "Theme"
    std::string parentId;      // e.g. "appearance" (empty for top-level)
    SectionType sectionType = SectionType::Settings;
    std::vector<SettingItem> items;  // only for SectionType::Settings
};
```

**SettingsModel** constructor takes `const Config& current, const Config& defaultConfig`:
- `defaultConfig` is produced by `loadDefaultConfig()` (already exists in `config.cpp`), initialized once at app startup and stored in SettingsModel.
- Builds the category tree with all setting items.
- Computes `modified` per item (current != default field value).
- Executes search: case-insensitive match across `key`, `label`, `description`. Returns filtered categories and items with match ranges for highlight rendering.

Reuses existing: `Config`, `FontIndex`, `ThemeIndex`, `ConfigApplier`.

### Platform Layer

Each platform reads `SettingsModel` and renders natively:

| Platform | Toolkit | Sidebar resize |
|----------|---------|----------------|
| Windows  | GDI+    | WM_MOUSEMOVE + WM_LBUTTONDOWN |
| macOS    | Cocoa   | NSSplitView built-in |
| Linux    | GTK/Cairo | GtkPaned built-in |

### Settings change flow

```
User interaction → SettingsModel update → ConfigApplier apply → Terminal reflects immediately + Lua file saved
```

If `ConfigApplier::apply()` or `persist()` fails, revert the setting in the UI to its previous value and show an error notification.

## Navigation Structure

Sidebar supports 1-level nesting. Top-level categories expand/collapse on click. Subcategories are expanded by default. Click a subcategory to scroll content to that section.

```
[Search settings...]                    [Open Lua]

General
  Shell
  Window
  Scrollback
Appearance
  Theme              ← CardGrid (ThemeIndex)
  Opacity & Blur
  Cursor
Font
  Font Family        ← CardGrid (FontIndex)
  Font Size & Features
Keyboard
  Keybindings        ← KeybindingList
Clipboard
  Paste Protection
  Permissions
```

## Window Layout

```
┌─────────────────────────────────────────────┐
│ [🔍 Search settings...]          [Open Lua] │  40px top bar
├────────────┬────────────────────────────────┤
│ sidebar    │ content area (scrollable)      │
│ (resizable)│                                │
│            │ Section title                  │
│ category   │ ┌─────────────────────────┐   │
│ tree       │ │ ● Setting Label          │   │
│            │ │   Description text       │   │
│            │ │   [control widget]       │   │
│            │ └─────────────────────────┘   │
│            │                                │
├────────────┴────────────────────────────────┤
│ breadterminal v1.0                          │  24px bottom bar
└─────────────────────────────────────────────┘
```

### Dimensions (all platforms)

- Default window: 800×600, resizable, minimum 640×480
- Sidebar: 200px default, 140px min, 320px max, drag-resizable at right edge
- Sidebar width clamped at boundaries; preference persisted to config
- Content padding: 24px
- Item spacing: 16px
- Card sizes: 190×134 (theme), 220×150 (font), 12px gap
- Section title: 20px font. Item label: 14px. Description: 12px.

## Setting Item Types

| Type | Widget | Example |
|------|--------|---------|
| Toggle | Switch (on/off) | cursor_blink |
| Text | Text input field | shell |
| Number | Numeric input + stepper | font_size, scrollback_limit |
| Slider | Horizontal slider + value | background_opacity (0.0–1.0) |
| Dropdown | Select menu | cursor_style (block/bar/underline) |
| ColorPicker | Color swatch + picker dialog | background, foreground, palette |

## CardGrid Sections

CardGrid sections render a grid of cards instead of individual setting items. They have their own local filter bar and search field within the content area.

### Theme (Appearance > Theme)

- Filter bar: All, Dark, Light, Installed — toggle buttons, multiple selectable (AND logic). "All" clears other filters.
- Local search field filters cards by name within this section
- Card shows: name, 6-color palette preview, Apply/Applied button
- Reuses existing `ThemeIndex` data and theme install/apply logic

### Font Family (Font > Font Family)

- Filter bar: All, Installed, NerdFonts, Ligatures — same toggle behavior as Theme
- Local search field filters cards by name within this section
- Card shows: name, font preview text, badges (NerdFont/Ligatures), Install/Apply/Uninstall
- Reuses existing `FontIndex` data and `FontInstaller` logic

### Install/Uninstall Behavior

- **Install**: Downloads resource, persists to platform-specific location, shows progress then success/error notification. Does NOT auto-apply.
- **Uninstall**: Removes local files and registry entries. If currently applied, reverts to default (Consolas/Menlo/monospace for fonts, default theme for themes).
- **Button states**: "Install" → "Installing..." → "Apply" (installed) / "Failed" (error) → "Applied" (active) with uninstall "×" on installed non-active items.

## KeybindingList Section

- Scrollable list of trigger + action pairs
- Each row: trigger key combo (left), action name (right), edit/delete buttons
- "Add Keybinding" button at top
- Edit opens inline row editor with trigger input + action dropdown
- Validation: duplicate triggers rejected with inline error message

## Search

- **Global search** (top bar): filters all settings across all categories by matching `key`, `label`, `description`. Sidebar shows only categories with matches. CardGrid/KeybindingList sections appear if their category label matches.
- **Local search** (within CardGrid sections): filters visible cards by name. Only active when that section is displayed.
- Global search takes precedence; local search is cleared when global search is active.
- Highlight style: semi-transparent accent color background on matched text in labels and descriptions.
- Clear search (click × or empty field) restores full category tree.

## Modified Indicator

- Each setting item compared against `defaultConfig`
- Modified items show a 3px blue (#007ACC) vertical bar on left edge (VSCode style)
- For CardGrid sections: the modified indicator shows on the section header if a non-default theme/font is applied
- For KeybindingList: modified indicator shows if any keybinding differs from defaults

## Open Lua Button

- Top-right button opens config.lua in system default text editor
- If config.lua does not exist, create it with default content first
- Paths: `~/.config/breadterminal/config.lua` (Linux), `~/Library/Application Support/BreadTerminal/config.lua` (macOS), `%APPDATA%/BreadTerminal/config.lua` (Windows)
- Settings window remains open; existing ConfigWatcher reloads on external changes

## File Structure

### Core (platform-common)

```
core/include/termcore/settings_model.h
core/src/settings_model.cpp
```

### Windows

```
platform/windows/include/UnifiedSettingsWindow.h
platform/windows/src/
  UnifiedSettingsWindow.cpp          — WndProc, events, sidebar resize
  UnifiedSettingsSidebar.cpp         — left navigation rendering
  UnifiedSettingsContent.cpp         — right content (general items)
  UnifiedSettingsThemeCards.cpp      — Theme card grid
  UnifiedSettingsFontCards.cpp       — Font card grid
  UnifiedSettingsSearch.cpp          — search bar + filtering
```

### macOS

```
platform/macos/src/
  UnifiedSettingsWindowController.mm — NSSplitView master
  UnifiedSettingsSidebar.mm          — NSOutlineView categories
  UnifiedSettingsContent.mm          — NSScrollView content
  UnifiedSettingsThemeCards.mm       — Theme cards
  UnifiedSettingsFontCards.mm        — Font cards
```

### Linux

```
platform/linux/src/
  UnifiedSettingsWindow.cpp          — GtkPaned based
  UnifiedSettingsSidebar.cpp         — GtkTreeView
  UnifiedSettingsContent.cpp         — GtkScrolledWindow + Cairo
  UnifiedSettingsThemeCards.cpp      — Cairo card rendering
  UnifiedSettingsFontCards.cpp       — Cairo card rendering
```

### Files to delete

Old files are deleted only after the new UnifiedSettings implementation is complete and verified per platform.

**Windows:** `SettingsWindow.h`, `SettingsWindow.cpp`, `SettingsPaint.cpp`, `SettingsControls.cpp`, `SettingsTabPanels.cpp`, `ThemeHubWindow.h`, `ThemeHubWindow.cpp`, `ThemeHubPaint.cpp`, `ThemeHubCards.cpp`, `FontHubWindow.h`, `FontHubWindow.cpp`, `FontHubPaint.cpp`, `FontHubCards.cpp`

**macOS:** `PreferencesWindowController.mm`, all `Prefs*ViewController.mm`, `ThemeHubViewController.mm`, `FontHubViewController.mm`, `ThemeCardView.mm`, `FontCardView.mm`

## IPlatformHost Changes

Remove:
```cpp
virtual void openThemeHub(const Config& config) = 0;
virtual void openFontHub(const Config& config) = 0;
```

Keep (unified):
```cpp
virtual void openSettingsWindow(const Config& config) = 0;
```

`ConfigApplier` methods (`applyFull`, `applyColors`, `applyFont`, `persist`) remain unchanged — the unified window calls them the same way the old separate windows did.

## Platform Rendering Requirements

All platforms must render these common elements identically in layout (pixel dimensions may vary slightly for native widget sizing):

1. **Sidebar**: category tree with 1-level nesting, expand/collapse, selected highlight, hover effect
2. **Search bar**: text input with search icon, clear button
3. **Toggle switch**: animated on/off with accent color
4. **Slider**: horizontal track + thumb + value display
5. **Dropdown**: native platform select/combobox
6. **Color picker**: swatch preview + click to open native color dialog
7. **Card grid**: scrollable grid with cards, local filter bar + search, install/apply/uninstall buttons
8. **Keybinding list**: scrollable rows with edit/delete, add button, inline editor
9. **Modified indicator**: 3px blue (#007ACC) vertical bar on left edge of modified items
10. **Resize handle**: sidebar right border, col-resize cursor on hover, clamped to [140, 320]px
