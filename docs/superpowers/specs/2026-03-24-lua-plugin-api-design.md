# Lua Plugin API Design

## Overview

BreadTerminal의 핵심 기능들을 Lua 플러그인으로 커스터마이즈할 수 있도록 모듈형 바인딩 시스템을 구축한다. 현재 `lua_engine.cpp`에 집중된 설정 전용 API를 확장하여, 20개 서브시스템에 대한 Lua API를 제공한다.

## Design Decisions

- **모듈형 등록 패턴 (B안)**: 각 기능이 자신의 Lua 바인딩을 등록. `ILuaModule` 인터페이스 구현.
- **2단계 구현 (B안)**: 1단계 등록 인프라 → 2단계 20개 바인딩 병렬 구현.
- **혼합 네임스페이스 (C안)**: 기존 `terminal.config()` 등 유지, 새 기능은 `terminal.tab.*` 서브 테이블.

## Thread Safety & Execution Model

모든 Lua 코드는 **메인 스레드(UI 스레드)에서만** 실행된다.

- LuaEngine은 단일 `sol::state`를 소유하며, 메인 스레드에서만 접근한다.
- 백그라운드 스레드에서 발생하는 이벤트(git 감지, 알림 등)는 메인 스레드로 디스패치 후 Lua 콜백을 호출한다.
- 기존 `PlatformHost::postToMainThread()` 메커니즘을 활용한다.
- Lua 콜백 내에서 C++ API를 호출하는 것은 안전하다 (동일 스레드).

### 재귀 콜백 방지:

```cpp
// 각 모듈에서 콜백 호출 시 reentrancy guard 사용
bool inCallback_ = false;
if (!inCallback_) {
    inCallback_ = true;
    auto result = callback_(args);
    inCallback_ = false;
}
```

## Callback Lifecycle

### 소유권 모델

- `LuaEngine`이 `std::vector<std::shared_ptr<ILuaModule>>` 소유
- 각 모듈은 대응 C++ 컴포넌트의 **비소유 포인터** 보유
- `TerminalController`가 모든 C++ 컴포넌트와 `LuaEngine`을 소유하므로 수명 보장

### 콜백 저장

- 콜백은 `sol::protected_function`으로 각 모듈 내에 저장
- 플러그인 언로드 시 `ILuaModule::clearCallbacks()` 호출하여 모든 콜백 해제
- C++ 컴포넌트의 `std::function` 콜백도 함께 null로 초기화

```cpp
class ILuaModule {
public:
    virtual ~ILuaModule() = default;
    virtual std::string_view moduleName() const = 0;
    virtual void registerBindings(sol::state& lua, sol::table& terminal) = 0;
    virtual void clearCallbacks() = 0;  // 플러그인 언로드 시 호출
};
```

### 파괴 순서

```
TerminalController 소멸
  → LuaEngine 소멸 (모든 모듈의 clearCallbacks() 호출)
  → 모듈 shared_ptr 해제
  → sol::state 소멸 (모든 Lua 함수 무효화)
  → TabController, CommandPalette, ... 소멸
```

## Error Handling

### Lua → C++ 에러 전파

모든 Lua API 함수는 실패 시 `nil, error_message`를 반환한다:

```lua
local info, err = terminal.tab.get_info(999)
if not info then
    terminal.log("Error: " .. err)
end
```

### C++ → Lua 콜백 에러 처리

콜백 실행 실패 시 `sol::protected_function_result`를 검사하고, 에러를 로그에 기록한 후 기존 C++ 로직으로 fallback:

```cpp
auto result = callback_.call(args);
if (!result.valid()) {
    sol::error err = result;
    logError("Lua callback failed: {}", err.what());
    // fallback to C++ default behavior
}
```

### 표준 에러 패턴

```cpp
// 모든 모듈에서 사용하는 헬퍼 매크로/함수
inline auto luaError(sol::state& lua, const std::string& msg) {
    return std::make_tuple(sol::nil, msg);
}
```

## Capability Enforcement

모듈 등록 시 capability 검사를 수행한다. `registerBindings()`에 capability 컨텍스트를 전달:

```cpp
class ILuaModule {
public:
    virtual void registerBindings(sol::state& lua, sol::table& terminal) = 0;
    virtual PluginCapability requiredCapability() const = 0;
};

// LuaEngine::initialize() 내부
for (auto& mod : modules_) {
    if (isConfigContext || hasCapability(mod->requiredCapability())) {
        mod->registerBindings(lua, terminal);
    }
}
```

config.lua 컨텍스트에서는 모든 모듈이 활성화된다. 플러그인 컨텍스트에서는 선언된 capability에 따라 필터링된다.

## Architecture

### Registration Interface

```cpp
// core/include/termcore/lua_module.h
class ILuaModule {
public:
    virtual ~ILuaModule() = default;
    virtual std::string_view moduleName() const = 0;
    virtual void registerBindings(sol::state& lua, sol::table& terminal) = 0;
};
```

### LuaEngine Changes

