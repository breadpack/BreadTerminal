# Core Unification Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move all business logic from platform layers into core, leaving only OS-native adapters in platform code.

**Architecture:** `TerminalController` facade in core owns all terminal state and dispatches actions. Platform implements `IPlatformHost` callback interface for OS-specific operations (clipboard, rendering, windows). Internal managers (FontManager, SelectionManager, SearchController, TabController, ConfigApplier) keep files under 500 lines.

**Tech Stack:** C++17, existing termcore library, platform APIs (WinAPI/Cocoa/GTK4)

**Spec:** `docs/superpowers/specs/2026-03-20-core-unification-design.md`

---

## File Structure

### New Core Files
- `core/include/termcore/platform_host.h` — IPlatformHost, KeyEvent, MouseEvent structs
- `core/include/termcore/terminal_controller.h` — TerminalController facade
- `core/include/termcore/font_manager.h` — FontManager
- `core/include/termcore/selection_manager.h` — SelectionManager
- `core/include/termcore/search_controller.h` — SearchController
- `core/include/termcore/tab_controller.h` — TabController
- `core/include/termcore/config_applier.h` — ConfigApplier
- `core/src/terminal_controller.cpp` — TerminalController implementation
- `core/src/font_manager.cpp` — FontManager implementation
- `core/src/selection_manager.cpp` — SelectionManager implementation
- `core/src/search_controller.cpp` — SearchController implementation
- `core/src/tab_controller.cpp` — TabController implementation
- `core/src/config_applier.cpp` — ConfigApplier implementation

### Modified Core Files
- `core/CMakeLists.txt` — add new source files

### Modified Platform Files (heavy rewrite)
- `platform/windows/include/TerminalWindowState.h` — thin adapter with IPlatformHost
- `platform/windows/src/TerminalWindowState.cpp` — delegate to TerminalController
- `platform/windows/src/TerminalWindowInput.cpp` — event translation only
- `platform/windows/src/TerminalWindow.cpp` — message loop delegates to controller
- `platform/macos/src/TerminalViewInput.mm` — event translation only
- `platform/macos/src/TerminalView.mm` — delegate to controller
- `platform/macos/src/AppDelegate.mm` — create controller, implement host
- `platform/linux/src/TerminalInput.cpp` — event translation only
- `platform/linux/src/TerminalWidget.cpp` — delegate to controller

---

## Chunk 1: Core Types & Interfaces

### Task 1: Create platform_host.h

**Files:**
- Create: `core/include/termcore/platform_host.h`

- [ ] **Step 1: Write KeyEvent, MouseEvent, and IPlatformHost**

```cpp
// core/include/termcore/platform_host.h
#ifndef TERMCORE_PLATFORM_HOST_H
#define TERMCORE_PLATFORM_HOST_H

#include "termcore/config.h"
#include <cstdint>
#include <functional>
#include <string>

namespace termcore {

// Modifier flags (bit field)
enum Modifier : uint8_t {
    ModNone  = 0,
    ModShift = 1 << 0,
    ModCtrl  = 1 << 1,
    ModAlt   = 1 << 2,
    ModSuper = 1 << 3,
};

struct KeyEvent {
    uint32_t keycode = 0;
    uint8_t modifiers = ModNone;
    std::string text;         // UTF-8 text input
    bool isRepeat = false;
};

struct MouseEvent {
    enum Type { Press, Release, Move, DoubleClick, ScrollUp, ScrollDown };
    Type type = Press;
    int x = 0, y = 0;        // pixel coordinates
    uint8_t modifiers = ModNone;
    int button = 0;           // 0=left, 1=middle, 2=right
    int scrollLines = 0;
};

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

    // Font/color update notifications
    virtual void onFontChanged(float cellW, float cellH) = 0;
    virtual void onColorsChanged() = 0;
    virtual void onGridSizeChanged(int rows, int cols) = 0;

    // Notifications
    virtual void showNotification(const std::string& title,
                                  const std::string& body) = 0;

    // Settings/Hub windows
    virtual void openSettingsWindow(const Config& config) = 0;
    virtual void openThemeHub(const Config& config) = 0;
    virtual void openFontHub(const Config& config) = 0;

    // DPI
    virtual float dpiScale() = 0;

    // PTY factory — platform creates PTY since it's OS-specific
    virtual std::unique_ptr<class Pty> createPty(const std::string& shell,
                                                  int rows, int cols) = 0;
};

} // namespace termcore
#endif
```

