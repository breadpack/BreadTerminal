# Lua Plugin API — Phase 2: All Binding Modules (Parallel)

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement all 20 Lua binding modules in parallel, each exposing a C++ subsystem as a `terminal.<name>` Lua API.

**Architecture:** Each module follows the pattern established in Phase 1 (`LuaTabModule`): implement `ILuaModule`, create sub-table in `registerBindings()`, add callback slots to backing C++ component. Modules are independent — no shared file edits needed.

**Tech Stack:** C++17, sol2 (sol3), Lua 5.4, Google Test

**Spec:** `docs/superpowers/specs/2026-03-24-lua-plugin-api-design.md`
**Phase 1 Plan:** `docs/superpowers/plans/2026-03-24-lua-plugin-api-phase1.md`

**Prerequisites:** Phase 1 must be complete (ILuaModule interface, LuaEngine module support, CMake glob).

---

## Common Module Pattern

Every module follows this exact pattern. Each task below implements one module.

**Files per module:**
1. `core/src/lua_bindings/lua_<name>_module.h` — module class declaration
2. `core/src/lua_bindings/lua_<name>_module.cpp` — module implementation
3. `core/include/termcore/<component>.h` — add callback slot(s) to backing C++ class
4. `core/src//<component>.cpp` — invoke callback in relevant methods
5. `tests/test_lua_<name>_module.cpp` — unit tests
6. `tests/CMakeLists.txt` — add test file

**Header template:**

```cpp
#pragma once
#include "termcore/lua_module.h"

namespace termcore {
class <Component>;

class Lua<Name>Module : public ILuaModule {
public:
    explicit Lua<Name>Module(<Component>* ptr);
    std::string_view moduleName() const override { return "<name>"; }
    PluginCapability requiredCapability() const override { return PluginCapability::<Cap>; }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;
private:
    <Component>* ptr_;
    // sol::protected_function stored callbacks...
};
} // namespace termcore
```

**Implementation template:**

```cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include "lua_<name>_module.h"
#include "termcore/<component>.h"

namespace termcore {

Lua<Name>Module::Lua<Name>Module(<Component>* ptr) : ptr_(ptr) {}

void Lua<Name>Module::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);
    auto tbl = terminal.create_named("<name>");

    tbl.set_function("...", [this](...) { ... });
}

void Lua<Name>Module::clearCallbacks() {
    // Reset all stored callbacks and C++ component callback slots
}

} // namespace termcore
```

**Reentrancy guard pattern** (for any callback that may trigger re-entry):

```cpp
bool inCallback_ = false;
// In the callback invocation site:
if (callbackFn_ && !inCallback_) {
    inCallback_ = true;
    auto result = callbackFn_(...);
    inCallback_ = false;
    if (!result.empty()) return result;
}
```

---

## Task 1: LuaTabModule — complete implementation

> Phase 1 created a skeleton. This task completes `terminal.tab` with full `list()`, `get_info()`, `set_title()`.

**Files:**
- Modify: `core/src/lua_bindings/lua_tab_module.h`
- Modify: `core/src/lua_bindings/lua_tab_module.cpp`
- Modify: `core/include/termcore/tab_controller.h`
- Create: `tests/test_lua_tab_module.cpp`
- Modify: `tests/CMakeLists.txt`

**Lua API:**
```lua
terminal.tab.on_title_format(function(info) return "..." end)
terminal.tab.set_title(tab_id, "Custom Title")
terminal.tab.get_info(tab_id) -- {id, title, process, cwd, is_active}
terminal.tab.list() -- array of tab info tables
```

**C++ changes needed:**
- `TabController`: add `setCustomTitle(int tabIndex, std::string title)` method
- `tabBarInfo()`: apply custom title if set, then try Lua format callback, then default

- [ ] Write tests for `terminal.tab.on_title_format`, `list`, `get_info`, `set_title`
- [ ] Implement full `registerBindings()` with sol table returns
- [ ] Add `setCustomTitle()` to `TabController`
- [ ] Run tests, verify all pass
- [ ] Commit

---

## Task 2: LuaCommandModule — `terminal.command`

