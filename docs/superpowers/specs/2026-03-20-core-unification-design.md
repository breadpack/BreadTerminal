# Core Unification Design

## Decisions

- Feature baseline: macOS (all features unified across 3 platforms)
- Communication: Callback interface (`IPlatformHost`)
- Strategy: Big bang — build `TerminalController` + managers in core, then rewire all platforms
- Structure: `TerminalController` facade + internal managers (FontManager, SelectionManager, SearchController, TabController, ConfigApplier)

## Architecture

```
Platform (thin adapter)              Core (all logic)
┌──────────────────────┐      ┌──────────────────────────┐
│ OS event → KeyEvent  │─────→│ TerminalController       │
│ OS event → MouseEvent│─────→│   ├── FontManager        │
│ IPlatformHost impl   │←─────│   ├── SelectionManager   │
│ GPU renderer         │      │   ├── SearchController   │
│ Clipboard/IME        │      │   ├── TabController      │
│ Native dialogs       │      │   └── ConfigApplier      │
└──────────────────────┘      │                          │
                              │ Existing:                │
                              │   Screen, Mux, Pty,      │
                              │   KeybindingManager,     │
                              │   TerminalSearch,        │
                              │   ViCopyMode             │
                              └──────────────────────────┘
```

## IPlatformHost Interface

Platform implements this. Core calls it when platform action is needed.

```cpp
class IPlatformHost {
public:
    virtual ~IPlatformHost() = default;

    // Rendering
    virtual void invalidate() = 0;
    virtual void getViewportSize(int& w, int& h) = 0;

    // Clipboard
    virtual std::string getClipboardText() = 0;
    virtual void setClipboardText(const std::string& text) = 0;

    // Window
    virtual void setWindowTitle(const std::string& title) = 0;
    virtual void toggleFullscreen() = 0;
    virtual void closeWindow() = 0;
    virtual void showConfirmDialog(const std::string& msg,
                                   std::function<void(bool)> cb) = 0;

    // Search UI
    virtual void showSearchBar() = 0;
    virtual void hideSearchBar() = 0;
    virtual void updateSearchResults(int current, int total) = 0;

    // IME
    virtual void positionIME(int x, int y, int height) = 0;

    // Font/color updates
    virtual void onFontChanged(float cellW, float cellH) = 0;
    virtual void onColorsChanged() = 0;

    // Notifications
    virtual void showNotification(const std::string& title,
                                  const std::string& body) = 0;

    // Settings/Hub windows
    virtual void openSettingsWindow(const Config& config) = 0;
    virtual void openThemeHub(const Config& config) = 0;
    virtual void openFontHub(const Config& config) = 0;

    // DPI
    virtual float dpiScale() = 0;
};
```

## TerminalController (facade, ~200 lines)

Single entry point. Platform creates one instance and feeds events into it.

```cpp
class TerminalController {
public:
    TerminalController(IPlatformHost* host, Config config);

    // Event entry points (platform calls these)
    void onKeyEvent(const KeyEvent& e);
    void onMouseEvent(const MouseEvent& e);
    void onResize(int pixelW, int pixelH);
    void onSearchQuery(const std::string& query);
    void onSearchNext();
    void onSearchPrev();
    void onConfigChanged(const Config& newConfig);  // from settings UI
    void onThemeChanged(const std::string& name);
    void onFontChanged(const std::string& family);

    // Lifecycle
    void initTerminal();
    void pollPty();
    void tick();  // called per frame — cursor blink, resize overlay timeout

    // Accessors for renderer
    Screen* activeScreen();
    const Config& config() const;
    const SelectionManager& selection() const;
    const SearchController& search() const;
    float cellWidth() const;
    float cellHeight() const;
    // Tab bar info, resize overlay info, etc.

private:
    IPlatformHost* host_;
    Config config_;
    std::unique_ptr<Mux> mux_;
    std::unique_ptr<KeybindingManager> keybindings_;
    std::unique_ptr<FontManager> fontMgr_;
    std::unique_ptr<SelectionManager> selMgr_;
    std::unique_ptr<SearchController> searchCtrl_;
    std::unique_ptr<TabController> tabCtrl_;
    std::unique_ptr<ConfigApplier> configApplier_;

    void handleAction(Action action);
};
```

## Internal Managers

### FontManager (~150 lines)
Owns FontCollection. Handles font size changes, grid recalculation, pane resize propagation.

```cpp
class FontManager {
public:
    FontManager(FontCollection* fc, float baseFontSize);
    void changeFontSize(float delta);
    void resetFontSize();
    void setFont(const std::string& family, float size);
    float cellWidth() const;
    float cellHeight() const;
    float currentFontSize() const;
    void recalcGrid(int viewportW, int viewportH, int& outRows, int& outCols);
};
```

### SelectionManager (~150 lines)
Manages selection state, pixel-to-grid conversion, text extraction.