- [ ] **Step 2: Commit**

```bash
git add core/include/termcore/platform_host.h
git commit -m "feat: add IPlatformHost interface and input event types"
```

### Task 2: Create SelectionManager

**Files:**
- Create: `core/include/termcore/selection_manager.h`
- Create: `core/src/selection_manager.cpp`

- [ ] **Step 1: Write SelectionManager header**

Manages selection state and text extraction. Platform feeds pixel coordinates, manager converts to grid and tracks selection.

```cpp
// core/include/termcore/selection_manager.h
#ifndef TERMCORE_SELECTION_MANAGER_H
#define TERMCORE_SELECTION_MANAGER_H

#include "termcore/screen.h"
#include <string>

namespace termcore {

class SelectionManager {
public:
    struct GridPos { int row = 0; int col = 0; };

    void onMouseDown(int px, int py, float cellW, float cellH,
                     int offsetX, int offsetY);
    void onMouseMove(int px, int py, float cellW, float cellH,
                     int offsetX, int offsetY);
    void onMouseUp(int px, int py, float cellW, float cellH,
                   int offsetX, int offsetY);
    void onDoubleClick(int px, int py, float cellW, float cellH,
                       int offsetX, int offsetY, const Screen& screen);
    void selectAll(int rows, int cols);
    void clear();

    bool hasSelection() const { return hasSelection_; }
    bool isDragging() const { return isDragging_; }
    GridPos start() const { return start_; }
    GridPos end() const { return end_; }

    std::string getSelectedText(const Screen& screen) const;

private:
    GridPos pixelToGrid(int px, int py, float cellW, float cellH,
                        int offsetX, int offsetY) const;
    void expandWordAt(const Screen& screen, GridPos pos);

    GridPos start_;
    GridPos end_;
    bool hasSelection_ = false;
    bool isDragging_ = false;
};

} // namespace termcore
#endif
```

- [ ] **Step 2: Write SelectionManager implementation**

Port logic from Windows TerminalSelection.cpp and macOS selection handling.

- [ ] **Step 3: Commit**

### Task 3: Create FontManager

**Files:**
- Create: `core/include/termcore/font_manager.h`
- Create: `core/src/font_manager.cpp`

- [ ] **Step 1: Write FontManager**

Owns font sizing logic, grid calculation, pane resize propagation. Does NOT own FontCollection (platform-specific rasterizer underneath).

```cpp
// core/include/termcore/font_manager.h
#ifndef TERMCORE_FONT_MANAGER_H
#define TERMCORE_FONT_MANAGER_H

#include "termcore/font/font_collection.h"
#include <string>

namespace termcore {

class FontManager {
public:
    FontManager(FontCollection* fc, const std::string& family, float baseSize);

    void changeFontSize(float delta);
    void resetFontSize();
    void setFont(const std::string& family, float size);

    float cellWidth() const { return cellW_; }
    float cellHeight() const { return cellH_; }
    float currentFontSize() const { return currentSize_; }
    float baseFontSize() const { return baseSize_; }
    const std::string& fontFamily() const { return family_; }

    // Recalculate grid dimensions from viewport pixel size
    void recalcGrid(int viewportW, int viewportH,
                    int& outRows, int& outCols) const;

private:
    void refreshMetrics();

    FontCollection* fc_;          // not owned — platform manages lifetime
    std::string family_;
    float baseSize_;
    float currentSize_;
    float cellW_ = 8.0f;
    float cellH_ = 16.0f;
};

} // namespace termcore
#endif
```

- [ ] **Step 2: Implement FontManager**

Port changeFontSize/resetFontSize from TerminalWindowState.cpp. Core logic: clamp size → setPrimaryFont → get metrics → update cell dimensions.

- [ ] **Step 3: Commit**

### Task 4: Create SearchController

**Files:**
- Create: `core/include/termcore/search_controller.h`
- Create: `core/src/search_controller.cpp`

- [ ] **Step 1: Write SearchController**

Wraps existing TerminalSearch with UI state management.