**Files:**
- Create: `core/src/lua_bindings/lua_command_module.h`
- Create: `core/src/lua_bindings/lua_command_module.cpp`
- Modify: `core/include/termcore/command_palette.h`
- Modify: `core/src/command_palette.cpp`
- Create: `tests/test_lua_command_module.cpp`
- Modify: `tests/CMakeLists.txt`

**Lua API:**
```lua
terminal.command.register("name", function() end)
terminal.command.register("name", function() end, {category="Plugin", icon="x"})
terminal.command.remove("name")
```

**C++ changes needed:**
- `CommandPalette`: add `registerLuaCommand(name, description, category, callback)` and `removeLuaCommand(name)`
- Store Lua commands separately from built-in actions
- Include Lua commands in fuzzy search results

**Capability:** `PluginCapability::Keybindings`

- [ ] Write tests for register, remove, and conflict with built-in
- [ ] Add Lua command storage to `CommandPalette`
- [ ] Implement `LuaCommandModule::registerBindings()`
- [ ] Run tests, verify all pass
- [ ] Commit

---

## Task 3: LuaEventModule — `terminal.event`

**Files:**
- Create: `core/src/lua_bindings/lua_event_module.h`
- Create: `core/src/lua_bindings/lua_event_module.cpp`
- Create: `tests/test_lua_event_module.cpp`
- Modify: `tests/CMakeLists.txt`

**Lua API:**
```lua
terminal.event.on("tab_created", function(tab) end)
terminal.event.on("tab_closed", function(tab_id) end)
terminal.event.on("pane_focus", function(pane_id) end)
terminal.event.on("directory_changed", function(cwd) end)
terminal.event.on("key_press", function(key, mods) end)
terminal.event.on("paste_check", function(text) return true end)
```

**C++ changes needed:**
- This module extends the existing `terminal.on()` with new event types
- Store handlers in module's own map (not in LuaEngine's `event_handlers`)
- Provide `fireModuleEvent(name, ...)` for other C++ components to call

**Capability:** `PluginCapability::Events`

- [ ] Write tests for each event type registration and firing
- [ ] Implement `LuaEventModule` with separate handler map
- [ ] Add `fireModuleEvent()` public method
- [ ] Run tests, verify all pass
- [ ] Commit

---

## Task 4: LuaThemeModule — `terminal.theme`

**Files:**
- Create: `core/src/lua_bindings/lua_theme_module.h`
- Create: `core/src/lua_bindings/lua_theme_module.cpp`
- Modify: `core/include/termcore/theme_loader.h`
- Create: `tests/test_lua_theme_module.cpp`
- Modify: `tests/CMakeLists.txt`

**Lua API:**
```lua
terminal.theme.switch("name")
terminal.theme.list()
terminal.theme.current()
terminal.theme.on_schedule(function(hour) return "dark" end)
```

**C++ changes needed:**
- Need reference to `Config*` and `ConfigApplier*` for theme switching
- `ThemeLoader`: expose `listThemes()` if not already available
- Store schedule callback, provide `evaluateSchedule(int hour)` for timer integration

**Capability:** `PluginCapability::Config`

- [ ] Write tests for switch, list, current, on_schedule
- [ ] Implement `LuaThemeModule`
- [ ] Run tests, verify all pass
- [ ] Commit

---

## Task 5: LuaUrlModule — `terminal.url`

**Files:**
- Create: `core/src/lua_bindings/lua_url_module.h`
- Create: `core/src/lua_bindings/lua_url_module.cpp`
- Modify: `core/include/termcore/url_detector.h`
- Modify: `core/src/url_detector.cpp`
- Modify: `core/include/termcore/url_highlight.h`
- Create: `tests/test_lua_url_module.cpp`
- Modify: `tests/CMakeLists.txt`

**Lua API:**
```lua
terminal.url.add_scheme("magnet", "obsidian")
terminal.url.on_click(function(url) end)
terminal.url.set_color(0x89b4fa)
terminal.url.set_color_by_scheme("ssh", "#ff6600")
```

**C++ changes needed:**
- `UrlDetector`: add `addCustomScheme(string)`, currently schemes are hardcoded
- `UrlHighlightManager`: add click callback slot, per-scheme color map

**Capability:** `PluginCapability::Events`

- [ ] Write tests for add_scheme, on_click, set_color
- [ ] Add `addCustomScheme()` to `UrlDetector`
- [ ] Implement `LuaUrlModule`
- [ ] Run tests, verify all pass
- [ ] Commit