```cpp
class SelectionManager {
public:
    struct GridPos { int row = 0; int col = 0; };

    void onMouseDown(int px, int py, float cellW, float cellH, int gridOffsetX, int gridOffsetY);
    void onMouseMove(int px, int py, float cellW, float cellH, int gridOffsetX, int gridOffsetY);
    void onMouseUp(int px, int py, float cellW, float cellH, int gridOffsetX, int gridOffsetY);
    void onDoubleClick(int px, int py, float cellW, float cellH, int gridOffsetX, int gridOffsetY);
    void clear();

    bool hasSelection() const;
    GridPos start() const;
    GridPos end() const;
    std::string getSelectedText(const Screen& screen) const;
};
```

### SearchController (~100 lines)
Wraps TerminalSearch with UI state (active flag, current match index).

```cpp
class SearchController {
public:
    void open();
    void close();
    bool isActive() const;
    void setQuery(const std::string& query, Screen& screen);
    void next(Screen& screen);
    void prev(Screen& screen);
    int currentMatch() const;
    int totalMatches() const;
};
```

### TabController (~100 lines)
Wraps Mux for tab/pane operations. Manages tab bar state.

```cpp
class TabController {
public:
    TabController(Mux* mux, WorkspaceId wsId);
    void createTab(int rows, int cols);
    void closeTab();
    void nextTab();
    void prevTab();
    void switchToTab(int index);  // 0-based
    void closePane();
    void splitRight(int rows, int cols);
    void splitDown(int rows, int cols);

    // Tab bar info for renderer
    struct TabBarInfo { /* titles, active index, hover state */ };
    TabBarInfo tabBarInfo() const;

    // Active pane accessors
    Screen* activeScreen();
    Pty* activePty();
    void syncAfterChange();  // update active pointers after tab/pane switch
};
```

### ConfigApplier (~100 lines)
Applies config changes across all panes and persists to Lua.

```cpp
class ConfigApplier {
public:
    void applyFull(Config& config, Mux& mux, FontManager& fontMgr, IPlatformHost* host);
    void applyColors(Config& config, Mux& mux, IPlatformHost* host);
    void applyFont(Config& config, Mux& mux, FontManager& fontMgr, IPlatformHost* host);
    void persist(const Config& config);  // writeConfigLua
};
```

## Platform Layer After Refactor

Each platform reduces to:

1. **Window creation + event loop** — OS-specific, ~100-200 lines
2. **IPlatformHost implementation** — ~150-200 lines (thin wrappers around OS APIs)
3. **Renderer** — GPU-specific, unchanged (D3D/Metal/GL)
4. **Event translation** — OS event → `KeyEvent`/`MouseEvent`, ~50-100 lines

### KeyEvent / MouseEvent (core types)

```cpp
struct KeyEvent {
    uint32_t keycode;     // platform-neutral keycode
    uint32_t modifiers;   // ModCtrl | ModShift | ModAlt | ModSuper
    std::string text;     // UTF-8 text input (for WM_CHAR / insertText)
    bool isRepeat = false;
};

struct MouseEvent {
    enum Type { Press, Release, Move, DoubleClick, ScrollUp, ScrollDown };
    Type type;
    int x, y;             // pixel coordinates
    uint32_t modifiers;
    int button = 0;       // 0=left, 1=middle, 2=right
    int scrollLines = 0;
};
```

## File Layout

```
core/include/termcore/
    platform_host.h          ← IPlatformHost, KeyEvent, MouseEvent
    terminal_controller.h    ← TerminalController
    font_manager.h           ← FontManager
    selection_manager.h      ← SelectionManager
    search_controller.h      ← SearchController
    tab_controller.h         ← TabController
    config_applier.h         ← ConfigApplier

core/src/
    terminal_controller.cpp  ← TerminalController + handleAction
    font_manager.cpp
    selection_manager.cpp
    search_controller.cpp
    tab_controller.cpp
    config_applier.cpp
```

## What Stays in Platform

| Component | Why |
|-----------|-----|
| Window creation / event loop | OS API (HWND, NSWindow, GtkWindow) |
| GPU renderer | D3D11, Metal, OpenGL — different APIs |
| Font rasterizer | DirectWrite, CoreText, FreeType |
| Clipboard | Win32 API, NSPasteboard, GdkClipboard |
| IME handling | OS-specific composition protocols |
| File watcher | ReadDirectoryChangesW, kqueue, inotify |
| Native dialogs | MessageBox, NSAlert, GtkDialog |
| DPI handling | Per-monitor DPI (Win), backing scale (macOS) |
| Title bar theming | DWM (Win), NSAppearance (macOS) |

## What Moves to Core

| Component | Lines Saved | Current Location |
|-----------|------------|-----------------|
| Action dispatch (handleAction) | ~400 | 3x platform input files |
| Font size management | ~420 | 3x changeFontSize/resetFontSize |
| Config apply callbacks | ~300 | 3x settings/theme/font callbacks |
| Tab management | ~150 | 3x tab create/close/switch |
| Selection management | ~180 | 3x mouse handlers |
| Search UI state | ~120 | 3x open/close/next/prev |
| Grid calculation | ~90 | 3x resizeSwapChain equivalent |
| Vi copy mode integration | ~100 | macOS only → all platforms |
| Mouse protocol encoding | ~40 | partially in platform |
| **Total** | **~1,800** | |