```cpp
// core/include/termcore/search_controller.h
#ifndef TERMCORE_SEARCH_CONTROLLER_H
#define TERMCORE_SEARCH_CONTROLLER_H

#include "termcore/search.h"
#include "termcore/screen.h"
#include <string>

namespace termcore {

class SearchController {
public:
    void open();
    void close();
    bool isActive() const { return active_; }

    void setQuery(const std::string& query, Screen& screen);
    void next(Screen& screen);
    void prev(Screen& screen);

    int currentMatch() const;
    int totalMatches() const;
    const TerminalSearch& search() const { return search_; }

private:
    TerminalSearch search_;
    bool active_ = false;
};

} // namespace termcore
#endif
```

- [ ] **Step 2: Implement SearchController**
- [ ] **Step 3: Commit**

### Task 5: Create TabController

**Files:**
- Create: `core/include/termcore/tab_controller.h`
- Create: `core/src/tab_controller.cpp`

- [ ] **Step 1: Write TabController**

Wraps Mux for tab/pane operations. Owns PaneState map, active pointers, tab bar info generation.

```cpp
// core/include/termcore/tab_controller.h
#ifndef TERMCORE_TAB_CONTROLLER_H
#define TERMCORE_TAB_CONTROLLER_H

#include "termcore/mux.h"
#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include "termcore/pty.h"
#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>

namespace termcore {

struct PaneState {
    PaneId id = kInvalidPane;
    std::unique_ptr<Screen> screen;
    std::unique_ptr<VtParser> parser;
    std::unique_ptr<Pty> pty;
};

// Called when a new pane needs a PTY
using PtyFactory = std::function<std::unique_ptr<Pty>(int rows, int cols)>;

class TabController {
public:
    TabController(std::unique_ptr<Mux> mux, WorkspaceId wsId,
                  PtyFactory ptyFactory, const Config& config);

    // Tab operations
    void createTab(int rows, int cols);
    void closeTab();
    void nextTab();
    void prevTab();
    void switchToTab(int index);

    // Pane operations
    void splitRight(int rows, int cols);
    void splitDown(int rows, int cols);
    void closePane();
    void focusDirection(int dr, int dc);  // -1/0/+1

    // Active pane access
    Screen* activeScreen();
    Pty* activePty();
    PaneState* activePane();

    // Iterate all panes (for pollPty, resize, color updates)
    template<typename F> void forEachPane(F&& fn);

    // Tab bar info for renderer
    struct TabInfo { std::string title; bool active; };
    std::vector<TabInfo> tabBarInfo() const;
    int tabCount() const;

    // Pane lifecycle
    PaneId createPaneState(int rows, int cols);
    void destroyPaneState(PaneId id);
    bool hasAnyAlivePty() const;
    void syncActivePointers();

    Mux* mux() { return mux_.get(); }
    WorkspaceId workspaceId() const { return wsId_; }

    // Poll all PTYs, returns true if any data was read
    bool pollAllPtys();

    // Resize all panes
    void resizeAllPanes(int rows, int cols);

private:
    std::unique_ptr<Mux> mux_;
    WorkspaceId wsId_;
    PtyFactory ptyFactory_;

    std::unordered_map<PaneId, std::unique_ptr<PaneState>> panes_;
    PaneId nextPaneId_ = 1;

    // Active pane cache
    Screen* activeScreen_ = nullptr;
    Pty* activePty_ = nullptr;

    Config config_;
};

} // namespace termcore
#endif
```

- [ ] **Step 2: Implement TabController**

Port from TerminalWindowState: pane management, tab operations, pollAllPtys, dead pane cleanup.

- [ ] **Step 3: Commit**

### Task 6: Create ConfigApplier

**Files:**
- Create: `core/include/termcore/config_applier.h`
- Create: `core/src/config_applier.cpp`

- [ ] **Step 1: Write ConfigApplier**

Applies config changes atomically across all panes and persists to Lua.

```cpp
// core/include/termcore/config_applier.h
#ifndef TERMCORE_CONFIG_APPLIER_H
#define TERMCORE_CONFIG_APPLIER_H

#include "termcore/config.h"
#include "termcore/platform_host.h"

namespace termcore {

class TabController;
class FontManager;

class ConfigApplier {
public:
    // Full config update (from settings UI)
    void applyFull(Config& config, const Config& newConfig,
                   TabController& tabs, FontManager& fontMgr,
                   IPlatformHost* host);

    // Color-only update (from theme hub)
    void applyColors(Config& config, const Config& newConfig,
                     TabController& tabs, IPlatformHost* host);

    // Font-only update (from font hub)
    void applyFont(Config& config, const std::string& family,
                   TabController& tabs, FontManager& fontMgr,
                   IPlatformHost* host);

    // Persist current config to disk
    void persist(const Config& config);
};

} // namespace termcore
#endif
```