---

## Task 6: LuaMuxModule — `terminal.mux`

**Files:**
- Create: `core/src/lua_bindings/lua_mux_module.h`
- Create: `core/src/lua_bindings/lua_mux_module.cpp`
- Create: `tests/test_lua_mux_module.cpp`
- Modify: `tests/CMakeLists.txt`

**Lua API:**
```lua
terminal.mux.layout("tiled")
terminal.mux.define_layout("name", function(panes) end)
terminal.mux.split("right", 0.3)
terminal.mux.broadcast("all")
terminal.mux.zoom_toggle()
```

**C++ changes needed:**
- Need `TabController*` and `Mux*` references
- `Mux`: add `registerCustomLayout(name, callback)` for Lua-defined layouts

**Capability:** `PluginCapability::PaneWrite`

- [ ] Write tests for layout, split, broadcast, zoom
- [ ] Implement `LuaMuxModule`
- [ ] Run tests, verify all pass
- [ ] Commit

---

## Task 7: LuaShaderModule — `terminal.shader`

**Files:**
- Create: `core/src/lua_bindings/lua_shader_module.h`
- Create: `core/src/lua_bindings/lua_shader_module.cpp`
- Modify: `core/include/termcore/shader_effect.h`
- Create: `tests/test_lua_shader_module.cpp`
- Modify: `tests/CMakeLists.txt`

**Lua API:**
```lua
terminal.shader.enable("CRT", 0.5)
terminal.shader.disable("Bloom")
terminal.shader.set_param("CRT", "curvature", 0.3)
terminal.shader.on_frame(function(time) return {intensity=0.5} end)
```

**C++ changes needed:**
- `ShaderEffect`: add `setEnabled(name, intensity)`, `setCustomParam(name, key, value)` if not present
- Add frame callback slot for dynamic parameter updates

**Capability:** `PluginCapability::Config`

- [ ] Write tests for enable, disable, set_param, on_frame
- [ ] Implement `LuaShaderModule`
- [ ] Run tests, verify all pass
- [ ] Commit

---

## Task 8: LuaSearchModule — `terminal.search`

**Files:**
- Create: `core/src/lua_bindings/lua_search_module.h`
- Create: `core/src/lua_bindings/lua_search_module.cpp`
- Modify: `core/include/termcore/search_controller.h`
- Create: `tests/test_lua_search_module.cpp`
- Modify: `tests/CMakeLists.txt`

**Lua API:**
```lua
terminal.search.set_debounce(100)
terminal.search.on_result(function(matches) end)
terminal.search.add_mode("fuzzy", function(query, text) end)
```

**C++ changes needed:**
- `SearchController`: add `setDebounceMs(int)`, result callback slot

**Capability:** `PluginCapability::PaneRead`

- [ ] Write tests for set_debounce, on_result
- [ ] Implement `LuaSearchModule`
- [ ] Run tests, verify all pass
- [ ] Commit

---

## Task 9: LuaClipboardModule — `terminal.clipboard`

**Files:**
- Create: `core/src/lua_bindings/lua_clipboard_module.h`
- Create: `core/src/lua_bindings/lua_clipboard_module.cpp`
- Modify: `core/include/termcore/clipboard_history.h`
- Modify: `core/src/clipboard_history.cpp`
- Create: `tests/test_lua_clipboard_module.cpp`
- Modify: `tests/CMakeLists.txt`

**Lua API:**
```lua
terminal.clipboard.set_history_size(50)
terminal.clipboard.on_copy(function(text) end)
terminal.clipboard.set_preview_length(120)
```

**C++ changes needed:**
- `ClipboardHistory`: add `setMaxEntries(int)`, `setPreviewMaxLength(int)`, copy callback

**Capability:** `PluginCapability::Clipboard`

- [ ] Write tests for set_history_size, on_copy, set_preview_length
- [ ] Implement `LuaClipboardModule`
- [ ] Run tests, verify all pass
- [ ] Commit

---

## Task 10: LuaPasteModule — `terminal.paste`

**Files:**
- Create: `core/src/lua_bindings/lua_paste_module.h`
- Create: `core/src/lua_bindings/lua_paste_module.cpp`
- Modify: `core/include/termcore/paste_guard.h`
- Modify: `core/src/paste_guard.cpp`
- Create: `tests/test_lua_paste_module.cpp`
- Modify: `tests/CMakeLists.txt`

