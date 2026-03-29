# BreadTerminal 플러그인 개발 가이드

## 목차

1. [개요](#개요)
2. [플러그인 형식](#플러그인-형식)
3. [Capability 시스템](#capability-시스템)
4. [샌드박스](#샌드박스)
5. [API 레퍼런스](#api-레퍼런스)
6. [고급 기능](#고급-기능)
7. [예제](#예제)

---

## 개요

BreadTerminal은 Lua 기반 플러그인 시스템을 제공합니다. 플러그인은 `~/.bt/plugins/` 디렉토리에 배치하며, 터미널 시작 시 자동으로 검색·로드됩니다.

**지원 Lua 버전:** sol2 바인딩 (Lua 5.4 호환)

---

## 플러그인 형식

### 단일 파일 플러그인

가장 간단한 형태입니다. `.lua` 파일 하나로 구성됩니다.

```
~/.bt/plugins/my-plugin.lua
```

```lua
plugin = {
    name = "my-plugin",
    version = "0.1.0",
    author = "Your Name",
    description = "간단한 플러그인 설명",
    capabilities = {"events"},
}

-- 플러그인 코드
terminal.on("bell", function()
    terminal.log("Bell!")
end)
```

`plugin` 테이블을 생략하면 파일명이 이름으로 사용되고, 모든 capability가 부여됩니다.

### 디렉토리 플러그인

복잡한 플러그인은 디렉토리 구조를 사용합니다.

```
~/.bt/plugins/my-plugin/
    plugin.lua   -- 메타데이터 (필수)
    init.lua     -- 진입점 (필수)
```

**plugin.lua** — 메타데이터만 정의합니다:

```lua
return {
    name = "my-plugin",
    version = "1.0.0",
    author = "Your Name",
    description = "플러그인 설명",
    capabilities = {"events", "config"},
}
```

**init.lua** — 실제 플러그인 로직을 작성합니다:

```lua
terminal.on("title_change", function(title)
    terminal.log("Title: " .. title)
end)
```

> 같은 이름의 디렉토리 플러그인과 단일 파일 플러그인이 있으면 디렉토리 플러그인이 우선합니다.

---

## Capability 시스템

플러그인은 사용할 기능에 해당하는 capability를 선언해야 합니다. 선언하지 않은 capability의 API는 샌드박스에 의해 차단됩니다.

| Capability | 설명 | 접근 가능 API |
|---|---|---|
| `events` | 터미널 이벤트 수신 | `terminal.on`, `terminal.tab`, `terminal.url`, `terminal.git`, `terminal.workspace` |
| `keybindings` | 키바인딩·명령 등록 | `terminal.command`, `terminal.vi`, `terminal.keymap` |
| `config` | 터미널 설정 변경 | `terminal.config`, `terminal.provider`, `terminal.theme`, `terminal.session`, `terminal.quick`, `terminal.shader`, `terminal.hooks` |
| `notifications` | 알림 전송 | `terminal.notify` |
| `pane_read` | 패널 콘텐츠 읽기 | `terminal.annotation`, `terminal.search`, `terminal.status` |
| `pane_write` | 패널에 텍스트 전송 | `terminal.shell`, `terminal.completion`, `terminal.mux` |
| `filesystem` | 파일 시스템 접근 | `io`, `os` 라이브러리 |
| `ui` | 설정 UI 확장 | `terminal.settings` |
| `clipboard` | 클립보드·붙여넣기 보호 | `terminal.clipboard`, `terminal.paste` |
| `networking` | HTTP 요청·네트워크 접근 | `terminal.http` |

---

## 샌드박스

각 플러그인은 선언한 capability에 따라 샌드박스가 적용됩니다.

- `filesystem` capability 없으면 `io`, `os` 전역이 차단됨
- 다른 capability 미선언 시 해당 `terminal.*` 하위 테이블이 nil
- 플러그인 간 전역 오염 방지: 로드 전 전역 백업 → 로드 후 복원

---

## API 레퍼런스

### 전역 함수

```lua
terminal.on(event, callback)      -- 이벤트 리스너 등록
terminal.log(message)             -- 로그 출력
terminal.action(action_name)      -- 내장 액션 실행
terminal.version                  -- BreadTerminal 버전 (string)
terminal.platform                 -- 플랫폼 ("windows", "macos", "linux")
```

---

### terminal.config(table) — Config

터미널 설정을 일괄 적용합니다.

```lua
terminal.config({
    font_family = "JetBrains Mono",
    font_size = 14,
    background = "#1e1e2e",
    foreground = "#cdd6f4",
    cursor_style = "bar",          -- "block", "bar", "underline"
    cursor_blink = true,
    scrollback_limit = 10000,
    background_opacity = 0.95,
    word_chars = "@-./_~?&=%+#",
    tab_bar_height = 0.06,
    tab_bar_always_visible = false,
    -- 사이드바 색상
    sidebar_color_running = "#89b4fa",
    sidebar_color_thinking = "#f9e2af",
    sidebar_color_tool_use = "#a6e3a1",
    sidebar_color_waiting = "#6c7086",
    sidebar_color_error = "#f38ba8",
    sidebar_color_idle = "#585b70",
    -- 커맨드 팔레트
    command_palette_width_percent = 0.4,
    command_palette_max_items = 12,
    command_palette_backdrop_opacity = 0.5,
    -- 상태 바
    status_bar_progress_height = 3.0,
    status_bar_progress_track_color = "#313244",
})
```

### terminal.keymap(trigger, action) — Config

```lua
terminal.keymap("ctrl+t", "new_tab")
terminal.keymap("ctrl+shift+c", "copy")
```

### terminal.keymap_preset(name) — Config

```lua
terminal.keymap_preset("default")
```

### terminal.colorscheme(name, table) — Config

```lua
terminal.colorscheme("my-theme", {
    background = "#1e1e2e",
    foreground = "#cdd6f4",
    cursor_color = "#f5e0dc",
    selection_background = "#45475a",
    palette = {
        "#45475a", "#f38ba8", "#a6e3a1", "#f9e2af",
        "#89b4fa", "#cba6f7", "#94e2d5", "#bac2de",
        "#585b70", "#f38ba8", "#a6e3a1", "#f9e2af",
        "#89b4fa", "#cba6f7", "#94e2d5", "#a6adc8",
    },
})
```

---

### terminal.tab — Events

```lua
-- 탭 타이틀 포맷 커스터마이즈
terminal.tab.on_title_format(function(info)
    -- info: {tab_index, process, cwd, title, is_active, custom_title}
    return info.process .. " @ " .. info.cwd
end)

terminal.tab.set_title(tab_id, "My Title")
terminal.tab.get_info(tab_id)  -- -> {tab_index, title, process, icon, ...}
terminal.tab.list()            -- -> 모든 탭 정보 배열
terminal.tab.set_process_icon("nvim", "")
terminal.tab.move_left()
terminal.tab.move_right()
```

---

### terminal.url — Events

```lua
terminal.url.add_scheme("https", "http", "ftp", "ssh", "git")
terminal.url.set_color(0x89b4fa)
terminal.url.set_color_by_scheme("https", "#a6e3a1")
terminal.url.set_terminators("\"'<>(){}|\\^`")
terminal.url.set_trailing_punctuation(".,;:!?")

terminal.url.on_click(function(url)
    terminal.log("Clicked: " .. url)
    return false  -- false면 기본 동작 수행, true면 커스텀 처리
end)
```

---

### terminal.command — Keybindings

```lua
terminal.command.register("Hello World", function()
    terminal.log("Hello!")
end, {
    category = "Custom",
    description = "인사 출력",
})

terminal.command.remove("Hello World")
```

---

### terminal.provider(id, opts) — Config

AI CLI 프로바이더를 등록합니다.

```lua
terminal.provider("claude_code", {
    display_name = "Claude Code",
    agent_type = "claude",
    detect_process = {"claude"},
    detect_env = {"CLAUDE_CODE"},
    state_patterns = {
        { state = "thinking", pattern = "Thinking..." },
        { state = "tool_use", pattern = "Tool:" },
        { state = "error", pattern = "Error:" },
    },
    hooks = {
        config_dir = "~/.claude",
        settings_file = "settings.json",
        settings_format = "json",
        events = {
            { bread_event = "notification", hook_name = "on_notification", env_map = {} },
        },
    },
})

terminal.agent.set_stale_timeout(120)  -- seconds
```

---

### terminal.paste — Clipboard

```lua
-- 단순 패턴: 해당 문자열 포함 시 위험
terminal.paste.add_danger("sudo ", "Contains sudo command")

-- 복합 패턴: 두 패턴 모두 포함 시 위험
terminal.paste.add_compound_danger("rm -rf", "~", "Recursive delete targeting home")

-- 파이프 패턴: sourceCmd가 | sh, | bash 등에 파이프될 때 위험
terminal.paste.add_pipe_danger("curl", "Curl piped to shell")

terminal.paste.whitelist("safe-pattern")
terminal.paste.set_mode("multiline")
```

---

### terminal.notify — Notifications

```lua
terminal.notify.send("Build Complete", "빌드가 완료되었습니다.", "normal")
-- urgency: "low", "normal", "critical"

terminal.notify.set_max(50)
terminal.notify.deduplicate(5)  -- 5초 내 중복 알림 제거

terminal.notify.on_receive(function(n)
    -- n: {id, title, body, read, pane_id}
    terminal.log("알림: " .. n.title)
end)
```

---

### terminal.clipboard — Clipboard

```lua
terminal.clipboard.set_history_size(100)
terminal.clipboard.set_preview_length(200)

terminal.clipboard.on_copy(function(text)
    terminal.log("Copied " .. #text .. " chars")
end)
```

---

### terminal.annotation — PaneRead

```lua
local id = terminal.annotation.add(5, "주석", { color = "#f9e2af" })
terminal.annotation.remove(id)
terminal.annotation.set_badge_format("{process} [{cwd}]")

terminal.annotation.on_pattern("ERROR", function(row, text)
    terminal.annotation.add(row, "⚠ Error", { color = "#f38ba8" })
end)
```

---

### terminal.status — PaneRead

```lua
terminal.status.set_pill(pane_id, {
    key = "branch",
    value = "main",
    color = "#a6e3a1",
})

terminal.status.set_progress(pane_id, 0.75, "Building...")
terminal.status.log(pane_id, "success", "Build completed")
-- level: "info", "success", "warning", "error"
```

---

### terminal.search — PaneRead

```lua
terminal.search.set_debounce(150)  -- ms

terminal.search.on_result(function(matches)
    -- matches: [{row, start_col, end_col}, ...]
    terminal.log("Found " .. #matches .. " matches")
end)
```

---

### terminal.completion — PaneWrite

```lua
terminal.completion.register_provider("my-complete", {
    priority = 50,
    async = false,
    on_input = function(ctx)
        -- ctx: {text, cwd}
        if ctx.text:match("^git ") then
            return "commit -m \"\""
        end
        return nil
    end,
})

terminal.completion.remove_provider("my-complete")
terminal.completion.set_suggestion("my-complete", "suggested text")
terminal.completion.set_enabled(true)
```

---

### terminal.shell — PaneWrite

```lua
terminal.shell.set_env("MY_VAR", "value")
terminal.shell.set_ssh_term("xterm-256color")

terminal.shell.on_command_finish(function(exit_code, duration)
    if exit_code ~= 0 then
        terminal.notify.send("Command Failed", "Exit code: " .. exit_code)
    end
end)
```

---

### terminal.mux — PaneWrite

```lua
terminal.mux.split("right", 0.5)    -- 오른쪽으로 50% 분할
terminal.mux.split("down", 0.3)     -- 아래로 30% 분할
terminal.mux.layout("tiled")        -- "even_horizontal", "even_vertical", "tiled", "main_left", "main_top"
terminal.mux.broadcast("all")       -- "all", "selected", "off"
terminal.mux.zoom_toggle()

terminal.mux.define_layout("dev", function(panes)
    -- 커스텀 레이아웃 정의
end)
terminal.mux.apply_custom_layout("dev")
```

---

### terminal.theme — Config

```lua
terminal.theme.switch("catppuccin-mocha")
local name = terminal.theme.current()
local themes = terminal.theme.list()

terminal.theme.on_schedule(function(hour)
    if hour >= 18 or hour < 6 then return "dark" end
    return "light"
end)
```

---

### terminal.git — Events

```lua
terminal.git.set_cache_ttl(30)  -- seconds

terminal.git.on_branch_change(function(branch)
    terminal.log("Branch: " .. branch)
end)

terminal.git.format_branch(function(name)
    return " " .. name
end)
```

---

### terminal.session — Config

```lua
terminal.session.on_save(function(name)
    terminal.log("Session saved: " .. name)
end)

terminal.session.on_restore(function(name)
    terminal.log("Session restored: " .. name)
end)

terminal.session.set_naming(function()
    return os.date("%Y-%m-%d_%H%M")
end)
```

---

### terminal.settings — UI

```lua
terminal.settings.add_category("My Plugin", {
    { key = "enabled", label = "활성화", type = "toggle", default = "true" },
    { key = "name", label = "이름", type = "text" },
    { key = "count", label = "개수", type = "number", default = "10" },
    { key = "mode", label = "모드", type = "dropdown", options = {"fast", "slow"} },
})
```

---

### terminal.vi — Keybindings

```lua
terminal.vi.set_word_chars("@-./_~")

terminal.vi.on_yank(function(text)
    terminal.log("Yanked: " .. text)
end)

terminal.vi.map("gx", function()
    -- 커스텀 vi 키매핑
end)
```

---

### terminal.quick — Config

```lua
terminal.quick.set_size(0.4)           -- 화면의 40%
terminal.quick.set_position("top")     -- "top", "bottom", "left", "right"
terminal.quick.set_animation("slide", { duration = 200, easing = "ease-out" })
```

---

### terminal.shader — Config

```lua
terminal.shader.enable("crt", 0.5)
terminal.shader.disable("crt")
terminal.shader.set_param("crt", "scanline_intensity", 0.3)

terminal.shader.on_frame(function(time)
    -- 프레임마다 호출
end)
```

---

### terminal.workspace — Events

```lua
terminal.workspace.on_status_change(function()
    local status = terminal.workspace.get_status()
    -- status: [{id, name, git_branch, cwd, is_active, unread_notifications}, ...]
end)

terminal.workspace.set_cwd(workspace_id, "/path/to/dir")
```

---

### terminal.hooks — Config

```lua
terminal.hooks.is_installed("claude_code")  -- -> bool
terminal.hooks.install("claude_code")       -- -> bool

terminal.hooks.on_provider_detected(function(data)
    -- data: {provider_id, pane_id}
    if not terminal.hooks.is_installed(data.provider_id) then
        terminal.hooks.install(data.provider_id)
    end
end)
```

---

## 예제

### 단일 파일: 빌드 알림 플러그인

```lua
plugin = {
    name = "build-notify",
    version = "0.1.0",
    author = "dev",
    description = "빌드 완료 시 알림 전송",
    capabilities = {"pane_write", "notifications"},
}

terminal.shell.on_command_finish(function(exit_code, duration)
    if duration > 10 then
        local status = exit_code == 0 and "성공" or "실패"
        terminal.notify.send("빌드 " .. status,
            string.format("%.1f초 소요 (exit %d)", duration, exit_code),
            exit_code == 0 and "normal" or "critical")
    end
end)
```

### 단일 파일: URL 커스텀 핸들러

```lua
plugin = {
    name = "jira-links",
    version = "0.1.0",
    capabilities = {"events"},
}

terminal.url.add_scheme("jira")

terminal.url.on_click(function(url)
    if url:match("^jira://") then
        local ticket = url:match("jira://(.+)")
        terminal.action("open_url")  -- 기본 브라우저로 열기
        return true
    end
    return false
end)
```

### 디렉토리: 자동 테마 전환

```
~/.bt/plugins/auto-theme/
    plugin.lua
    init.lua
```

**plugin.lua:**
```lua
return {
    name = "auto-theme",
    version = "1.0.0",
    description = "시간대별 자동 테마 전환",
    capabilities = {"config"},
}
```

**init.lua:**
```lua
terminal.theme.on_schedule(function(hour)
    if hour >= 8 and hour < 18 then
        return "catppuccin-latte"
    else
        return "catppuccin-mocha"
    end
end)
```

### 데이터 테이블 패턴 (커맨드 등록)

```lua
plugin = {
    name = "my-commands",
    version = "0.1.0",
    capabilities = {"keybindings"},
}

local commands = {
    { "Clear All",   "clear_scrollback", "Terminal",  "스크롤백 버퍼 초기화" },
    { "Toggle Zoom", "zoom_toggle",      "Pane",      "패널 줌 토글" },
}

for _, cmd in ipairs(commands) do
    terminal.command.register(cmd[1], function() terminal.action(cmd[2]) end,
        { category = cmd[3], description = cmd[4] })
end
```

---

---

## 고급 기능

### 타이머 — Events

```lua
-- 1초 후 1회 실행
local id = terminal.timer.once(1000, function()
    terminal.log("1초 경과")
end)

-- 5초마다 반복 실행
local id = terminal.timer.every(5000, function()
    terminal.log("5초마다 실행")
end)

-- 타이머 취소
terminal.timer.cancel(id)

-- 디바운스 (마지막 호출 후 300ms 지나면 실행)
local debounced = terminal.timer.debounce(300, function(text)
    terminal.log("debounced: " .. text)
end)
debounced("hello")

-- 비동기 sleep (async 내에서 사용)
terminal.timer.sleep(1000)
```

### 비동기/코루틴 — Events

```lua
-- 비동기 태스크 생성
terminal.async(function()
    terminal.log("시작")
    terminal.await(terminal.timer.sleep(2000))
    terminal.log("2초 후")
    terminal.yield()  -- 다음 tick에서 재개
    terminal.log("다음 tick")
end)
```

### require() 모듈 시스템

플러그인은 `~/.bt/plugins/lib/` 디렉토리의 공유 모듈을 `require()`로 불러올 수 있습니다.

```lua
-- ~/.bt/plugins/lib/utils.lua
local M = {}
function M.greet(name) return "Hello, " .. name end
return M
```

```lua
-- 플러그인에서 사용
local utils = require("lib.utils")
terminal.log(utils.greet("World"))
```

보안: 시스템 경로 로드 차단, C 모듈 차단. `~/.bt/plugins/` 하위만 허용.

### 플로팅 윈도우 — UI

```lua
local win = terminal.window.open({
    width = 60, height = 20,
    title = "My Plugin",
    border = "rounded",
    relative = "editor",
    anchor = "center",
    style = {
        background = "#1e1e2e",
        foreground = "#cdd6f4",
        border_color = "#89b4fa",
    },
})

win:set_lines({"Line 1", "Line 2"})
win:set_cursor(0, 0)
win:on_key("q", function() win:close() end)
win:on_key("j", function() win:move_cursor(1, 0) end)
win:on_close(function() terminal.log("closed") end)
```

### 피커/입력 다이얼로그 — UI

```lua
-- 목록 선택
terminal.picker.show({
    title = "Select File",
    items = {"file1.lua", "file2.lua"},
    filter = true, fuzzy = true,
    on_select = function(index, item) terminal.log(item) end,
    on_cancel = function() end,
})

-- 텍스트 입력
terminal.picker.input({
    title = "Rename",
    prompt = "New name: ",
    default = "current",
    on_confirm = function(text) terminal.log(text) end,
})

-- 확인 다이얼로그
terminal.picker.confirm({
    title = "Delete?",
    message = "되돌릴 수 없습니다.",
    on_yes = function() terminal.log("삭제") end,
    on_no = function() end,
})
```

### 패널 직접 접근 — PaneRead/PaneWrite

```lua
local pane = terminal.pane.active()

-- 속성
local rows = pane:rows()
local cols = pane:cols()

-- 버퍼 읽기
local lines = pane:get_lines(1, 10)
local cursor_row, cursor_col = pane:cursor()
local selection = pane:get_selection()

-- 텍스트 전송
pane:send_text("ls -la\n")
pane:send_keys("ctrl+c")

-- 가상 텍스트 오버레이
local mark = pane:add_virtual_text(5, 0, "annotation", {fg = "#89b4fa"})
pane:remove_virtual_text(mark)

-- 하이라이트
local hl = pane:add_highlight(3, 0, 10, {fg = "#f38ba8", bold = true})
pane:remove_highlight(hl)

-- 이벤트
pane:on_output(function(text) end)
pane:on_exit(function(exit_code) end)
```

### JSON 유틸리티 — Events

```lua
local json_str = terminal.json.encode({name = "test", count = 42})
local data = terminal.json.decode('{"key": "value"}')
local pretty = terminal.json.encode(data, {pretty = true, indent = 2})
```

### 영속 저장소 — FileSystem

```lua
local store = terminal.storage.open("my-plugin")
store:set("key", "value")
store:set("data", {a = 1, b = {2, 3}})
local val = store:get("key", "default")
store:delete("key")
store:save()
```

데이터는 `~/.bt/data/<namespace>.json`에 자동 저장됩니다.

### HTTP 클라이언트 — Networking

```lua
terminal.http.get("https://api.github.com/repos/user/repo", function(res)
    if res.ok then
        local data = terminal.json.decode(res.body)
        terminal.log("Stars: " .. data.stargazers_count)
    end
end)

terminal.http.post("https://api.example.com/data", {
    headers = {["Content-Type"] = "application/json"},
    body = terminal.json.encode({key = "value"}),
    timeout = 5000,
}, function(res) end)
```

### 의존성 및 지연 로딩

```lua
-- plugin.lua 확장 메타데이터
return {
    name = "my-plugin",
    version = "1.0.0",
    capabilities = {"events"},
    dependencies = {"lib-utils >= 0.2.0"},  -- 의존성
    after = {"theme-plugin"},               -- 로딩 순서
    lazy = true,                            -- 지연 로드
    on_event = "bell",                      -- 이벤트 트리거
    on_command = "my_command",              -- 명령 트리거
}
```

### 라이프사이클 훅

```lua
-- init.lua
function setup(opts)
    -- 플러그인 로드 후 호출
    terminal.log("plugin loaded")
end

function on_unload()
    -- 언로드 시 정리
    terminal.log("plugin unloaded")
end
```

### 커스텀 이벤트

```lua
-- 이벤트 발행
terminal.event.emit("my-plugin:file-selected", {path = "/foo"})

-- 1회성 리스너
terminal.event.once("bell", function() terminal.log("bell once") end)

-- 리스너 제거
terminal.event.off("my-plugin:file-selected")
```

---

## 플러그인 디버깅

- `terminal.log(msg)` — 로그 출력 (capability 불필요)
- 플러그인 로드 실패 시 `PluginState::Error`로 전환되고 에러 메시지가 기록됩니다
- 샌드박스에 의해 차단된 API 호출은 nil 접근 에러로 나타납니다

---

## 참고 사항

- 플러그인은 `~/.bt/plugins/` 디렉토리에 배치합니다
- 터미널 시작 시 자동 검색·로드됩니다
- capability를 최소한으로 선언하는 것이 보안상 권장됩니다
- 내장 기본값(`core/defaults/*.lua`)은 플러그인보다 먼저 로드됩니다
- 기존 플러그인 예제는 `plugins/` 디렉토리를 참고하세요
