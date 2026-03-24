# Lua-First Default Implementation Design

## Overview

BreadTerminal의 모든 기본 동작(탭 타이틀, 키바인딩, 페이스트 가드, 커맨드 팔레트, URL 감지, 테마, 아이콘, Config 기본값)을 C++ 하드코딩에서 Lua로 이전한다. C++은 렌더링/PTY/VT 파서 엔진만 남기고, 모든 사용자 대면 동작은 Lua로 정의하여 완전한 커스터마이즈를 가능하게 한다.

## Design Decisions

- **범위**: Full Lua-first — C++의 모든 하드코딩된 기본값/로직을 Lua로 이전
- **배포**: 모듈별 분리 임베딩 — defaults/*.lua 파일을 빌드 시 C 바이트 배열로 변환하여 바이너리에 포함
- **오버라이드**: Extend 패턴 — `terminal.<module>.defaults` 테이블에 기본 함수를 보존하여 사용자가 감싸서 확장 가능
- **테마**: 코어 5~10개만 임베드 + `~/.config/bread/themes/*.lua` 외부 로딩
- **전환**: 빅뱅 — 모든 기본값을 한 번에 Lua로 이전, C++ 하드코딩 일괄 제거

## Loading Order

```
[앱 시작]
  → LuaEngine 생성
  → ILuaModule 바인딩 등록 (terminal.tab.*, terminal.paste.* 등 C++ API)
  → initializeModules()
  → loadDefaults() — 임베드된 defaults/*.lua 순서대로 로드:
      1. defaults/config.lua
      2. defaults/colors.lua
      3. defaults/keybindings.lua
      4. defaults/commands.lua
      5. defaults/tab_title.lua
      6. defaults/paste_guard.lua
      7. defaults/url_detect.lua
      8. defaults/icons.lua
      9. defaults/themes.lua
  → loadExternalThemes() — ~/.config/bread/themes/*.lua
  → 사용자 config.lua 로드 (오버라이드)
```

## Extend Mechanism

defaults/*.lua가 등록하는 기본 함수는 `terminal.<module>.defaults` 테이블에 보존된다.

```lua
-- defaults/tab_title.lua 내부
local function default_title_format(info)
    local shell_names = {"cmd.exe", "powershell", "bash", "zsh", "fish", "sh"}
    for _, name in ipairs(shell_names) do
        if info.process == name then
            return info.cwd:match("([^/\\]+)$") or info.cwd
        end
    end
    return info.title ~= "" and info.title or ("Tab " .. info.tab_index)
end

terminal.tab.defaults = terminal.tab.defaults or {}
terminal.tab.defaults.title_format = default_title_format
terminal.tab.on_title_format(default_title_format)
```

사용자 config.lua에서:
```lua
-- 기본 함수를 감싸서 확장
local default = terminal.tab.defaults.title_format
terminal.tab.on_title_format(function(info)
    return "★ " .. default(info)
end)
```

## Module Specifications

### defaults/config.lua — Config 기본값

현재 `config.h`의 모든 기본값을 `terminal.config()` 호출로 정의:

```lua
terminal.config({
    font_family = "Menlo",
    font_size = 14.0,
    scrollback_limit = 10000,
    cursor_style = "block",
    cursor_blink = true,
    cursor_blink_interval = 0.5,
    background_opacity = 1.0,
    background_blur = "none",
    window_width = 800,
    window_height = 600,
    window_padding = 0,
    minimum_contrast = 1.0,
    shell = "",
    paste_protection = "multiline",
    paste_bracket_trust = true,
    osc52_clipboard_write = false,
    clickable_urls = true,
    url_color = 0x89b4fa,
    notify_on_command_finish = true,
    notify_after_seconds = 5.0,
    custom_shader = "none",
    shader_intensity = 1.0,
    sidebar_visible = true,
    sidebar_width = 220,
    check_for_updates = true,
    update_check_interval = 24,
    font_ligatures = true,
    session_autosave = true,
    session_autosave_interval = 30,
})
```

### defaults/colors.lua — 기본 색상/팔레트

```lua
terminal.config({
    foreground = 0xcdd6f4,
    background = 0x1e1e2e,
    cursor_color = 0xf5e0dc,
    selection_bg = 0x585b70,
    selection_fg = 0xcdd6f4,
    palette = {
        0x45475a, 0xf38ba8, 0xa6e3a1, 0xf9e2af,
        0x89b4fa, 0xf5c2e7, 0x94e2d5, 0xbac2de,
        0x585b70, 0xf38ba8, 0xa6e3a1, 0xf9e2af,
        0x89b4fa, 0xf5c2e7, 0x94e2d5, 0xa6adc8,
    },
})
```

### defaults/keybindings.lua — 40개+ 기본 키바인딩

플랫폼 감지 후 `terminal.keymap()` 호출:

```lua
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
terminal.keymap(mod .. "+k", "clear_scrollback")
terminal.keymap(mod .. "+shift+,", "reload_config")

-- UI
terminal.keymap(mod .. "+shift+b", "toggle_sidebar")
terminal.keymap(mod .. "+,", "open_settings")
terminal.keymap(mod .. "+shift+t", "open_theme_hub")
terminal.keymap(mod .. "+shift+p", "open_command_palette")
terminal.keymap(mod .. "+shift+f", "open_font_hub")

-- Navigation
terminal.keymap(mod .. "+up", "jump_prompt_up")
terminal.keymap(mod .. "+down", "jump_prompt_down")

-- Profiles
terminal.keymap(mod .. "+shift+n", "show_profile_dropdown")
for i = 1, 9 do
    terminal.keymap(mod .. "+shift+" .. i, "new_tab_profile_" .. i)
end

-- Copy mode
terminal.keymap(mod .. "+shift+x", "enter_copy_mode")
```

### defaults/commands.lua — 커맨드 팔레트 기본 커맨드

48개 기본 커맨드를 `terminal.command.register()`로 등록. `terminal.action()` API 필요:

```lua
-- Tab/Pane
terminal.command.register("New Tab",       function() terminal.action("new_tab") end,       {category="Tab"})
terminal.command.register("Close Tab",     function() terminal.action("close_tab") end,     {category="Tab"})
terminal.command.register("Next Tab",      function() terminal.action("next_tab") end,      {category="Tab"})
terminal.command.register("Previous Tab",  function() terminal.action("prev_tab") end,      {category="Tab"})
terminal.command.register("Split Right",   function() terminal.action("split_right") end,   {category="Pane"})
terminal.command.register("Split Down",    function() terminal.action("split_down") end,    {category="Pane"})
terminal.command.register("Close Pane",    function() terminal.action("close_pane") end,    {category="Pane"})

-- Clipboard
terminal.command.register("Copy",               function() terminal.action("copy") end,               {category="Clipboard"})
terminal.command.register("Paste",              function() terminal.action("paste") end,              {category="Clipboard"})
terminal.command.register("Paste from History", function() terminal.action("paste_from_history") end, {category="Clipboard"})
terminal.command.register("Select All",         function() terminal.action("select_all") end,         {category="Clipboard"})

-- Search
terminal.command.register("Find",          function() terminal.action("search_open") end,  {category="Search"})
terminal.command.register("Find Next",     function() terminal.action("search_next") end,  {category="Search"})
terminal.command.register("Find Previous", function() terminal.action("search_prev") end,  {category="Search"})

-- Scroll
terminal.command.register("Scroll Page Up",   function() terminal.action("scroll_page_up") end,   {category="Scroll"})
terminal.command.register("Scroll Page Down", function() terminal.action("scroll_page_down") end, {category="Scroll"})
terminal.command.register("Scroll to Top",    function() terminal.action("scroll_to_top") end,    {category="Scroll"})
terminal.command.register("Scroll to Bottom", function() terminal.action("scroll_to_bottom") end, {category="Scroll"})

-- Font
terminal.command.register("Increase Font Size", function() terminal.action("font_increase") end, {category="Font"})
terminal.command.register("Decrease Font Size", function() terminal.action("font_decrease") end, {category="Font"})
terminal.command.register("Reset Font Size",    function() terminal.action("font_reset") end,    {category="Font"})

-- Window
terminal.command.register("Toggle Fullscreen", function() terminal.action("toggle_fullscreen") end, {category="Window"})
terminal.command.register("Close Window",      function() terminal.action("close_window") end,      {category="Window"})

-- Terminal
terminal.command.register("Reset Terminal",     function() terminal.action("reset_terminal") end,     {category="Terminal"})
terminal.command.register("Clear Scrollback",   function() terminal.action("clear_scrollback") end,   {category="Terminal"})
terminal.command.register("Reload Config",      function() terminal.action("reload_config") end,      {category="Terminal"})

-- Settings
terminal.command.register("Open Settings",  function() terminal.action("open_settings") end,  {category="Settings"})
terminal.command.register("Open Theme Hub", function() terminal.action("open_theme_hub") end, {category="Settings"})
terminal.command.register("Open Font Hub",  function() terminal.action("open_font_hub") end,  {category="Settings"})

-- Copy Mode
terminal.command.register("Enter Copy Mode", function() terminal.action("enter_copy_mode") end, {category="Terminal"})

-- Broadcast
terminal.command.register("Toggle Broadcast", function() terminal.action("toggle_broadcast") end, {category="Pane"})

-- Navigation
terminal.command.register("Jump to Previous Prompt", function() terminal.action("jump_prompt_up") end,   {category="Navigation"})
terminal.command.register("Jump to Next Prompt",     function() terminal.action("jump_prompt_down") end, {category="Navigation"})
```

### defaults/tab_title.lua — 탭 타이틀 포매팅

현재 `tab_controller.cpp`의 `buildTabTitle()` 로직을 Lua로 포팅:

```lua
local shell_names = {
    ["cmd.exe"] = true, ["powershell.exe"] = true, ["pwsh.exe"] = true,
    ["bash"] = true, ["bash.exe"] = true,
    ["zsh"] = true, ["fish"] = true, ["sh"] = true,
    ["wsl.exe"] = true, ["wsl"] = true,
}

local function extract_last_path_component(path)
    return path:match("([^/\\]+)$") or path
end

local function extract_meaningful_title(title)
    -- "PREFIX:path" 패턴 (예: "MINGW64:/c/Users/foo")
    local prefix, path = title:match("^(%u[%u%d_]+):(.+)$")
    if prefix and path then
        return extract_last_path_component(path)
    end
    return title
end

local function default_title_format(info)
    -- 1) 사용자 커스텀 타이틀이 있으면 사용
    if info.custom_title and info.custom_title ~= "" then
        return info.custom_title
    end

    -- 2) 의미 있는 screen title이 있고 셸이 아니면 사용
    if info.title and info.title ~= "" then
        local is_shell = shell_names[info.process or ""]
        if not is_shell then
            return extract_meaningful_title(info.title)
        end
    end

    -- 3) working directory의 마지막 컴포넌트
    if info.cwd and info.cwd ~= "" then
        return extract_last_path_component(info.cwd)
    end

    -- 4) 프로세스 이름
    if info.process and info.process ~= "" then
        return info.process
    end

    -- 5) 폴백
    return "Tab " .. (info.tab_index or 0)
end

terminal.tab.defaults = terminal.tab.defaults or {}
terminal.tab.defaults.title_format = default_title_format
terminal.tab.on_title_format(default_title_format)
```

### defaults/paste_guard.lua — 페이스트 가드 규칙

현재 `paste_guard.cpp`의 패턴을 Lua로 포팅:

```lua
terminal.paste.set_mode("multiline")

terminal.paste.add_danger("sudo ",           "Contains sudo command")
terminal.paste.add_danger("sudo su",         "Contains sudo su (root shell)")
terminal.paste.add_danger("sudo %-i",        "Contains sudo -i (root shell)")
terminal.paste.add_danger("rm %-rf",         "Contains rm -rf command")
terminal.paste.add_danger("rm %-r ",         "Contains recursive rm command")
terminal.paste.add_danger("chmod %-R 777",   "Dangerous permission change")
terminal.paste.add_danger("curl.*|.*sh",     "Curl piped to shell")
terminal.paste.add_danger("wget.*|.*sh",     "Wget piped to shell")
terminal.paste.add_danger("base64.*|.*sh",   "Encoded payload piped to shell")
terminal.paste.add_danger(":%(%)%{.*|",      "Fork bomb pattern")
```

### defaults/url_detect.lua — URL 스킴

```lua
terminal.url.add_scheme("https", "http", "ftp", "file", "ssh", "git")
terminal.url.set_color(0x89b4fa)
```

### defaults/icons.lua — 프로세스 아이콘 매핑

```lua
local icons = {
    bash       = "\u{e795}",  -- nf-dev-terminal
    sh         = "\u{e795}",
    zsh        = "\u{e795}",
    fish       = "\u{e795}",
    ["cmd.exe"]       = "\u{e70f}",  -- nf-dev-windows
    ["powershell.exe"]= "\u{ebc7}",  -- nf-md-powershell
    ["pwsh.exe"]      = "\u{ebc7}",
    python     = "\u{e73c}",  -- nf-dev-python
    python3    = "\u{e73c}",
    node       = "\u{e718}",  -- nf-dev-nodejs
    vim        = "\u{e62b}",  -- nf-dev-vim
    nvim       = "\u{e62b}",
    git        = "\u{e702}",  -- nf-dev-git
    ssh        = "\u{f489}",  -- nf-oct-terminal
    docker     = "\u{f308}",  -- nf-linux-docker
    cargo      = "\u{e7a8}",  -- nf-dev-rust
    go         = "\u{e626}",  -- nf-dev-go
    ruby       = "\u{e791}",  -- nf-dev-ruby
    lua        = "\u{e620}",  -- nf-seti-lua
}

for process, icon in pairs(icons) do
    terminal.tab.set_process_icon(process, icon)
end
```

### defaults/themes.lua — 코어 테마

```lua
terminal.colorscheme("Catppuccin Mocha", {
    foreground = 0xcdd6f4, background = 0x1e1e2e,
    cursor_color = 0xf5e0dc,
    selection_bg = 0x585b70, selection_fg = 0xcdd6f4,
    palette = { 0x45475a, 0xf38ba8, 0xa6e3a1, 0xf9e2af,
                0x89b4fa, 0xf5c2e7, 0x94e2d5, 0xbac2de,
                0x585b70, 0xf38ba8, 0xa6e3a1, 0xf9e2af,
                0x89b4fa, 0xf5c2e7, 0x94e2d5, 0xa6adc8 },
})
-- Catppuccin Latte, Dracula, One Dark, Solarized Dark, Tokyo Night 등 5~10개
```

## New C++ APIs Required

### terminal.action(name)

Lua에서 C++ Action enum을 문자열로 실행:

```cpp
// LuaEngine or new LuaActionModule
terminal.set_function("action", [controller](const std::string& name) {
    Action a = parseActionName(name);  // "new_tab" → Action::NewTab
    if (a != Action::None) {
        controller->handleAction(a);
    }
});
```

`parseActionName()`은 문자열 → Action enum 매핑. 기존 `command_palette.cpp`의 역매핑을 재활용.

### terminal.tab.set_process_icon(process, icon)

```cpp
// LuaTabModule에 추가
tab.set_function("set_process_icon", [this](std::string process, std::string icon) {
    tabCtrl_->setProcessIcon(process, icon);
});
```

`TabController`에 `std::unordered_map<std::string, std::string> processIcons_` 추가.

## C++ Removal Scope

### config.h — 기본값을 "unset" 상태로

```cpp
// Before
float font_size = 14.0f;
std::string cursor_style = "block";
uint32_t foreground = 0xcdd6f4;

// After
float font_size = 0;        // 0 = unset, defaults/config.lua가 설정
std::string cursor_style;    // empty = unset
uint32_t foreground = 0;     // 0 = unset
```

Config 적용 시 "unset" 값은 건너뛴다. defaults/config.lua가 항상 먼저 로드되므로 실제로 0인 경우는 없다.

### keybinding.cpp — loadDefaults() 비움

```cpp
void KeybindingManager::loadDefaults() {
    // Moved to defaults/keybindings.lua
}
```

### command_palette.cpp — kActionEntries[] 제거

`kActionEntries` 배열을 삭제하고, `open()` 시 Lua에서 등록된 커맨드만 표시.

### paste_guard.cpp — 기본 패턴 제거

`analyzeContent()` 내 하드코딩된 패턴 감지를 제거. `customDangers_` 벡터만 사용 (Lua가 채움).

### url_detector.cpp — kSchemes[] 비움

```cpp
// Before
static const char* kSchemes[] = {"https://", "http://", "ftp://", ...};
// After
// kSchemes removed — customSchemes_ only (populated by Lua)
```

### tab_controller.cpp — buildTabTitle() 단순화

Lua 콜백이 항상 설정되어 있으므로 C++ fallback 로직은 최소화:

```cpp
std::string TabController::buildTabTitle(...) {
    if (titleFormatFn_) {
        auto result = titleFormatFn_(info);
        if (!result.empty()) return result;
    }
    return "Tab " + std::to_string(index);  // minimal fallback
}
```

### config_themes.cpp — 빌트인 테마 제거

60개+ 테마 정의를 삭제. `terminal.colorscheme()` API를 통해 Lua에서만 등록.

### config.h — tab_process_icons 기본값 제거

하드코딩된 18개+ 매핑 제거. Lua `defaults/icons.lua`가 채움.

## Embedding Mechanism

### CMake — .lua → .h 변환

```cmake
# core/CMakeLists.txt에 추가
set(DEFAULT_LUA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/defaults)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})

set(DEFAULT_LUA_FILES
    config colors keybindings commands
    tab_title paste_guard url_detect icons themes
)

foreach(NAME ${DEFAULT_LUA_FILES})
    set(INPUT ${DEFAULT_LUA_DIR}/${NAME}.lua)
    set(OUTPUT ${GENERATED_DIR}/default_${NAME}_lua.h)
    add_custom_command(
        OUTPUT ${OUTPUT}
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_lua.cmake
            ${INPUT} ${OUTPUT} default_${NAME}_lua
        DEPENDS ${INPUT}
        COMMENT "Embedding defaults/${NAME}.lua"
    )
    list(APPEND GENERATED_HEADERS ${OUTPUT})
endforeach()

add_custom_target(generate_lua_defaults DEPENDS ${GENERATED_HEADERS})
add_dependencies(termcore generate_lua_defaults)
target_include_directories(termcore PRIVATE ${GENERATED_DIR})
```

### cmake/embed_lua.cmake — 변환 스크립트

입력 .lua 파일을 `const unsigned char name[] = {...}; const unsigned int name_len = N;` 형태의 C 헤더로 변환.

## External Theme Loading

```cpp
void LuaEngine::loadExternalThemes() {
    std::string themeDir = getConfigDir() + "/themes";
    for (auto& entry : fs::directory_iterator(themeDir)) {
        if (entry.path().extension() == ".lua") {
            loadPlugin(entry.path().string());
        }
    }
}
```

## Safety Fallback

defaults/*.lua 로드 실패 시:
- 에러를 `terminal.log()`로 기록
- C++ 최소 fallback 값 사용 (검은 화면 방지):
  - font_size가 0이면 14.0f 사용
  - foreground가 0이면 0xffffff 사용
  - background가 0이면 0x000000 사용
- 키바인딩이 비어있으면 Ctrl+Shift+P (커맨드 팔레트) 하나만 하드코딩
- 커맨드 팔레트가 비어있으면 "Reload Config" 하나만 하드코딩

## File Structure

```
core/
  defaults/
    config.lua
    colors.lua
    keybindings.lua
    commands.lua
    tab_title.lua
    paste_guard.lua
    url_detect.lua
    icons.lua
    themes.lua
  cmake/
    embed_lua.cmake
  generated/              (빌드 시 생성, .gitignore)
    default_config_lua.h
    ...
```