**Lua API:**
```lua
terminal.paste.add_danger("DROP TABLE", "Dangerous SQL")
terminal.paste.whitelist("sudo apt update")
terminal.paste.set_mode("multiline")
```

**C++ changes needed:**
- `PasteGuard`: add `addCustomDanger(pattern, description)`, `addWhitelist(pattern)`, `setModeFromString(string)`

**Capability:** `PluginCapability::Clipboard`

- [ ] Write tests for add_danger, whitelist, set_mode
- [ ] Add custom pattern support to `PasteGuard`
- [ ] Implement `LuaPasteModule`
- [ ] Run tests, verify all pass
- [ ] Commit

---

## Task 11: LuaNotifyModule — `terminal.notify`

**Files:**
- Create: `core/src/lua_bindings/lua_notify_module.h`
- Create: `core/src/lua_bindings/lua_notify_module.cpp`
- Modify: `core/include/termcore/notification.h`
- Modify: `core/src/notification.cpp`
- Create: `tests/test_lua_notify_module.cpp`
- Modify: `tests/CMakeLists.txt`

**Lua API:**
```lua
terminal.notify.send("Title", "Body", "normal")
terminal.notify.set_max(200)
terminal.notify.on_receive(function(n) end)
terminal.notify.deduplicate(5)
```

**C++ changes needed:**
- `NotificationStore`: add `setMaxNotifications(int)`, `setDeduplicateWindow(int)`, receive callback

**Capability:** `PluginCapability::Notifications`

- [ ] Write tests for send, set_max, on_receive, deduplicate
- [ ] Implement `LuaNotifyModule`
- [ ] Run tests, verify all pass
- [ ] Commit

---

## Task 12: LuaStatusModule — `terminal.status`

**Files:**
- Create: `core/src/lua_bindings/lua_status_module.h`
- Create: `core/src/lua_bindings/lua_status_module.cpp`
- Create: `tests/test_lua_status_module.cpp`
- Modify: `tests/CMakeLists.txt`

**Lua API:**
```lua
terminal.status.set_pill(pane_id, {key="Build", value="OK", color="#00ff00"})
terminal.status.set_progress(pane_id, 0.75, "Building...")
terminal.status.log(pane_id, "info", "Task completed")
```

**C++ changes needed:**
- Need `TabController*` to look up `PaneState` by ID and access its `PaneStatus`

**Capability:** `PluginCapability::PaneRead`

- [ ] Write tests for set_pill, set_progress, log
- [ ] Implement `LuaStatusModule`
- [ ] Run tests, verify all pass
- [ ] Commit

---

## Task 13: LuaGitModule — `terminal.git`

**Files:**
- Create: `core/src/lua_bindings/lua_git_module.h`
- Create: `core/src/lua_bindings/lua_git_module.cpp`
- Modify: `core/include/termcore/git_branch_detector.h`
- Modify: `core/src/git_branch_detector.cpp`
- Create: `tests/test_lua_git_module.cpp`
- Modify: `tests/CMakeLists.txt`

**Lua API:**
```lua
terminal.git.set_cache_ttl(5)
terminal.git.on_branch_change(function(branch) end)
terminal.git.format_branch(function(name) return "..." end)
```

**C++ changes needed:**
- `GitBranchDetector`: add `setCacheTtlSeconds(int)`, branch change callback, format callback

**Capability:** `PluginCapability::Events`

- [ ] Write tests for set_cache_ttl, on_branch_change, format_branch
- [ ] Implement `LuaGitModule`
- [ ] Run tests, verify all pass
- [ ] Commit

---

## Task 14: LuaSessionModule — `terminal.session`

**Files:**
- Create: `core/src/lua_bindings/lua_session_module.h`
- Create: `core/src/lua_bindings/lua_session_module.cpp`
- Modify: `core/include/termcore/session_manager.h`
- Create: `tests/test_lua_session_module.cpp`
- Modify: `tests/CMakeLists.txt`

**Lua API:**
```lua
terminal.session.on_save(function(session) end)
terminal.session.on_restore(function(session) end)
terminal.session.set_naming(function() return "name" end)
```