- `registerModule(std::shared_ptr<ILuaModule>)` 메서드 추가
- 초기화 시 등록된 모든 모듈의 `registerBindings()` 호출
- 기존 `terminal.config()`, `terminal.keymap()`, `terminal.on()` 등은 LuaEngine 내부에 그대로 유지 (하위 호환)

### Module → C++ Component Connection

각 Lua 모듈은 대응 C++ 컴포넌트의 비소유 포인터를 보유:

```cpp
class LuaTabModule : public ILuaModule {
    TabController* tabCtrl_;
public:
    LuaTabModule(TabController* tc) : tabCtrl_(tc) {}
    std::string_view moduleName() const override { return "tab"; }
    void registerBindings(sol::state& lua, sol::table& terminal) override {
        auto tab = terminal.create_named("tab");
        tab.set_function("set_title", [this](int id, std::string t) { ... });
        tab.set_function("on_title_format", [this](sol::protected_function fn) {
            tabCtrl_->setTitleFormatCallback(std::move(fn));
        });
    }
};
```

### C++ Component Changes (Minimal Invasion)

각 컴포넌트에 콜백 슬롯만 추가:

```cpp
// tab_controller.h
using TitleFormatFn = std::function<std::string(const TabTitleInfo&)>;
void setTitleFormatCallback(TitleFormatFn fn);

// tab_controller.cpp - buildTabTitle() 내부
if (titleFormatCallback_) {
    auto result = titleFormatCallback_(info);
    if (!result.empty()) return result;  // Lua 결과 사용
}
// 기존 로직 fallback
```

### Initialization Flow

```
TerminalController 생성
  → LuaEngine 생성
  → TabController, CommandPalette, ... 생성
  → LuaTabModule(tabCtrl), LuaCommandModule(palette), ... 생성
  → luaEngine.registerModule(tabModule), ...
  → luaEngine.initialize()  // 모든 모듈의 registerBindings() 호출
  → config.lua 로드
```

## File Structure

```
core/include/termcore/lua_module.h          -- ILuaModule 인터페이스
core/src/lua_bindings/
    lua_tab_module.cpp                      -- terminal.tab
    lua_command_module.cpp                  -- terminal.command
    lua_event_module.cpp                    -- terminal.event
    lua_theme_module.cpp                    -- terminal.theme
    lua_url_module.cpp                      -- terminal.url
    lua_mux_module.cpp                      -- terminal.mux
    lua_shader_module.cpp                   -- terminal.shader
    lua_search_module.cpp                   -- terminal.search
    lua_clipboard_module.cpp               -- terminal.clipboard
    lua_paste_module.cpp                    -- terminal.paste
    lua_notify_module.cpp                   -- terminal.notify
    lua_status_module.cpp                   -- terminal.status
    lua_git_module.cpp                      -- terminal.git
    lua_session_module.cpp                  -- terminal.session
    lua_annotation_module.cpp              -- terminal.annotation
    lua_shell_module.cpp                    -- terminal.shell
    lua_workspace_module.cpp               -- terminal.workspace
    lua_settings_module.cpp                -- terminal.settings
    lua_vi_module.cpp                       -- terminal.vi
    lua_quick_module.cpp                    -- terminal.quick
```

## Lua API Reference

### Existing APIs (Unchanged)

```lua
terminal.config({...})
terminal.keymap(trigger, action)
terminal.keymap_preset(name)
terminal.colorscheme(name, {...})
terminal.profile({...})
terminal.default_profile(id)
terminal.hide_profile(id)
terminal.on(event, handler)
terminal.log(message)
terminal.version
terminal.platform
```

### terminal.tab — Tab Management

```lua
terminal.tab.on_title_format(function(info) return "[" .. info.process .. "] " .. info.cwd end)
terminal.tab.set_title(tab_id, "Custom Title")
terminal.tab.get_info(tab_id)  -- {id, title, process, cwd, is_active}
terminal.tab.list()
```

### terminal.command — Command Palette

```lua
terminal.command.register("My Command", function() ... end)
terminal.command.register("Open Config", function() ... end, {category="Plugin", icon="..."})
terminal.command.remove("My Command")
```

### terminal.event — Extended Events

```lua
terminal.event.on("tab_created", function(tab) ... end)
terminal.event.on("tab_closed", function(tab_id) ... end)
terminal.event.on("pane_focus", function(pane_id) ... end)
terminal.event.on("directory_changed", function(cwd) ... end)
terminal.event.on("key_press", function(key, mods) ... end)
terminal.event.on("paste_check", function(text) return true end)
```

### terminal.theme — Theme Control

```lua
terminal.theme.switch("Catppuccin Mocha")
terminal.theme.list()
terminal.theme.on_schedule(function(hour) return hour >= 18 and "dark" or "light" end)
terminal.theme.current()
```

### terminal.url — URL Detection

```lua
terminal.url.add_scheme("magnet", "obsidian", "vscode")
terminal.url.on_click(function(url) ... end)
terminal.url.set_color(0x89b4fa)
terminal.url.set_color_by_scheme("ssh", "#ff6600")
```

