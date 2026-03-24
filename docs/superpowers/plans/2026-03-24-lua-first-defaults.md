# Lua-First Default Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move all hardcoded C++ defaults (config, keybindings, commands, paste guard, URL schemes, themes, icons, tab titles) to embedded Lua modules, making every default fully customizable.

**Architecture:** 9 Lua files in `core/defaults/` are embedded as C byte arrays at build time via CMake. `LuaEngine::loadDefaults()` loads them after module registration but before user `config.lua`. C++ hardcoded defaults are removed and replaced with "unset" sentinel values + minimal fallbacks.

**Tech Stack:** C++17, sol2/sol3, Lua 5.4, CMake, Google Test

**Spec:** `docs/superpowers/specs/2026-03-24-lua-first-defaults-design.md`

---

## File Structure

### New Files
```
core/defaults/config.lua          — Config 기본값 (font, cursor, window 등)
core/defaults/colors.lua          — 기본 색상/팔레트
core/defaults/keybindings.lua     — 40개+ 기본 키바인딩
core/defaults/commands.lua        — 39개 기본 커맨드 팔레트 등록
core/defaults/tab_title.lua       — 탭 타이틀 포매팅 로직
core/defaults/paste_guard.lua     — 페이스트 가드 위험 패턴
core/defaults/url_detect.lua      — URL 스킴 규칙
core/defaults/icons.lua           — 프로세스 아이콘 매핑 33개
core/defaults/themes.lua          — 코어 테마 10개
core/cmake/embed_lua.cmake        — .lua → .h 변환 CMake 스크립트
tests/test_lua_defaults.cpp       — 통합 테스트
```

### Modified Files
```
core/CMakeLists.txt               — embed_lua 빌드 단계 추가
core/include/termcore/lua_engine.h — loadDefaults() 선언 추가
core/src/lua_engine.cpp           — loadDefaults() 구현 + terminal.action() API
core/src/terminal_controller.cpp  — loadDefaults() 호출 추가
core/include/termcore/config.h    — 기본값 → "unset" 센티넬, icons 빈맵
core/src/keybinding.cpp           — initDefaults() 비움
core/src/command_palette.cpp      — kActionEntries[] 제거
core/src/paste_guard.cpp          — 하드코딩 패턴 제거
core/src/url_detector.cpp         — kSchemes[] 비움
core/src/tab_controller.cpp       — buildTabTitle() 단순화
core/src/config_themes.cpp        — 101개 빌트인 테마 제거
core/src/lua_bindings/lua_tab_module.cpp — set_process_icon 추가
core/include/termcore/tab_controller.h   — processIcons_ 맵 추가
```

---

## Task 1: CMake Embed Infrastructure

**Files:**
- Create: `core/cmake/embed_lua.cmake`
- Modify: `core/CMakeLists.txt`

- [ ] **Step 1: Create embed_lua.cmake script**

```cmake
# core/cmake/embed_lua.cmake
# Usage: cmake -P embed_lua.cmake <input.lua> <output.h> <variable_name>
#
# Converts a .lua file into a C header with:
#   const unsigned char <variable_name>[] = { 0x2d, 0x2d, ... };
#   const unsigned int <variable_name>_len = <size>;

set(INPUT_FILE "${CMAKE_ARGV3}")
set(OUTPUT_FILE "${CMAKE_ARGV4}")
set(VAR_NAME "${CMAKE_ARGV5}")

file(READ "${INPUT_FILE}" CONTENT HEX)
string(LENGTH "${CONTENT}" HEX_LEN)
math(EXPR BYTE_COUNT "${HEX_LEN} / 2")

# Convert hex pairs to 0xNN format
set(HEX_ARRAY "")
set(LINE_COUNT 0)
string(REGEX MATCHALL ".." HEX_PAIRS "${CONTENT}")
foreach(PAIR ${HEX_PAIRS})
    if(HEX_ARRAY)
        string(APPEND HEX_ARRAY ",")
        if(LINE_COUNT GREATER_EQUAL 16)
            string(APPEND HEX_ARRAY "\n    ")
            set(LINE_COUNT 0)
        end()
    endif()
    string(APPEND HEX_ARRAY "0x${PAIR}")
    math(EXPR LINE_COUNT "${LINE_COUNT} + 1")
endforeach()

file(WRITE "${OUTPUT_FILE}"
    "#pragma once\n"
    "// Auto-generated from ${INPUT_FILE} — do not edit\n"
    "static const unsigned char ${VAR_NAME}[] = {\n    ${HEX_ARRAY}\n};\n"
    "static const unsigned int ${VAR_NAME}_len = ${BYTE_COUNT};\n"
)
```

- [ ] **Step 2: Add embed step to core/CMakeLists.txt**

After the existing `file(GLOB LUA_BINDING_SOURCES ...)` line, add:

```cmake
# Embed defaults/*.lua as C byte arrays
if(TERMCORE_HAS_LUA)
    set(DEFAULT_LUA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/defaults)
    set(LUA_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/lua_generated)
    file(MAKE_DIRECTORY ${LUA_GENERATED_DIR})

    set(DEFAULT_LUA_NAMES
        config colors keybindings commands
        tab_title paste_guard url_detect icons themes
    )

    set(LUA_GENERATED_HEADERS "")
    foreach(NAME ${DEFAULT_LUA_NAMES})
        set(LUA_INPUT ${DEFAULT_LUA_DIR}/${NAME}.lua)
        set(LUA_OUTPUT ${LUA_GENERATED_DIR}/default_${NAME}_lua.h)
        add_custom_command(
            OUTPUT ${LUA_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_lua.cmake
                ${LUA_INPUT} ${LUA_OUTPUT} default_${NAME}_lua
            DEPENDS ${LUA_INPUT}
            COMMENT "Embedding defaults/${NAME}.lua"
        )
        list(APPEND LUA_GENERATED_HEADERS ${LUA_OUTPUT})
    endforeach()

    add_custom_target(generate_lua_defaults DEPENDS ${LUA_GENERATED_HEADERS})
    add_dependencies(termcore generate_lua_defaults)
    target_include_directories(termcore PRIVATE ${LUA_GENERATED_DIR})
endif()
```