**C++ changes needed:**
- `MultiSessionManager`: add save/restore callback slots, naming callback

**Capability:** `PluginCapability::Config`

- [ ] Write tests for on_save, on_restore, set_naming
- [ ] Implement `LuaSessionModule`
- [ ] Run tests, verify all pass
- [ ] Commit

---

## Task 15: LuaAnnotationModule — `terminal.annotation`

**Files:**
- Create: `core/src/lua_bindings/lua_annotation_module.h`
- Create: `core/src/lua_bindings/lua_annotation_module.cpp`
- Modify: `core/include/termcore/annotations.h`
- Create: `tests/test_lua_annotation_module.cpp`
- Modify: `tests/CMakeLists.txt`

**Lua API:**
```lua
terminal.annotation.add(row, "text", {color="#ffff00"})
terminal.annotation.remove(id)
terminal.annotation.set_badge_format("{branch} | {cwd}")
terminal.annotation.on_pattern("ERROR", function(row, text) end)
```

**C++ changes needed:**
- `AnnotationManager`: add pattern-match callback system
- `TabBadge`: expose format string setter

**Capability:** `PluginCapability::PaneRead`

- [ ] Write tests for add, remove, set_badge_format, on_pattern
- [ ] Implement `LuaAnnotationModule`
- [ ] Run tests, verify all pass
- [ ] Commit

---

## Task 16: LuaShellModule — `terminal.shell`

**Files:**
- Create: `core/src/lua_bindings/lua_shell_module.h`
- Create: `core/src/lua_bindings/lua_shell_module.cpp`
- Modify: `core/include/termcore/shell_integration.h`
- Create: `tests/test_lua_shell_module.cpp`
- Modify: `tests/CMakeLists.txt`

**Lua API:**
```lua
terminal.shell.set_env("KEY", "value")
terminal.shell.on_command_finish(function(exit_code, duration) end)
terminal.shell.set_ssh_term("xterm-256color")
```

**C++ changes needed:**
- `ShellIntegration`: add custom env var map, command finish callback, SSH TERM override

**Capability:** `PluginCapability::PaneWrite`

- [ ] Write tests for set_env, on_command_finish, set_ssh_term
- [ ] Implement `LuaShellModule`
- [ ] Run tests, verify all pass
- [ ] Commit

---

## Task 17: LuaWorkspaceModule — `terminal.workspace`

**Files:**
- Create: `core/src/lua_bindings/lua_workspace_module.h`
- Create: `core/src/lua_bindings/lua_workspace_module.cpp`
- Modify: `core/include/termcore/workspace_status.h`
- Create: `tests/test_lua_workspace_module.cpp`
- Modify: `tests/CMakeLists.txt`

**Lua API:**
```lua
terminal.workspace.on_status_change(function(snapshot) end)
terminal.workspace.set_cwd(workspace_id, "/path")
terminal.workspace.get_status()
```

**C++ changes needed:**
- `WorkspaceStatus`: add Lua status change callback slot

**Capability:** `PluginCapability::Events`

- [ ] Write tests for on_status_change, set_cwd, get_status
- [ ] Implement `LuaWorkspaceModule`
- [ ] Run tests, verify all pass
- [ ] Commit

---

## Task 18: LuaSettingsModule — `terminal.settings`

**Files:**
- Create: `core/src/lua_bindings/lua_settings_module.h`
- Create: `core/src/lua_bindings/lua_settings_module.cpp`
- Modify: `core/include/termcore/settings_model.h`
- Modify: `core/src/settings_model.cpp`
- Create: `tests/test_lua_settings_module.cpp`
- Modify: `tests/CMakeLists.txt`

**Lua API:**
```lua
terminal.settings.add_category("My Plugin", {
    {key="plugin.opt1", label="Option 1", type="toggle", default=true},
    {key="plugin.opt2", label="Option 2", type="dropdown", options={"a","b","c"}},
})
```

**C++ changes needed:**
- `SettingsModel`: add `addLuaCategory(name, fields)` to insert plugin categories into the settings UI

**Capability:** `PluginCapability::UI`

- [ ] Write tests for add_category with various field types
- [ ] Add Lua category storage to `SettingsModel`
- [ ] Implement `LuaSettingsModule`
- [ ] Run tests, verify all pass
- [ ] Commit