- [ ] **Step 2: Implement ConfigApplier**
- [ ] **Step 3: Commit**

---

## Chunk 2: TerminalController Facade

### Task 7: Create TerminalController

**Files:**
- Create: `core/include/termcore/terminal_controller.h`
- Create: `core/src/terminal_controller.cpp`
- Modify: `core/CMakeLists.txt`

- [ ] **Step 1: Write TerminalController header**

Single entry point for all terminal logic. Platform feeds events, controller dispatches to managers.

Key methods:
- `onKeyEvent(KeyEvent)` — keybinding lookup → action dispatch
- `onMouseEvent(MouseEvent)` — selection, scroll, mouse protocol
- `onResize(int w, int h)` — grid recalc, pane resize
- `onSearchQuery/Next/Prev` — search delegation
- `onConfigChanged/onThemeChanged/onFontChanged` — config application
- `tick()` — cursor blink, resize overlay timeout
- `pollPty()` — read all PTYs
- Accessors for renderer: `activeScreen()`, `config()`, `selection()`, `cellWidth()`, etc.

- [ ] **Step 2: Implement TerminalController**

The `handleAction(Action)` method is the core — port the full action switch from Windows (25+ cases) and add macOS-only actions (JumpPromptUp/Down, EnterCopyMode, etc.).

`onKeyEvent` flow:
1. If search active + Escape → close search
2. If copy mode active → delegate to vi copy mode
3. Lookup keybinding → if action found, handleAction
4. Otherwise: send text/key sequence to PTY

`onMouseEvent` flow:
1. Check if mouse mode active → encode and send to PTY
2. Otherwise: delegate to SelectionManager for selection tracking
3. ScrollUp/Down: viewport scroll or mouse protocol

- [ ] **Step 3: Add all new source files to CMakeLists.txt**

```cmake
list(APPEND TERMCORE_SOURCES
    src/terminal_controller.cpp
    src/font_manager.cpp
    src/selection_manager.cpp
    src/search_controller.cpp
    src/tab_controller.cpp
    src/config_applier.cpp
)
```

- [ ] **Step 4: Build core to verify compilation**

```bash
cmake --build build --config Release --target termcore
```

- [ ] **Step 5: Commit**

```bash
git commit -m "feat: add TerminalController with all managers"
```

---

## Chunk 3: Rewire Windows Platform

### Task 8: Rewrite Windows TerminalWindowState

**Files:**
- Modify: `platform/windows/include/TerminalWindowState.h`
- Modify: `platform/windows/src/TerminalWindowState.cpp`

- [ ] **Step 1: Slim TerminalWindowState to platform-only**

Remove all core state (panes, mux, keybindings, search, selection, config, cell dimensions, etc.). Add `TerminalController` as the single core entry point. Implement `IPlatformHost`.

State that remains:
- D3D objects (device, swapChain, rtv, renderer)
- Font rasterization stack (rasterizer, discovery, shaper, fontCollection, atlas, cache)
- Window handle, fullscreen state, DPI
- UI windows (themeHub, fontHub, settingsWin)
- Resize overlay (visual-only)

- [ ] **Step 2: Rewrite TerminalWindowState.cpp**

`initD3D` — unchanged.
`initTerminal` — create TerminalController, pass `this` as IPlatformHost.
`pollPty` — delegate to `controller->pollPty()`.
`renderFrame` — read state from `controller->activeScreen()`, `controller->selection()`, etc.
`resizeSwapChain` — D3D resize + `controller->onResize(w, h)`.
IPlatformHost methods — thin wrappers around WinAPI.

- [ ] **Step 3: Build and verify**
- [ ] **Step 4: Commit**

### Task 9: Rewrite Windows TerminalWindowInput

**Files:**
- Modify: `platform/windows/src/TerminalWindowInput.cpp`

- [ ] **Step 1: Replace action dispatch with event translation**