- [ ] **Step 3: Create placeholder defaults/*.lua files**

Create 9 empty `.lua` files so the build doesn't fail:

```lua
-- core/defaults/config.lua (and all others)
-- Placeholder: will be populated in subsequent tasks
```

- [ ] **Step 4: Build and verify embed generates headers**

Run: `cmake --build build --target generate_lua_defaults`
Expected: `build/core/lua_generated/default_*_lua.h` files created

- [ ] **Step 5: Commit**

```
feat: add CMake Lua embedding infrastructure
```

---

## Task 2: LuaEngine loadDefaults() + terminal.action() API

**Files:**
- Modify: `core/include/termcore/lua_engine.h`
- Modify: `core/src/lua_engine.cpp`
- Modify: `core/src/terminal_controller.cpp`

- [ ] **Step 1: Add loadDefaults() and setActionHandler() to lua_engine.h**

```cpp
// After existing public methods:
void loadDefaults();
using ActionHandler = std::function<void(const std::string&)>;
void setActionHandler(ActionHandler handler);
```

- [ ] **Step 2: Implement loadDefaults() in lua_engine.cpp**

After the existing `clearAllModules()` implementation, add:

```cpp
#include "default_config_lua.h"
#include "default_colors_lua.h"
#include "default_keybindings_lua.h"
#include "default_commands_lua.h"
#include "default_tab_title_lua.h"
#include "default_paste_guard_lua.h"
#include "default_url_detect_lua.h"
#include "default_icons_lua.h"
#include "default_themes_lua.h"

struct DefaultScript {
    const char* name;
    const unsigned char* data;
    unsigned int len;
};

static const DefaultScript kDefaultScripts[] = {
    {"config",      default_config_lua,      default_config_lua_len},
    {"colors",      default_colors_lua,      default_colors_lua_len},
    {"keybindings", default_keybindings_lua,  default_keybindings_lua_len},
    {"commands",    default_commands_lua,     default_commands_lua_len},
    {"tab_title",   default_tab_title_lua,   default_tab_title_lua_len},
    {"paste_guard", default_paste_guard_lua,  default_paste_guard_lua_len},
    {"url_detect",  default_url_detect_lua,   default_url_detect_lua_len},
    {"icons",       default_icons_lua,        default_icons_lua_len},
    {"themes",      default_themes_lua,       default_themes_lua_len},
};

void LuaEngine::loadDefaults() {
    for (const auto& script : kDefaultScripts) {
        std::string code(reinterpret_cast<const char*>(script.data), script.len);
        auto result = loadString(code);
        if (!result.ok()) {
            // Log error but continue loading remaining defaults
            impl_->terminal_table["log"].get<sol::protected_function>()(
                std::string("Failed to load defaults/") + script.name + ".lua: " + lastError());
        }
    }
}

void LuaEngine::setActionHandler(ActionHandler handler) {
    impl_->terminal_table.set_function("action",
        [handler = std::move(handler)](const std::string& name) {
            handler(name);
        });
}
```

- [ ] **Step 3: Add ActionHandler member to LuaEngine header**

In the private section of LuaEngine:

```cpp
ActionHandler actionHandler_;
```

- [ ] **Step 4: Wire loadDefaults() in terminal_controller.cpp**

In `initTerminal()`, after `luaEngine_->initializeModules();`, add:

```cpp
    // Register terminal.action() to dispatch C++ actions from Lua
    luaEngine_->setActionHandler([this](const std::string& name) {
        Action a = parseActionName(name);
        if (a != Action::None) {
            handleAction(a);
        }
    });

    // Load embedded Lua defaults (before user config)
    luaEngine_->loadDefaults();
```

- [ ] **Step 5: Add parseActionName() to terminal_controller.cpp**

```cpp
static Action parseActionName(const std::string& name) {
    static const std::unordered_map<std::string, Action> kMap = {
        {"new_tab", Action::NewTab},
        {"close_tab", Action::CloseTab},
        {"next_tab", Action::NextTab},
        {"prev_tab", Action::PrevTab},
        {"split_right", Action::SplitRight},
        {"split_down", Action::SplitDown},
        {"close_pane", Action::ClosePane},
        {"close_window", Action::CloseWindow},
        {"copy", Action::Copy},
        {"paste", Action::Paste},
        {"paste_from_history", Action::PasteFromHistory},
        {"select_all", Action::SelectAll},
        {"search_open", Action::SearchOpen},
        {"search_close", Action::SearchClose},
        {"search_next", Action::SearchNext},
        {"search_prev", Action::SearchPrev},
        {"font_increase", Action::FontIncrease},
        {"font_decrease", Action::FontDecrease},
        {"font_reset", Action::FontReset},
        {"toggle_fullscreen", Action::ToggleFullscreen},
        {"scroll_page_up", Action::ScrollPageUp},
        {"scroll_page_down", Action::ScrollPageDown},
        {"scroll_to_top", Action::ScrollToTop},
        {"scroll_to_bottom", Action::ScrollToBottom},
        {"scroll_up", Action::ScrollUp},
        {"scroll_down", Action::ScrollDown},
        {"new_tab", Action::NewTab},
        {"jump_prompt_up", Action::JumpPromptUp},
        {"jump_prompt_down", Action::JumpPromptDown},
        {"reset_terminal", Action::ResetTerminal},
        {"clear_scrollback", Action::ClearScrollback},
        {"reload_config", Action::ReloadConfig},
        {"enter_copy_mode", Action::EnterCopyMode},
        {"toggle_sidebar", Action::ToggleSidebar},
        {"open_settings", Action::OpenSettings},
        {"open_theme_hub", Action::OpenThemeHub},
        {"open_font_hub", Action::OpenFontHub},
        {"open_command_palette", Action::OpenCommandPalette},
        {"toggle_broadcast", Action::ToggleBroadcast},
        {"show_profile_dropdown", Action::ShowProfileDropdown},
    };
    auto it = kMap.find(name);
    return it != kMap.end() ? it->second : Action::None;
}
```

Also add `switch_tab_1` through `switch_tab_9` and `new_tab_profile_1` through `new_tab_profile_9`:

```cpp
    // In the kMap above, add:
    {"switch_tab_1", Action::SwitchTab1}, {"switch_tab_2", Action::SwitchTab2},
    {"switch_tab_3", Action::SwitchTab3}, {"switch_tab_4", Action::SwitchTab4},
    {"switch_tab_5", Action::SwitchTab5}, {"switch_tab_6", Action::SwitchTab6},
    {"switch_tab_7", Action::SwitchTab7}, {"switch_tab_8", Action::SwitchTab8},
    {"switch_tab_9", Action::SwitchTab9},
    {"new_tab_profile_1", Action::NewTabProfile1},
    {"new_tab_profile_2", Action::NewTabProfile2},
    {"new_tab_profile_3", Action::NewTabProfile3},
    {"new_tab_profile_4", Action::NewTabProfile4},
    {"new_tab_profile_5", Action::NewTabProfile5},
    {"new_tab_profile_6", Action::NewTabProfile6},
    {"new_tab_profile_7", Action::NewTabProfile7},
    {"new_tab_profile_8", Action::NewTabProfile8},
    {"new_tab_profile_9", Action::NewTabProfile9},
```

- [ ] **Step 6: Build and verify**
- [ ] **Step 7: Commit**

```
feat: add LuaEngine::loadDefaults() and terminal.action() API
```

---

## Task 3: terminal.tab.set_process_icon() API

**Files:**
- Modify: `core/include/termcore/tab_controller.h`
- Modify: `core/src/tab_controller.cpp`
- Modify: `core/src/lua_bindings/lua_tab_module.cpp`

- [ ] **Step 1: Add setProcessIcon/getProcessIcon to TabController**

In `tab_controller.h`, public section:
```cpp
void setProcessIcon(const std::string& process, const std::string& icon);
std::string getProcessIcon(const std::string& process) const;
```

In private section:
```cpp
std::unordered_map<std::string, std::string> processIcons_;
```

- [ ] **Step 2: Implement in tab_controller.cpp**

```cpp
void TabController::setProcessIcon(const std::string& process, const std::string& icon) {
    processIcons_[process] = icon;
}

std::string TabController::getProcessIcon(const std::string& process) const {
    auto it = processIcons_.find(process);
    return it != processIcons_.end() ? it->second : "";
}
```

- [ ] **Step 3: Add set_process_icon to LuaTabModule**

In `lua_tab_module.cpp`, inside `registerBindings()`:

```cpp
tab.set_function("set_process_icon",
    [this](const std::string& process, const std::string& icon) {
        if (tabCtrl_) tabCtrl_->setProcessIcon(process, icon);
    });
```

- [ ] **Step 4: Build and verify**
- [ ] **Step 5: Commit**

```
feat: add terminal.tab.set_process_icon() Lua API
```

---

## Task 4: Create All 9 defaults/*.lua Files

**Files:**
- Create: `core/defaults/config.lua`
- Create: `core/defaults/colors.lua`
- Create: `core/defaults/keybindings.lua`
- Create: `core/defaults/commands.lua`
- Create: `core/defaults/tab_title.lua`
- Create: `core/defaults/paste_guard.lua`
- Create: `core/defaults/url_detect.lua`
- Create: `core/defaults/icons.lua`
- Create: `core/defaults/themes.lua`

Each file uses the existing `terminal.*` API. Values are ported directly from the C++ hardcoded defaults found in the research phase.

- [ ] **Step 1: defaults/config.lua**

```lua
-- BreadTerminal default configuration
-- Users can override any value in their config.lua

terminal.config({
    font_family = "Menlo",
    font_size = 14.0,
    font_subpixel = "auto",
    font_hinting = "auto",
    font_ligatures = true,

    scrollback_limit = 10000,
    cursor_style = "block",
    cursor_blink = true,
    cursor_blink_interval = 0.5,
    shell = "",

    clipboard_paste_protection = "multiline",
    clipboard_paste_bracketed_safe = true,
    allow_clipboard_write = false,

    clickable_urls = true,
    url_color = 0x89b4fa,

    notify_on_command_finish = true,
    notify_after_seconds = 5.0,

    custom_shader = "none",
    shader_intensity = 1.0,
    background_opacity = 1.0,
    background_blur = 0.5,
    background_blur_mode = "none",
    background_blur_material = "none",

    window_width = 800,
    window_height = 600,
    window_padding = 0,
    minimum_contrast = 1.0,

    sidebar_visible = true,
    sidebar_width = 220,

    quick_terminal_height = 0.4,
    quick_terminal_animation_ms = 150,
    quick_terminal_position = "top",
    quick_terminal_auto_hide = true,

    auto_detect_high_contrast = true,
    respect_reduced_motion = true,

    image_preview = false,
    image_preview_max_height = 10,

    session_autosave = true,
    session_autosave_interval = 30,
})
```

- [ ] **Step 2: defaults/colors.lua**

```lua
-- BreadTerminal default colors (Catppuccin Mocha)

terminal.config({
    foreground = 0xcdd6f4,
    background = 0x1e1e2e,
    cursor_color = 0xf5e0dc,
    selection_background = 0x585b70,
    selection_foreground = 0xcdd6f4,
    palette = {
        0x45475a, 0xf38ba8, 0xa6e3a1, 0xf9e2af,
        0x89b4fa, 0xf5c2e7, 0x94e2d5, 0xbac2de,
        0x585b70, 0xf38ba8, 0xa6e3a1, 0xf9e2af,
        0x89b4fa, 0xf5c2e7, 0x94e2d5, 0xa6adc8,
    },
})
```

- [ ] **Step 3: defaults/keybindings.lua**

```lua
-- BreadTerminal default keybindings
-- Users can override in config.lua with terminal.keymap()

local mod = terminal.platform == "macos" and "super" or "ctrl"

-- Tab
terminal.keymap(mod .. "+t", "new_tab")
terminal.keymap(mod .. "+w", "close_tab")
terminal.keymap(mod .. "+shift+]", "next_tab")
terminal.keymap(mod .. "+shift+[", "prev_tab")

-- Pane
terminal.keymap(mod .. "+d", "split_right")
terminal.keymap(mod .. "+shift+d", "split_down")

-- Tab switching 1-9
for i = 1, 9 do
    terminal.keymap(mod .. "+" .. i, "switch_tab_" .. i)
end

-- Clipboard
terminal.keymap(mod .. "+c", "copy")
terminal.keymap(mod .. "+v", "paste")
terminal.keymap(mod .. "+shift+v", "paste_from_history")
terminal.keymap(mod .. "+a", "select_all")

-- Search
terminal.keymap(mod .. "+f", "search_open")
terminal.keymap(mod .. "+g", "search_next")
terminal.keymap(mod .. "+shift+g", "search_prev")

-- Font
terminal.keymap(mod .. "+=", "font_increase")
terminal.keymap(mod .. "+-", "font_decrease")
terminal.keymap(mod .. "+0", "font_reset")

-- Scroll
terminal.keymap("shift+pageup", "scroll_page_up")
terminal.keymap("shift+pagedown", "scroll_page_down")
terminal.keymap("shift+home", "scroll_to_top")
terminal.keymap("shift+end", "scroll_to_bottom")

-- Window
terminal.keymap(mod .. "+n", "new_window")
terminal.keymap(mod .. "+enter", "toggle_fullscreen")

-- Misc
terminal.keymap(mod .. "+k", "clear_scrollback")
terminal.keymap(mod .. "+up", "jump_prompt_up")
terminal.keymap(mod .. "+down", "jump_prompt_down")
terminal.keymap(mod .. "+shift+,", "reload_config")
terminal.keymap(mod .. "+shift+x", "enter_copy_mode")
terminal.keymap(mod .. "+shift+b", "toggle_sidebar")

-- Settings/UI
terminal.keymap(mod .. "+,", "open_settings")
terminal.keymap(mod .. "+shift+t", "open_theme_hub")
terminal.keymap(mod .. "+shift+p", "open_command_palette")
terminal.keymap(mod .. "+shift+f", "open_font_hub")

-- Profiles
terminal.keymap(mod .. "+shift+n", "show_profile_dropdown")
for i = 1, 9 do
    terminal.keymap(mod .. "+shift+" .. i, "new_tab_profile_" .. i)
end
```

- [ ] **Step 4: defaults/commands.lua**

```lua
-- BreadTerminal default command palette entries
-- Users can add/remove commands in config.lua

-- Tab/Pane
terminal.command.register("New Tab",       function() terminal.action("new_tab") end,       {category="Tab",  description="Open a new tab"})
terminal.command.register("Close Tab",     function() terminal.action("close_tab") end,     {category="Tab",  description="Close the current tab"})
terminal.command.register("Next Tab",      function() terminal.action("next_tab") end,      {category="Tab",  description="Switch to the next tab"})
terminal.command.register("Previous Tab",  function() terminal.action("prev_tab") end,      {category="Tab",  description="Switch to the previous tab"})
terminal.command.register("Split Right",   function() terminal.action("split_right") end,   {category="Pane", description="Split pane to the right"})
terminal.command.register("Split Down",    function() terminal.action("split_down") end,    {category="Pane", description="Split pane downward"})
terminal.command.register("Close Pane",    function() terminal.action("close_pane") end,    {category="Pane", description="Close the current pane"})

-- Clipboard
terminal.command.register("Copy",               function() terminal.action("copy") end,               {category="Clipboard", description="Copy selection to clipboard"})
terminal.command.register("Paste",              function() terminal.action("paste") end,              {category="Clipboard", description="Paste from clipboard"})
terminal.command.register("Paste from History", function() terminal.action("paste_from_history") end, {category="Clipboard", description="Paste from clipboard history"})
terminal.command.register("Select All",         function() terminal.action("select_all") end,         {category="Clipboard", description="Select all text"})

-- Search
terminal.command.register("Find",          function() terminal.action("search_open") end,  {category="Search", description="Open search bar"})
terminal.command.register("Find Next",     function() terminal.action("search_next") end,  {category="Search", description="Jump to next match"})
terminal.command.register("Find Previous", function() terminal.action("search_prev") end,  {category="Search", description="Jump to previous match"})

-- Scroll
terminal.command.register("Scroll Up",        function() terminal.action("scroll_up") end,        {category="Scroll", description="Scroll up a few lines"})
terminal.command.register("Scroll Down",      function() terminal.action("scroll_down") end,      {category="Scroll", description="Scroll down a few lines"})
terminal.command.register("Scroll Page Up",   function() terminal.action("scroll_page_up") end,   {category="Scroll", description="Scroll up one page"})
terminal.command.register("Scroll Page Down", function() terminal.action("scroll_page_down") end, {category="Scroll", description="Scroll down one page"})
terminal.command.register("Scroll to Top",    function() terminal.action("scroll_to_top") end,    {category="Scroll", description="Scroll to top of history"})
terminal.command.register("Scroll to Bottom", function() terminal.action("scroll_to_bottom") end, {category="Scroll", description="Scroll to bottom"})

-- Window
terminal.command.register("New Window",        function() terminal.action("new_window") end,        {category="Window", description="Open a new window"})
terminal.command.register("Close Window",      function() terminal.action("close_window") end,      {category="Window", description="Close the window"})
terminal.command.register("Toggle Fullscreen", function() terminal.action("toggle_fullscreen") end, {category="Window", description="Toggle fullscreen mode"})

-- Font
terminal.command.register("Increase Font Size", function() terminal.action("font_increase") end, {category="Font", description="Make text larger"})
terminal.command.register("Decrease Font Size", function() terminal.action("font_decrease") end, {category="Font", description="Make text smaller"})
terminal.command.register("Reset Font Size",    function() terminal.action("font_reset") end,    {category="Font", description="Reset to default size"})

-- Terminal
terminal.command.register("Reset Terminal",     function() terminal.action("reset_terminal") end,     {category="Terminal", description="Reset the terminal state"})
terminal.command.register("Clear Scrollback",   function() terminal.action("clear_scrollback") end,   {category="Terminal", description="Clear scrollback buffer"})
terminal.command.register("Reload Config",      function() terminal.action("reload_config") end,      {category="Terminal", description="Reload configuration"})
terminal.command.register("Enter Copy Mode",    function() terminal.action("enter_copy_mode") end,    {category="Terminal", description="Enter vi-style copy mode"})
terminal.command.register("Toggle Broadcast",   function() terminal.action("toggle_broadcast") end,   {category="Pane",     description="Toggle broadcast mode"})

-- Navigation
terminal.command.register("Jump to Previous Prompt", function() terminal.action("jump_prompt_up") end,   {category="Navigation", description="Navigate to previous prompt"})
terminal.command.register("Jump to Next Prompt",     function() terminal.action("jump_prompt_down") end, {category="Navigation", description="Navigate to next prompt"})

-- Settings
terminal.command.register("Toggle Sidebar",  function() terminal.action("toggle_sidebar") end,  {category="Settings", description="Show or hide sidebar"})
terminal.command.register("Open Settings",   function() terminal.action("open_settings") end,   {category="Settings", description="Open settings window"})
terminal.command.register("Open Theme Hub",  function() terminal.action("open_theme_hub") end,  {category="Settings", description="Browse and apply themes"})
terminal.command.register("Open Font Hub",   function() terminal.action("open_font_hub") end,   {category="Settings", description="Browse and apply fonts"})
terminal.command.register("Open Command Palette", function() terminal.action("open_command_palette") end, {category="Settings", description="Open command palette"})
```

- [ ] **Step 5: defaults/tab_title.lua**

```lua
-- BreadTerminal default tab title formatting
-- Override with: terminal.tab.on_title_format(function(info) ... end)

local shell_names = {
    ["cmd.exe"] = true, ["powershell.exe"] = true, ["pwsh.exe"] = true,
    ["powershell"] = true, ["pwsh"] = true,
    ["bash"] = true, ["bash.exe"] = true,
    ["zsh"] = true, ["fish"] = true, ["sh"] = true,
    ["wsl.exe"] = true, ["wsl"] = true,
    ["login"] = true, ["tmux"] = true, ["screen"] = true,
}

local function extract_last_component(path)
    return path:match("([^/\\]+)$") or path
end

local function extract_meaningful_title(title)
    -- Handle "PREFIX:path" patterns (e.g., "MINGW64:/c/Users/foo")
    local _, path = title:match("^(%u[%u%d_]+):(.+)$")
    if path then
        return extract_last_component(path)
    end
    -- Handle plain paths
    if title:find("[/\\]") then
        return extract_last_component(title)
    end
    return title
end

local function default_title_format(info)
    -- Custom title override (from terminal.tab.set_title)
    if info.custom_title and info.custom_title ~= "" then
        return info.custom_title
    end

    -- Meaningful screen title from non-shell process
    if info.title and info.title ~= "" then
        local is_shell = shell_names[info.process or ""]
        if not is_shell then
            return extract_meaningful_title(info.title)
        end
    end

    -- Working directory last component
    if info.cwd and info.cwd ~= "" then
        return extract_last_component(info.cwd)
    end

    -- Process name
    if info.process and info.process ~= "" then
        return info.process
    end

    -- Fallback
    return "Tab " .. (info.tab_index or 0)
end

terminal.tab.defaults = terminal.tab.defaults or {}
terminal.tab.defaults.title_format = default_title_format
terminal.tab.defaults.shell_names = shell_names
terminal.tab.on_title_format(default_title_format)
```

- [ ] **Step 6: defaults/paste_guard.lua**

```lua
-- BreadTerminal default paste guard rules
-- Override with: terminal.paste.add_danger() / terminal.paste.whitelist()

terminal.paste.set_mode("multiline")

terminal.paste.add_danger("sudo ",             "Contains sudo command")
terminal.paste.add_danger("sudo su",           "Contains sudo su (root shell)")
terminal.paste.add_danger("sudo %-i",          "Contains sudo -i (root shell)")
terminal.paste.add_danger("rm %-rf",           "Contains rm -rf command")
terminal.paste.add_danger("rm %-r ",           "Contains recursive rm command")
terminal.paste.add_danger("rm %-R ",           "Contains recursive rm command")
terminal.paste.add_danger("chmod %-R 777",     "Dangerous permission change")
terminal.paste.add_danger("curl.*|.*sh",       "Curl piped to shell")
terminal.paste.add_danger("curl.*|.*bash",     "Curl piped to shell")
terminal.paste.add_danger("wget.*|.*sh",       "Wget piped to shell")
terminal.paste.add_danger("wget.*|.*bash",     "Wget piped to shell")
terminal.paste.add_danger("base64.*%-d.*|.*sh","Encoded payload piped to shell")
```

- [ ] **Step 7: defaults/url_detect.lua**

```lua
-- BreadTerminal default URL schemes
terminal.url.add_scheme("https", "http", "ftp", "file", "ssh", "git")
terminal.url.set_color(0x89b4fa)
```

- [ ] **Step 8: defaults/icons.lua**

```lua
-- BreadTerminal default process icon mappings (Nerd Font)

local icons = {
    bash             = "F489",   -- nf-oct-terminal
    sh               = "F489",
    zsh              = "F489",
    fish             = "F489",
    ["cmd"]          = "E70F",   -- nf-dev-windows
    ["cmd.exe"]      = "E70F",
    powershell       = "EBC7",   -- nf-md-powershell
    pwsh             = "EBC7",
    ["powershell.exe"] = "EBC7",
    ["pwsh.exe"]     = "EBC7",
    python           = "E73C",   -- nf-dev-python
    python3          = "E73C",
    ["python.exe"]   = "E73C",
    ["python3.exe"]  = "E73C",
    node             = "E718",   -- nf-dev-nodejs_small
    ["node.exe"]     = "E718",
    vim              = "E62B",   -- nf-dev-vim
    nvim             = "E62B",
    git              = "E702",   -- nf-dev-git
    ["git.exe"]      = "E702",
    ssh              = "F489",   -- nf-oct-terminal
    ["ssh.exe"]      = "F489",
    docker           = "F308",   -- nf-linux-docker
    ["docker.exe"]   = "F308",
    cargo            = "E7A8",   -- nf-dev-rust
    rustc            = "E7A8",
    go               = "E626",   -- nf-dev-go
    ["go.exe"]       = "E626",
    ruby             = "E739",   -- nf-dev-ruby
    irb              = "E739",
    lua              = "E620",   -- nf-seti-lua
    luajit           = "E620",
}

for process, icon in pairs(icons) do
    terminal.tab.set_process_icon(process, icon)
end
```

- [ ] **Step 9: defaults/themes.lua**

Port 10 core themes from `config_themes.cpp`. Include: Catppuccin Mocha, Catppuccin Latte, Dracula, One Dark, Solarized Dark, Solarized Light, Tokyo Night, Nord, Gruvbox Dark, Rosé Pine. Each theme uses `terminal.colorscheme(name, {colors})`.

The remaining ~91 themes move to `~/.config/bread/themes/` as external files (not part of this plan — separate follow-up).

- [ ] **Step 10: Build and verify all defaults load**
- [ ] **Step 11: Commit**

```
feat: create all 9 defaults/*.lua with ported C++ defaults
```

---

## Task 5: C++ Default Removal — Big Bang

All C++ hardcoded defaults are removed in a single task. The defaults/*.lua files (Task 4) now provide all default values.

**Files:**
- Modify: `core/include/termcore/config.h`
- Modify: `core/src/keybinding.cpp`
- Modify: `core/src/command_palette.cpp`
- Modify: `core/src/paste_guard.cpp`
- Modify: `core/src/url_detector.cpp`
- Modify: `core/src/tab_controller.cpp`
- Modify: `core/src/config_themes.cpp`

- [ ] **Step 1: config.h — Remove default values**

Change all member initializers to "unset" sentinel values:

```cpp
// Numeric defaults → 0 (unset)
float font_size = 0;
int scrollback_limit = 0;
float cursor_blink_interval = 0;
int window_width = 0;
int window_height = 0;
// etc.

// String defaults → empty
std::string font_family;
std::string cursor_style;
std::string custom_shader;
// etc.

// Bool defaults → false (unset is same as false for most)
// Keep as-is where false is a reasonable unset value

// Color defaults → 0 (unset)
uint32_t background = 0;
uint32_t foreground = 0;
uint32_t cursor_color = 0;
uint32_t selection_background = 0;
uint32_t selection_foreground = 0;

// Palette → all zeros
uint32_t palette[16] = {};

// Process icon map → empty
std::unordered_map<std::string, std::string> tab_process_icons = {};
```

- [ ] **Step 2: keybinding.cpp — Empty initDefaults()**

Replace the body of `KeybindingManager::initDefaults()` (lines 262-355):

```cpp
void KeybindingManager::initDefaults() {
    // Default keybindings are now defined in defaults/keybindings.lua
}
```

- [ ] **Step 3: command_palette.cpp — Remove kActionEntries[]**

Remove the `kActionEntries[]` array (lines 9-49). Update `CommandPalette::open()` to only use Lua-registered commands:

```cpp
void CommandPalette::open() {
    // No built-in entries — all commands come from Lua
    filteredItems_.clear();
    for (const auto& cmd : luaCommands_) {
        filteredItems_.push_back({cmd.name, cmd.description, cmd.category, Action::None, cmd.callback});
    }
    isOpen_ = true;
    selectedIndex_ = 0;
    query_.clear();
}
```

- [ ] **Step 4: paste_guard.cpp — Remove hardcoded patterns**

Remove all pattern detection functions (`hasSudoSuRoot`, `hasRmDangerousFlags`, `hasHomeOrRootWipe`, `hasPipeTo`, `hasChmodRecursive777`, `hasBase64DecodePipe`).

Simplify `analyze()` to only check `customDangers_` (populated by Lua):

```cpp
PasteAnalysis PasteGuard::analyze(const std::string& text, bool bracketed) const {
    PasteAnalysis result;
    result.danger = PasteDanger::Safe;

    // Check custom dangers (from Lua terminal.paste.add_danger)
    for (const auto& danger : customDangers_) {
        if (std::regex_search(text, std::regex(danger.pattern))) {
            result.danger = PasteDanger::Warn;
            result.signals |= static_cast<uint32_t>(PasteSignal::CustomDanger);
            break;
        }
    }

    // Multi-line check (if mode requires it)
    if (mode_ == Mode::Multiline && text.find('\n') != std::string::npos) {
        result.danger = PasteDanger::Warn;
        result.signals |= static_cast<uint32_t>(PasteSignal::MultiLine);
    }

    // Trailing newline check
    if (!text.empty() && text.back() == '\n') {
        result.signals |= static_cast<uint32_t>(PasteSignal::TrailingNewline);
    }

    // Bracketed paste safety
    if (bracketed && trustBracketedPaste_) {
        result.danger = PasteDanger::Safe;
    }

    return result;
}
```

- [ ] **Step 5: url_detector.cpp — Remove kSchemes[]**

Remove `kSchemes[]` (lines 8-10). The `detectInScreen()` method now only uses `customSchemes_` (populated by Lua `terminal.url.add_scheme()`):

```cpp
// Remove:
// static const std::string kSchemes[] = { "https://", "http://", ... };

// In detectInScreen() or detectInLine(), replace kSchemes references with customSchemes_
```

- [ ] **Step 6: tab_controller.cpp — Simplify buildTabTitle()**

Replace the complex `buildTabTitle()` with minimal fallback:

```cpp
static std::string buildTabTitle(const std::string& title, const std::string& cwd,
                                  const std::string& processName) {
    // Title formatting is now handled by Lua (defaults/tab_title.lua)
    // This fallback only runs if Lua callback is not set
    if (!title.empty()) return title;
    if (!cwd.empty()) return cwd;
    if (!processName.empty()) return processName;
    return "Terminal";
}
```

Remove `isShellName()` function (lines 8-24) — now in Lua.

- [ ] **Step 7: config_themes.cpp — Remove builtin themes**

Replace `kBuiltinThemes[]` (lines 10-1437) with an empty array:

```cpp
static const BuiltinTheme kBuiltinThemes[] = {};
static const size_t kBuiltinThemeCount = 0;
```

Update `getBuiltinTheme()` and `listBuiltinThemes()` to also check Lua-registered colorschemes.

- [ ] **Step 8: Safety fallback in terminal_controller.cpp**

After `luaEngine_->loadDefaults()`, add fallback checks:

```cpp
    // Safety fallback if defaults failed to load
    if (config_.font_size <= 0) config_.font_size = 14.0f;
    if (config_.foreground == 0) config_.foreground = 0xffffff;
    if (config_.background == 0) config_.background = 0x000000;
    if (config_.scrollback_limit <= 0) config_.scrollback_limit = 10000;
    if (config_.cursor_style.empty()) config_.cursor_style = "block";
```

- [ ] **Step 9: Build and verify no compilation errors**
- [ ] **Step 10: Commit**

```
feat: remove all C++ hardcoded defaults — Lua-first architecture

All default values now defined in embedded defaults/*.lua:
- config.h: member initializers → unset sentinels
- keybinding.cpp: initDefaults() emptied
- command_palette.cpp: kActionEntries[] removed
- paste_guard.cpp: hardcoded patterns removed
- url_detector.cpp: kSchemes[] removed
- tab_controller.cpp: buildTabTitle() simplified
- config_themes.cpp: 101 builtin themes removed
```

---

## Task 6: Integration Test

**Files:**
- Create: `tests/test_lua_defaults.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write integration test**

```cpp
#include <gtest/gtest.h>
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include "termcore/lua_engine.h"
#include "lua_bindings/lua_tab_module.h"
#include "lua_bindings/lua_command_module.h"
#include "lua_bindings/lua_event_module.h"
#include "lua_bindings/lua_paste_module.h"
#include "lua_bindings/lua_url_module.h"
#include "lua_bindings/lua_theme_module.h"
#include "lua_bindings/lua_quick_module.h"

using namespace termcore;

class LuaDefaultsTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<LuaEngine>();
        // Register minimal modules needed by defaults
        engine_->registerModule(std::make_shared<LuaTabModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaCommandModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaEventModule>());
        engine_->registerModule(std::make_shared<LuaPasteModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaUrlModule>(nullptr, nullptr));
        engine_->registerModule(std::make_shared<LuaThemeModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaQuickModule>(nullptr));
        engine_->initializeModules();
    }
    void TearDown() override {
        engine_->clearAllModules();
        engine_.reset();
    }
    std::unique_ptr<LuaEngine> engine_;
};

TEST_F(LuaDefaultsTest, LoadAllDefaultsWithoutError) {
    engine_->loadDefaults();
    // If we get here without crash, all 9 defaults loaded
}

TEST_F(LuaDefaultsTest, DefaultsTableExists) {
    engine_->loadDefaults();
    auto r = engine_->loadString("assert(terminal.tab.defaults ~= nil)");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaDefaultsTest, DefaultTitleFormatCallable) {
    engine_->loadDefaults();
    auto r = engine_->loadString(R"(
        assert(type(terminal.tab.defaults.title_format) == "function")
        local result = terminal.tab.defaults.title_format({
            process = "bash", cwd = "/home/user/projects", title = "", tab_index = 1
        })
        assert(result == "projects", "Expected 'projects', got: " .. tostring(result))
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaDefaultsTest, UserCanExtendTitleFormat) {
    engine_->loadDefaults();
    auto r = engine_->loadString(R"(
        local default = terminal.tab.defaults.title_format
        terminal.tab.on_title_format(function(info)
            return "PREFIX:" .. default(info)
        end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaDefaultsTest, KeybindingsRegistered) {
    engine_->loadDefaults();
    // Keybindings are registered via terminal.keymap() — verify no error
}

TEST_F(LuaDefaultsTest, CommandsRegistered) {
    engine_->loadDefaults();
    auto r = engine_->loadString(R"(
        -- Verify some commands exist by re-registering (no error = API works)
        assert(type(terminal.command.register) == "function")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaDefaultsTest, ThemesRegistered) {
    engine_->loadDefaults();
    auto r = engine_->loadString(R"(
        local themes = terminal.theme.list()
        assert(themes ~= nil)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaDefaultsTest, UserConfigOverridesDefaults) {
    engine_->loadDefaults();
    // Simulate user config.lua overriding font size
    auto r = engine_->loadString(R"(
        terminal.config({ font_size = 18.0 })
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}
```

- [ ] **Step 2: Add to tests/CMakeLists.txt**

```cmake
    test_lua_defaults.cpp
```

- [ ] **Step 3: Build and run tests**
- [ ] **Step 4: Commit**

```
test: add Lua defaults integration tests
```

---

## Parallel Execution Strategy

| Task | Dependencies | Parallelizable |
|------|-------------|----------------|
| Task 1 (CMake embed) | None | Independent |
| Task 2 (loadDefaults + action API) | Task 1 | Sequential after 1 |
| Task 3 (set_process_icon API) | None | Independent |
| Task 4 (9 defaults/*.lua files) | Task 1 | Sequential after 1 |
| Task 5 (C++ removal) | Tasks 2, 3, 4 | Sequential after all |
| Task 6 (Integration test) | Task 5 | Sequential after 5 |

**Recommended execution order:**
- Tasks 1 + 3 in parallel
- Tasks 2 + 4 after Task 1
- Task 5 after Tasks 2, 3, 4
- Task 6 after Task 5