---

## Task 19: LuaViModule — `terminal.vi`

**Files:**
- Create: `core/src/lua_bindings/lua_vi_module.h`
- Create: `core/src/lua_bindings/lua_vi_module.cpp`
- Modify: `core/include/termcore/vi_copy_mode.h`
- Create: `tests/test_lua_vi_module.cpp`
- Modify: `tests/CMakeLists.txt`

**Lua API:**
```lua
terminal.vi.set_word_chars("a-zA-Z0-9_-")
terminal.vi.on_yank(function(text) end)
terminal.vi.map("gd", function() end)
```

**C++ changes needed:**
- `ViCopyMode`: add `setWordChars(string)`, yank callback, custom key mapping

**Capability:** `PluginCapability::Keybindings`

- [ ] Write tests for set_word_chars, on_yank, map
- [ ] Add customization points to `ViCopyMode`
- [ ] Implement `LuaViModule`
- [ ] Run tests, verify all pass
- [ ] Commit

---

## Task 20: LuaQuickModule — `terminal.quick`

**Files:**
- Create: `core/src/lua_bindings/lua_quick_module.h`
- Create: `core/src/lua_bindings/lua_quick_module.cpp`
- Modify: `core/include/termcore/quick_terminal.h`
- Create: `tests/test_lua_quick_module.cpp`
- Modify: `tests/CMakeLists.txt`

**Lua API:**
```lua
terminal.quick.set_animation("slide", {duration=200, easing="ease-out"})
terminal.quick.set_size(0.4)
terminal.quick.set_position("top")
```

**C++ changes needed:**
- `QuickTerminalConfig`: these may already map to config fields; add setters if not present

**Capability:** `PluginCapability::Config`

- [ ] Write tests for set_animation, set_size, set_position
- [ ] Implement `LuaQuickModule`
- [ ] Run tests, verify all pass
- [ ] Commit

---

## Task 21: Module Registration Wiring

> After all 20 modules are implemented, wire them into `TerminalController`.

**Files:**
- Modify: `core/include/termcore/terminal_controller.h`
- Modify: `core/src/terminal_controller.cpp`

**Changes:**
- Add `#include` for all 20 module headers
- Add `std::unique_ptr<LuaEngine> luaEngine_` member
- In `initTerminal()`, after all components are created:
  1. Create `LuaEngine`
  2. Create all 20 modules with component pointers
  3. Call `luaEngine_->registerModule()` for each
  4. Call `luaEngine_->initializeModules()`
  5. Load `config.lua`

- [ ] Add LuaEngine member to TerminalController
- [ ] Create and register all 20 modules in `initTerminal()`
- [ ] Build and verify no compilation errors
- [ ] Run full test suite
- [ ] Commit

---

## Task 22: Integration Test — Full Plugin Round-Trip

**Files:**
- Create: `tests/test_lua_plugin_integration.cpp`
- Modify: `tests/CMakeLists.txt`

Write a test that:
1. Creates `LuaEngine`
2. Registers multiple modules (tab, command, url — with nullptr components)
3. Loads a Lua script that uses multiple `terminal.*` APIs
4. Verifies sub-tables exist and functions are callable
5. Verifies `clearAllModules()` cleans everything up

- [ ] Write integration test
- [ ] Run test, verify passes
- [ ] Commit

---

## Parallel Execution Strategy

Tasks 1-20 are **fully independent** and can be executed by separate agents simultaneously:

| Agent Group | Tasks | Shared File Risk |
|---|---|---|
| A | 1 (tab complete), 2 (command), 3 (event) | None — different .h/.cpp files |
| B | 4 (theme), 5 (url), 6 (mux) | None |
| C | 7 (shader), 8 (search), 9 (clipboard) | None |
| D | 10 (paste), 11 (notify), 12 (status) | None |
| E | 13 (git), 14 (session), 15 (annotation) | None |
| F | 16 (shell), 17 (workspace), 18 (settings) | None |
| G | 19 (vi), 20 (quick) | None |

**Task 21 (wiring)** depends on all 20 modules being complete.
**Task 22 (integration test)** depends on Task 21.

Only `tests/CMakeLists.txt` is shared — each agent appends their test file. Resolve by having the wiring task (21) consolidate all test file additions.