Remove the entire action switch. Convert WM_KEYDOWN to `KeyEvent`, call `controller->onKeyEvent(e)`. Convert mouse messages to `MouseEvent`, call `controller->onMouseEvent(e)`.

Keep only:
- Win32 keycode → termcore keycode mapping
- Win32 modifier extraction
- IME message forwarding

- [ ] **Step 2: Build and verify**
- [ ] **Step 3: Commit**

### Task 10: Rewrite Windows TerminalWindow (message loop)

**Files:**
- Modify: `platform/windows/src/TerminalWindow.cpp`

- [ ] **Step 1: Simplify message loop**

Tab bar click handling logic moves to controller (via MouseEvent with coordinates). WM_TIMER calls `controller->tick()`. Search field events delegate to controller.

- [ ] **Step 2: Build full Windows target**

```bash
cmake --build build --config Release --target BreadTerminal
```

- [ ] **Step 3: Commit**

```bash
git commit -m "refactor: rewire Windows platform to use TerminalController"
```

---

## Chunk 4: Rewire macOS Platform

### Task 11: Rewrite macOS TerminalView + Input

**Files:**
- Modify: `platform/macos/src/TerminalViewInput.mm`
- Modify: `platform/macos/src/TerminalView.mm`
- Modify: `platform/macos/src/AppDelegate.mm`

- [ ] **Step 1: Create macOS IPlatformHost implementation**

In AppDelegate or a new MacPlatformHost class. Implement all IPlatformHost methods using Cocoa APIs.

- [ ] **Step 2: Slim TerminalViewInput.mm**

Remove handleAction switch, font size logic, search logic, selection logic. Keep:
- NSEvent → KeyEvent translation (keycodeFromEvent, modsFromEvent)
- NSTextInputClient protocol (IME)
- NSEvent → MouseEvent translation

- [ ] **Step 3: Slim TerminalView.mm**

Remove copy mode state, selection state, search UI state. Rendering reads from controller accessors.

- [ ] **Step 4: Update AppDelegate.mm**

Create TerminalController in applicationDidFinishLaunching. Pass MacPlatformHost. Remove duplicated config apply callbacks.

- [ ] **Step 5: Commit**

```bash
git commit -m "refactor: rewire macOS platform to use TerminalController"
```

---

## Chunk 5: Rewire Linux Platform

### Task 12: Rewrite Linux TerminalWidget + Input

**Files:**
- Modify: `platform/linux/src/TerminalInput.cpp`
- Modify: `platform/linux/src/TerminalWidget.cpp`
- Modify: `platform/linux/src/main.cpp`

- [ ] **Step 1: Create Linux IPlatformHost implementation**

GTK4-based host. Clipboard via GdkClipboard, window via GtkWindow.

- [ ] **Step 2: Slim TerminalInput.cpp**

Remove action dispatch switch. Keep GDK → KeyEvent/MouseEvent translation.

- [ ] **Step 3: Slim TerminalWidget.cpp**

Create TerminalController in widget init. Delegate all logic.

- [ ] **Step 4: Linux gains all features**

Since TerminalController has full feature set (tabs, copy mode, prompt navigation, etc.), Linux gets all features automatically.

- [ ] **Step 5: Commit**

```bash
git commit -m "refactor: rewire Linux platform to use TerminalController"
```

---

## Chunk 6: Cleanup & Verification

### Task 13: Remove dead platform code

- [ ] **Step 1: Delete duplicated code**

Remove any leftover functions in platform layers that are now in core (changeFontSize, resetFontSize, handleAction switch remnants, selection helpers, search helpers).

- [ ] **Step 2: Verify line counts**

Platform files should be significantly smaller:
- Windows TerminalWindowInput.cpp: 476 → ~100 lines
- Windows TerminalWindowState.cpp: 589 → ~200 lines
- macOS TerminalViewInput.mm: 1273 → ~200 lines
- Linux TerminalInput.cpp: 357 → ~80 lines

- [ ] **Step 3: Build all targets**

```bash
cmake --build build --config Release --target BreadTerminal  # Windows
# macOS and Linux: verify in CI or on respective machines
```

- [ ] **Step 4: Run tests**

```bash
cmake --build build --config Release --target termcore_tests
./build/tests/Release/termcore_tests
```

- [ ] **Step 5: Final commit**

```bash
git commit -m "chore: remove dead platform code after core unification"
```