### terminal.mux — Layout/Pane

```lua
terminal.mux.layout("tiled")
terminal.mux.define_layout("my_layout", function(panes) ... end)
terminal.mux.split("right", 0.3)
terminal.mux.broadcast("all")
terminal.mux.zoom_toggle()
```

### terminal.shader — Shader Effects

```lua
terminal.shader.enable("CRT", 0.5)
terminal.shader.disable("Bloom")
terminal.shader.set_param("CRT", "curvature", 0.3)
terminal.shader.on_frame(function(time) return {intensity = math.sin(time) * 0.5 + 0.5} end)
```

### terminal.search — Search

```lua
terminal.search.set_debounce(100)
terminal.search.on_result(function(matches) ... end)
terminal.search.add_mode("fuzzy", function(query, text) ... end)
```

### terminal.clipboard — Clipboard History

```lua
terminal.clipboard.set_history_size(50)
terminal.clipboard.on_copy(function(text) ... end)
terminal.clipboard.set_preview_length(120)
```

### terminal.paste — Paste Guard

```lua
terminal.paste.add_danger("DROP TABLE", "Dangerous SQL command")
terminal.paste.whitelist("sudo apt update")
terminal.paste.set_mode("multiline")
```

### terminal.notify — Notifications

```lua
terminal.notify.send("Title", "Body", "normal")
terminal.notify.set_max(200)
terminal.notify.on_receive(function(n) ... end)
terminal.notify.deduplicate(5)
```

### terminal.status — Pane Status

```lua
terminal.status.set_pill(pane_id, {key="Build", value="OK", color="#00ff00"})
terminal.status.set_progress(pane_id, 0.75, "Building...")
terminal.status.log(pane_id, "info", "Task completed")
```

### terminal.git — Git Detection

```lua
terminal.git.set_cache_ttl(5)
terminal.git.on_branch_change(function(branch) ... end)
terminal.git.format_branch(function(name) return "branch: " .. name end)
```

### terminal.session — Session Management

```lua
terminal.session.on_save(function(session) ... end)
terminal.session.on_restore(function(session) ... end)
terminal.session.set_naming(function() return os.date("%Y%m%d") .. "-work" end)
```

### terminal.annotation — Annotations

```lua
terminal.annotation.add(row, "Note text", {color="#ffff00"})
terminal.annotation.remove(id)
terminal.annotation.set_badge_format("{branch} | {cwd}")
terminal.annotation.on_pattern("ERROR", function(row, text) ... end)
```

### terminal.shell — Shell Integration

```lua
terminal.shell.set_env("MY_VAR", "value")
terminal.shell.on_command_finish(function(exit_code, duration) ... end)
terminal.shell.set_ssh_term("xterm-256color")
```

### terminal.workspace — Workspace

```lua
terminal.workspace.on_status_change(function(snapshot) ... end)
terminal.workspace.set_cwd(workspace_id, "/path")
terminal.workspace.get_status()
```

### terminal.settings — Settings UI Extension

```lua
terminal.settings.add_category("My Plugin", {
    {key="plugin.option1", label="Option 1", type="toggle", default=true},
    {key="plugin.option2", label="Option 2", type="dropdown", options={"a","b","c"}},
})
```

### terminal.vi — Vi Copy Mode

```lua
terminal.vi.set_word_chars("a-zA-Z0-9_-")
terminal.vi.on_yank(function(text) ... end)
terminal.vi.map("gd", function() ... end)
```

### terminal.quick — Quick Terminal

```lua
terminal.quick.set_animation("slide", {duration=200, easing="ease-out"})
terminal.quick.set_size(0.4)
terminal.quick.set_position("top")
```

## Implementation Phases

### Phase 1: Registration Infrastructure

1. `ILuaModule` 인터페이스 생성
2. `LuaEngine`에 모듈 등록/초기화 로직 추가
3. `CMakeLists.txt`에 `lua_bindings/` 디렉토리 추가
4. 샘플 모듈 1개로 통합 테스트

### Phase 2: All Bindings (Parallel)

20개 모듈을 병렬 에이전트로 동시 구현. 각 에이전트가 담당:
- `lua_bindings/lua_*_module.cpp` 생성
- 대응 C++ 헤더에 콜백 슬롯 추가
- 대응 C++ 소스에 콜백 호출 로직 추가
- 초기화 코드에 모듈 등록 추가

## Plugin Sandbox Compatibility

새 모듈들도 기존 capability 시스템과 통합:
- `Config` capability → `terminal.theme`, `terminal.shader`, `terminal.quick` 접근
- `Events` capability → `terminal.event`, `terminal.git`, `terminal.workspace` 접근
- `Keybindings` capability → `terminal.command`, `terminal.vi` 접근
- `PaneRead` capability → `terminal.status`, `terminal.search`, `terminal.annotation` 접근
- `PaneWrite` capability → `terminal.mux`, `terminal.shell` 접근
- 새 capability `UI` → `terminal.settings` 접근
- 새 capability `Clipboard` → `terminal.clipboard`, `terminal.paste` 접근
