# Multi-Provider AI CLI Integration Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace BreadTerminal's Claude Code-only hook system with a universal provider framework supporting any AI CLI tool via hook scripts, OSC 7770 in-band protocol, and direct CLI.

**Architecture:** Provider metadata defined in Lua (`defaults/providers.lua`), parsed into `ProviderRegistry` C++ class. OSC 7770 adds a second communication channel through the existing HookBridge pipeline. Auto-detection triggers install notifications via NotificationStore.

**Tech Stack:** C++20, Lua (sol2), nlohmann/json, GoogleTest, CMake

**Spec:** `docs/superpowers/specs/2026-03-25-multi-provider-ai-cli-integration-design.md`

---

## Chunk 1: ProviderRegistry Core + Lua Binding

### Task 1: ProviderRegistry data structures and class

**Files:**
- Create: `core/include/termcore/provider_registry.h`
- Create: `core/src/provider_registry.cpp`
- Test: `tests/test_provider_registry.cpp`
- Modify: `core/CMakeLists.txt` — add `src/provider_registry.cpp` to TERMCORE_SOURCES

**Context:** This is the central registry that holds provider metadata. Other components query it for detection, hook installation, and UI display. It has no dependencies on Lua — Lua module populates it.

Existing patterns to follow:
- `AgentTracker` in `agent.h/cpp` — similar registry + detection pattern
- `NotificationStore` in `notification.h/cpp` — similar data store pattern

- [ ] **Step 1: Write test file with first test**

```cpp
// tests/test_provider_registry.cpp
#include <gtest/gtest.h>
#include "termcore/provider_registry.h"

using namespace termcore;

TEST(ProviderRegistryTest, StartsEmpty) {
    ProviderRegistry registry;
    EXPECT_TRUE(registry.all().empty());
}
```

- [ ] **Step 2: Write header with structs and class**

```cpp
// core/include/termcore/provider_registry.h
#pragma once

#include <set>
#include <string>
#include <vector>

namespace termcore {

struct ProviderEnvMapping {
    std::string bread_field;   // e.g. "agent_id"
    std::string tool_env_var;  // e.g. "CLAUDE_AGENT_ID"
};

struct ProviderHookEvent {
    std::string bread_event;   // e.g. "SubagentStart"
    std::string hook_name;     // e.g. "subagent-start" (tool's hook event name)
    std::vector<ProviderEnvMapping> env_map;
};

struct ProviderHooksConfig {
    std::string config_dir;       // e.g. "~/.claude"
    std::string settings_file;    // e.g. "settings.json"
    std::string settings_format;  // "json" (only JSON for now)
    std::vector<ProviderHookEvent> events;

    bool empty() const { return config_dir.empty(); }
};

struct ProviderInfo {
    std::string id;                              // e.g. "claude_code"
    std::string display_name;                    // e.g. "Claude Code"
    std::string agent_type;                      // maps to AgentType enum name
    std::vector<std::string> detect_process;     // process name substrings
    std::vector<std::string> detect_env;         // env var markers
    ProviderHooksConfig hooks;
};

class ProviderRegistry {
public:
    void registerProvider(ProviderInfo info);

    const ProviderInfo* findById(const std::string& id) const;
    const ProviderInfo* findByAgentType(const std::string& agent_type) const;
    const ProviderInfo* detect(const std::string& process_name,
                               const std::vector<std::string>& env_vars) const;

    const std::vector<ProviderInfo>& all() const { return providers_; }

    void markInstalled(const std::string& provider_id);
    bool isInstalled(const std::string& provider_id) const;

private:
    std::vector<ProviderInfo> providers_;
    std::set<std::string> installed_;
};

}  // namespace termcore
```

- [ ] **Step 3: Write minimal implementation**

```cpp
// core/src/provider_registry.cpp
#include "termcore/provider_registry.h"

#include <algorithm>

namespace termcore {

void ProviderRegistry::registerProvider(ProviderInfo info) {
    // Replace if same id exists
    for (auto& p : providers_) {
        if (p.id == info.id) {
            p = std::move(info);
            return;
        }
    }
    providers_.push_back(std::move(info));
}

const ProviderInfo* ProviderRegistry::findById(const std::string& id) const {
    for (const auto& p : providers_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

const ProviderInfo* ProviderRegistry::findByAgentType(
    const std::string& agent_type) const {
    for (const auto& p : providers_) {
        if (p.agent_type == agent_type) return &p;
    }
    return nullptr;
}

static bool containsIgnoreCase(const std::string& haystack,
                                const std::string& needle) {
    if (needle.empty()) return false;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char a, char b) { return std::tolower(a) == std::tolower(b); });
    return it != haystack.end();
}

const ProviderInfo* ProviderRegistry::detect(
    const std::string& process_name,
    const std::vector<std::string>& env_vars) const {
    for (const auto& p : providers_) {
        for (const auto& proc : p.detect_process) {
            if (containsIgnoreCase(process_name, proc))
                return &p;
        }
        for (const auto& marker : p.detect_env) {
            for (const auto& env : env_vars) {
                if (containsIgnoreCase(env, marker))
                    return &p;
            }
        }
    }
    return nullptr;
}

void ProviderRegistry::markInstalled(const std::string& provider_id) {
    installed_.insert(provider_id);
}

bool ProviderRegistry::isInstalled(const std::string& provider_id) const {
    return installed_.count(provider_id) > 0;
}

}  // namespace termcore
```

- [ ] **Step 4: Add to CMakeLists.txt**

In `core/CMakeLists.txt`, add `src/provider_registry.cpp` to the `TERMCORE_SOURCES` list (near the other source files like `src/agent_tree_tracker.cpp`).

- [ ] **Step 5: Write remaining tests**

Append to `tests/test_provider_registry.cpp`:

```cpp
TEST(ProviderRegistryTest, RegisterAndFindById) {
    ProviderRegistry registry;
    ProviderInfo info;
    info.id = "claude_code";
    info.display_name = "Claude Code";
    info.agent_type = "ClaudeCode";
    info.detect_process = {"claude"};
    registry.registerProvider(info);

    auto* found = registry.findById("claude_code");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->display_name, "Claude Code");
    EXPECT_EQ(registry.all().size(), 1u);
}

TEST(ProviderRegistryTest, FindByAgentType) {
    ProviderRegistry registry;
    ProviderInfo info;
    info.id = "codex";
    info.agent_type = "Codex";
    registry.registerProvider(info);

    EXPECT_NE(registry.findByAgentType("Codex"), nullptr);
    EXPECT_EQ(registry.findByAgentType("Unknown"), nullptr);
}

TEST(ProviderRegistryTest, DetectByProcessName) {
    ProviderRegistry registry;
    ProviderInfo info;
    info.id = "claude_code";
    info.detect_process = {"claude"};
    registry.registerProvider(info);

    EXPECT_NE(registry.detect("claude", {}), nullptr);
    EXPECT_NE(registry.detect("Claude-Code", {}), nullptr);
    EXPECT_EQ(registry.detect("python", {}), nullptr);
}

TEST(ProviderRegistryTest, DetectByEnvVar) {
    ProviderRegistry registry;
    ProviderInfo info;
    info.id = "codex";
    info.detect_env = {"CODEX_SESSION"};
    registry.registerProvider(info);

    EXPECT_NE(registry.detect("node", {"CODEX_SESSION=1"}), nullptr);
    EXPECT_EQ(registry.detect("node", {"OTHER=1"}), nullptr);
}

TEST(ProviderRegistryTest, InstallState) {
    ProviderRegistry registry;
    ProviderInfo info;
    info.id = "claude_code";
    registry.registerProvider(info);

    EXPECT_FALSE(registry.isInstalled("claude_code"));
    registry.markInstalled("claude_code");
    EXPECT_TRUE(registry.isInstalled("claude_code"));
}

TEST(ProviderRegistryTest, ReplaceExistingProvider) {
    ProviderRegistry registry;
    ProviderInfo info1;
    info1.id = "claude_code";
    info1.display_name = "Old Name";
    registry.registerProvider(info1);

    ProviderInfo info2;
    info2.id = "claude_code";
    info2.display_name = "New Name";
    registry.registerProvider(info2);

    EXPECT_EQ(registry.all().size(), 1u);
    EXPECT_EQ(registry.findById("claude_code")->display_name, "New Name");
}

TEST(ProviderRegistryTest, HooksConfigEmpty) {
    ProviderHooksConfig config;
    EXPECT_TRUE(config.empty());
    config.config_dir = "~/.claude";
    EXPECT_FALSE(config.empty());
}
```

- [ ] **Step 6: Add test to CMakeLists.txt**

In `tests/CMakeLists.txt`, add `test_provider_registry.cpp` to the `termcore_tests` source list.

- [ ] **Step 7: Build and run tests**

```bash
cmake --build build --config Debug
ctest --test-dir build -C Debug -R ProviderRegistry
```

Expected: All 7 tests pass.

- [ ] **Step 8: Commit**

```bash
git add core/include/termcore/provider_registry.h core/src/provider_registry.cpp \
        tests/test_provider_registry.cpp core/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: add ProviderRegistry for multi-provider AI CLI support"
```

---

### Task 2: Lua provider module (terminal.provider() binding)

**Files:**
- Create: `core/src/lua_bindings/lua_provider_module.h`
- Create: `core/src/lua_bindings/lua_provider_module.cpp`
- Modify: `core/src/lua_engine.cpp` — add `#include "default_providers_lua.h"` and entry in `kDefaultScripts[]`
- Modify: `core/CMakeLists.txt` — add `src/lua_bindings/lua_provider_module.cpp` to sources, add `providers` to `DEFAULT_LUA_NAMES`

**Context:** Follow the pattern from `lua_paste_module.h/cpp`. The module registers `terminal.provider(id, table)` which parses the Lua table into `ProviderInfo` and calls `ProviderRegistry::registerProvider()`.

- [ ] **Step 1: Write module header**

```cpp
// core/src/lua_bindings/lua_provider_module.h
#pragma once

#include "termcore/lua_module.h"

namespace termcore {

class ProviderRegistry;

class LuaProviderModule : public ILuaModule {
public:
    explicit LuaProviderModule(ProviderRegistry* registry);

    std::string_view moduleName() const override { return "provider"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Config;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override {}

private:
    ProviderRegistry* registry_;
};

}  // namespace termcore
```

- [ ] **Step 2: Write module implementation**

```cpp
// core/src/lua_bindings/lua_provider_module.cpp
#include "lua_provider_module.h"
#include "termcore/provider_registry.h"

#include <sol/sol.hpp>

namespace termcore {

LuaProviderModule::LuaProviderModule(ProviderRegistry* registry)
    : registry_(registry) {}

void LuaProviderModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);
    (void)lua;

    terminal.set_function("provider",
        [this](const std::string& id, sol::table opts) {
            if (!registry_) return;

            ProviderInfo info;
            info.id = id;
            info.display_name = opts.get_or<std::string>("display_name", id);
            info.agent_type = opts.get_or<std::string>("agent_type", "Unknown");

            // detect_process
            sol::optional<sol::table> procs = opts["detect_process"];
            if (procs) {
                for (auto& kv : *procs) {
                    if (kv.second.is<std::string>())
                        info.detect_process.push_back(kv.second.as<std::string>());
                }
            }

            // detect_env
            sol::optional<sol::table> envs = opts["detect_env"];
            if (envs) {
                for (auto& kv : *envs) {
                    if (kv.second.is<std::string>())
                        info.detect_env.push_back(kv.second.as<std::string>());
                }
            }

            // hooks
            sol::optional<sol::table> hooks = opts["hooks"];
            if (hooks) {
                info.hooks.config_dir =
                    hooks->get_or<std::string>("config_dir", "");
                info.hooks.settings_file =
                    hooks->get_or<std::string>("settings_file", "");
                info.hooks.settings_format =
                    hooks->get_or<std::string>("settings_format", "json");

                sol::optional<sol::table> events = (*hooks)["events"];
                if (events) {
                    for (auto& kv : *events) {
                        if (!kv.second.is<sol::table>()) continue;
                        sol::table evt = kv.second.as<sol::table>();
                        ProviderHookEvent he;
                        he.bread_event =
                            evt.get_or<std::string>("bread_event", "");
                        he.hook_name =
                            evt.get_or<std::string>("hook_name", "");

                        sol::optional<sol::table> emap = evt["env_map"];
                        if (emap) {
                            for (auto& m : *emap) {
                                if (m.first.is<std::string>() &&
                                    m.second.is<std::string>()) {
                                    he.env_map.push_back({
                                        m.first.as<std::string>(),
                                        m.second.as<std::string>()
                                    });
                                }
                            }
                        }
                        info.hooks.events.push_back(std::move(he));
                    }
                }
            }

            registry_->registerProvider(std::move(info));
        });
}

}  // namespace termcore
```

- [ ] **Step 3: Add source to CMakeLists.txt**

In `core/CMakeLists.txt`:
1. Add `src/lua_bindings/lua_provider_module.cpp` to `TERMCORE_SOURCES`
2. Add `providers` to the `DEFAULT_LUA_NAMES` list (after `themes`)

- [ ] **Step 4: Add include and kDefaultScripts entry in lua_engine.cpp**

In `core/src/lua_engine.cpp`:
1. Add `#include "default_providers_lua.h"` after the other default includes (line ~21)
2. Add `{"providers", default_providers_lua, default_providers_lua_len}` to `kDefaultScripts[]` array (after the themes entry)

- [ ] **Step 5: Register module in LuaEngine**

Find where other modules are registered (search for `registerModule` calls in `lua_engine.cpp` or `terminal_controller.cpp`). Add:

```cpp
luaEngine_->registerModule(
    std::make_shared<LuaProviderModule>(&providerRegistry_));
```

The `providerRegistry_` member needs to be added to whichever class owns the LuaEngine (likely `TerminalController`).

- [ ] **Step 6: Build and verify**

```bash
cmake --build build --config Debug
ctest --test-dir build -C Debug -R ProviderRegistry
```

All existing tests should still pass. The Lua module is loaded at runtime; its test coverage comes from providers.lua loading without errors.

- [ ] **Step 7: Commit**

```bash
git add core/src/lua_bindings/lua_provider_module.h \
        core/src/lua_bindings/lua_provider_module.cpp \
        core/src/lua_engine.cpp core/CMakeLists.txt
git commit -m "feat: add LuaProviderModule for terminal.provider() Lua binding"
```

---

### Task 3: Default providers.lua

**Files:**
- Create: `core/defaults/providers.lua`

**Context:** This file defines metadata for all 8 built-in AI CLI tools. Only Claude Code has fully specified hook events (the others will be filled in as their hook APIs become known). The file is embedded into the binary via CMake and loaded by LuaEngine::loadDefaults().

- [ ] **Step 1: Write providers.lua**

```lua
-- BreadTerminal default AI CLI provider definitions
-- Users can add custom providers in config.lua with terminal.provider()

terminal.provider("claude_code", {
    display_name = "Claude Code",
    agent_type = "ClaudeCode",
    detect_process = {"claude"},
    detect_env = {"CLAUDE_CODE_SESSION"},
    hooks = {
        config_dir = "~/.claude",
        settings_file = "settings.json",
        settings_format = "json",
        events = {
            {
                bread_event = "SubagentStart",
                hook_name = "subagent-start",
                env_map = {
                    agent_id = "CLAUDE_AGENT_ID",
                    agent_type = "CLAUDE_AGENT_TYPE",
                    description = "CLAUDE_AGENT_DESCRIPTION",
                },
            },
            {
                bread_event = "SubagentStop",
                hook_name = "subagent-stop",
                env_map = { agent_id = "CLAUDE_AGENT_ID" },
            },
            {
                bread_event = "Notification",
                hook_name = "notification",
                env_map = { body = "CLAUDE_NOTIFICATION_MESSAGE" },
            },
            {
                bread_event = "PostTool",
                hook_name = "post-tool",
                env_map = { tool_name = "CLAUDE_TOOL_NAME" },
            },
        },
    },
})

terminal.provider("codex", {
    display_name = "Codex",
    agent_type = "Codex",
    detect_process = {"codex"},
    detect_env = {"CODEX_SESSION"},
    hooks = {
        config_dir = "~/.codex",
        settings_file = "config.json",
        settings_format = "json",
        events = {},
    },
})

terminal.provider("gemini_cli", {
    display_name = "Gemini CLI",
    agent_type = "GeminiCli",
    detect_process = {"gemini"},
    detect_env = {"GEMINI_CLI"},
    hooks = {
        config_dir = "~/.gemini",
        settings_file = "settings.json",
        settings_format = "json",
        events = {},
    },
})

terminal.provider("aider", {
    display_name = "Aider",
    agent_type = "Aider",
    detect_process = {"aider"},
    detect_env = {},
})

terminal.provider("opencode", {
    display_name = "OpenCode",
    agent_type = "OpenCode",
    detect_process = {"opencode"},
    detect_env = {},
})

terminal.provider("goose", {
    display_name = "Goose",
    agent_type = "Goose",
    detect_process = {"goose"},
    detect_env = {},
})

terminal.provider("amp", {
    display_name = "Amp",
    agent_type = "Amp",
    detect_process = {"amp"},
    detect_env = {},
})

terminal.provider("cline", {
    display_name = "Cline",
    agent_type = "Cline",
    detect_process = {"cline"},
    detect_env = {},
})
```

- [ ] **Step 2: Build and verify Lua loads without error**

```bash
cmake --build build --config Debug
ctest --test-dir build -C Debug -R ProviderRegistry
```

The providers.lua is loaded via `LuaEngine::loadDefaults()`. If it has syntax errors, other Lua-dependent tests would fail.

- [ ] **Step 3: Commit**

```bash
git add core/defaults/providers.lua
git commit -m "feat: add default provider definitions for 8 AI CLI tools"
```

---

## Chunk 2: OSC 7770 Protocol

### Task 4: OSC 7770 handler in Screen

**Files:**
- Modify: `core/src/screen_osc.cpp` — add OSC 7770 dispatch in `onOscDispatch()` and handler method
- Modify: `core/include/termcore/screen.h` — add handler method declaration and hook_bridge callback
- Test: `tests/test_osc_7770.cpp`
- Modify: `tests/CMakeLists.txt` — add test file

**Context:** The Screen class already handles OSC 9/99/777 via `handleOscNotification()`. OSC 7770 follows the same dispatch pattern but routes JSON to a new callback (`osc_hook_callback_`) instead of the notification callback. The controller layer connects this callback to `HookBridge::processHookEvent()`.

Key design note: Screen does NOT have pane_id — it's injected at the controller layer when setting up the callback.

- [ ] **Step 1: Write test file**

```cpp
// tests/test_osc_7770.cpp
#include <gtest/gtest.h>
#include "termcore/screen.h"
#include "termcore/vt_parser.h"

#include <nlohmann/json.hpp>
#include <string>

using namespace termcore;

class Osc7770Test : public ::testing::Test {
protected:
    Screen screen{24, 80};
    VtParser parser;
    nlohmann::json last_event;
    bool callback_fired = false;

    void SetUp() override {
        screen.setOscHookCallback([this](const std::string& json_str) {
            last_event = nlohmann::json::parse(json_str, nullptr, false);
            callback_fired = true;
        });
        parser.setHandler(&screen);
    }

    void feed(const std::string& data) {
        parser.feed(data);
    }
};

TEST_F(Osc7770Test, StateChangeEvent) {
    feed("\033]7770;{\"event\":\"StateChange\",\"state\":\"thinking\"}\033\\");
    ASSERT_TRUE(callback_fired);
    EXPECT_EQ(last_event["event"], "StateChange");
    EXPECT_EQ(last_event["state"], "thinking");
}

TEST_F(Osc7770Test, NotificationEvent) {
    feed("\033]7770;{\"event\":\"Notification\",\"title\":\"Done\",\"body\":\"OK\"}\033\\");
    ASSERT_TRUE(callback_fired);
    EXPECT_EQ(last_event["event"], "Notification");
    EXPECT_EQ(last_event["title"], "Done");
}

TEST_F(Osc7770Test, MalformedJsonIgnored) {
    feed("\033]7770;not json at all\033\\");
    EXPECT_FALSE(callback_fired);
}

TEST_F(Osc7770Test, EmptyPayloadIgnored) {
    feed("\033]7770;\033\\");
    EXPECT_FALSE(callback_fired);
}

TEST_F(Osc7770Test, BellTerminator) {
    feed("\033]7770;{\"event\":\"StateChange\",\"state\":\"idle\"}\007");
    ASSERT_TRUE(callback_fired);
    EXPECT_EQ(last_event["state"], "idle");
}
```

- [ ] **Step 2: Add callback and handler to screen.h**

In `core/include/termcore/screen.h`, add near the other callback types:

```cpp
using OscHookCallback = std::function<void(const std::string&)>;
void setOscHookCallback(OscHookCallback cb) { osc_hook_callback_ = std::move(cb); }
```

Add to private members:
```cpp
OscHookCallback osc_hook_callback_;
```

Add handler declaration near the other OSC handlers:
```cpp
void handleOscHookEvent(const std::string& str);
```

- [ ] **Step 3: Implement OSC 7770 handler in screen_osc.cpp**

In `screen_osc.cpp`, add the handler:

```cpp
void Screen::handleOscHookEvent(const std::string& str) {
    if (!osc_hook_callback_ || str.empty()) return;
    // Validate it looks like JSON before forwarding
    if (str.front() != '{') return;
    osc_hook_callback_(str);
}
```

In the `onOscDispatch()` switch/if-chain, add a case for 7770:

```cpp
case 7770:
    handleOscHookEvent(osc_string);
    break;
```

- [ ] **Step 4: Add test to CMakeLists.txt**

Add `test_osc_7770.cpp` to the `termcore_tests` source list in `tests/CMakeLists.txt`.

- [ ] **Step 5: Build and run tests**

```bash
cmake --build build --config Debug
ctest --test-dir build -C Debug -R Osc7770
```

Expected: All 5 tests pass.

- [ ] **Step 6: Commit**

```bash
git add core/include/termcore/screen.h core/src/screen_osc.cpp \
        tests/test_osc_7770.cpp tests/CMakeLists.txt
git commit -m "feat: add OSC 7770 handler for in-band AI CLI communication"
```

---

### Task 5: PaneEnvironment additions + HookBridge wiring

**Files:**
- Modify: `core/include/termcore/pane_environment.h` — add `BREADTERMINAL_OSC_CHANNEL` and `BREADTERMINAL_VERSION`
- Modify: controller layer (where Screen's osc_hook_callback is connected) — wire OSC 7770 → HookBridge with pane_id injection

**Context:** Environment variable advertising lets tools discover the OSC channel at runtime. The controller layer connects Screen's new callback to HookBridge, injecting the pane_id that Screen doesn't know about.

- [ ] **Step 1: Add env vars to PaneEnvironment**

In `core/include/termcore/pane_environment.h`, in `toEnvVars()`:

```cpp
vars.emplace_back("BREADTERMINAL_OSC_CHANNEL", "7770");
vars.emplace_back("BREADTERMINAL_VERSION", "0.1.0");
```

Add these after the existing `BREADTERMINAL_PANE_ID` block, before the TMUX compat block.

- [ ] **Step 2: Wire OSC 7770 to HookBridge in controller**

Find where `Screen::setNotificationCallback()` is called (likely in `terminal_controller.cpp` or the pane creation code). Add a similar setup:

```cpp
screen->setOscHookCallback([this, pane_id](const std::string& json_str) {
    auto event = nlohmann::json::parse(json_str, nullptr, false);
    if (event.is_discarded()) return;
    // Inject pane_id if not present
    if (!event.contains("pane_id")) {
        event["pane_id"] = pane_id;
    }
    hookBridge_.processHookEvent(event);
});
```

- [ ] **Step 3: Build and verify**

```bash
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

All existing + new tests should pass.

- [ ] **Step 4: Commit**

```bash
git add core/include/termcore/pane_environment.h
git commit -m "feat: advertise OSC 7770 channel via env vars and wire to HookBridge"
```

---

## Chunk 3: bread CLI Extensions

### Task 6: bread osc emit command

**Files:**
- Create: `tools/bread/osc_emitter.h`
- Create: `tools/bread/osc_emitter.cpp`
- Modify: `tools/bread/arg_parser.cpp` — add `osc` subcommand parsing
- Modify: `tools/bread/arg_parser.h` — add `LocalCmd::OscEmit`
- Modify: `tools/bread/main.cpp` — handle `LocalCmd::OscEmit`
- Modify: `tools/bread/CMakeLists.txt` — add `osc_emitter.cpp`
- Test: `tests/test_osc_emitter.cpp`
- Modify: `tests/CMakeLists.txt`

**Context:** `bread osc emit` is a LOCAL command (no socket needed). It outputs OSC 7770 escape sequences to stdout. This allows any script or tool to send events to BreadTerminal by running a simple command.

- [ ] **Step 1: Write header**

```cpp
// tools/bread/osc_emitter.h
#pragma once

#include <string>

namespace bread {

/// Emit an OSC 7770 event to stdout.
/// Returns 0 on success, 1 on error.
int emitOsc(int argc, char* argv[]);

/// Build OSC 7770 escape sequence for a JSON payload.
std::string buildOscSequence(const std::string& json_payload);

}  // namespace bread
```

- [ ] **Step 2: Write implementation**

```cpp
// tools/bread/osc_emitter.cpp
#include "osc_emitter.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

namespace bread {

std::string buildOscSequence(const std::string& json_payload) {
    return "\033]7770;" + json_payload + "\033\\";
}

int emitOsc(int argc, char* argv[]) {
    if (argc < 1) {
        std::cerr << "Usage: bread osc emit <type> [options]\n"
                  << "Types: state-change, notify, subagent-start, subagent-stop, raw\n";
        return 1;
    }

    std::string type = argv[0];
    nlohmann::json event;

    auto getOpt = [&](const std::string& flag) -> std::string {
        for (int i = 1; i < argc - 1; ++i) {
            if (std::string(argv[i]) == flag)
                return argv[i + 1];
        }
        return "";
    };

    if (type == "state-change") {
        event["event"] = "StateChange";
        auto state = getOpt("--state");
        if (state.empty()) {
            std::cerr << "Error: --state required\n";
            return 1;
        }
        event["state"] = state;
        auto aid = getOpt("--agent-id");
        if (!aid.empty()) event["agent_id"] = aid;

    } else if (type == "notify") {
        event["event"] = "Notification";
        auto title = getOpt("--title");
        auto body = getOpt("--body");
        if (title.empty() && body.empty()) {
            std::cerr << "Error: --title or --body required\n";
            return 1;
        }
        if (!title.empty()) event["title"] = title;
        if (!body.empty()) event["body"] = body;
        auto urgency = getOpt("--urgency");
        if (!urgency.empty()) event["urgency"] = urgency;

    } else if (type == "subagent-start") {
        event["event"] = "SubagentStart";
        auto aid = getOpt("--agent-id");
        if (aid.empty()) {
            std::cerr << "Error: --agent-id required\n";
            return 1;
        }
        event["agent_id"] = aid;
        auto at = getOpt("--agent-type");
        if (!at.empty()) event["agent_type"] = at;
        auto desc = getOpt("--description");
        if (!desc.empty()) event["description"] = desc;

    } else if (type == "subagent-stop") {
        event["event"] = "SubagentStop";
        auto aid = getOpt("--agent-id");
        if (aid.empty()) {
            std::cerr << "Error: --agent-id required\n";
            return 1;
        }
        event["agent_id"] = aid;

    } else if (type == "raw") {
        if (argc < 2) {
            std::cerr << "Error: raw JSON argument required\n";
            return 1;
        }
        auto parsed = nlohmann::json::parse(argv[1], nullptr, false);
        if (parsed.is_discarded()) {
            std::cerr << "Error: invalid JSON\n";
            return 1;
        }
        event = parsed;

    } else {
        std::cerr << "Unknown osc emit type: " << type << "\n";
        return 1;
    }

    std::cout << buildOscSequence(event.dump());
    std::cout.flush();
    return 0;
}

}  // namespace bread
```

- [ ] **Step 3: Add LocalCmd::OscEmit to arg_parser.h**

```cpp
enum class LocalCmd {
    None,
    HooksInstall,
    Identify,
    Capabilities,
    GetText,
    OscEmit,       // NEW
    HooksStatus,   // NEW (for Task 8)
};
```

- [ ] **Step 4: Add osc subcommand parsing in arg_parser.cpp**

In `parseArgs()`, before the `hooks` check, add:

```cpp
if (positional[0] == "osc") {
    if (positional.size() >= 2 && positional[1] == "emit") {
        result.type = CommandType::LocalCommand;
        result.local_cmd = LocalCmd::OscEmit;
        // Pass remaining args to emitOsc
        result.valid = true;
        return result;
    }
    result.error = "Unknown osc subcommand. Try: bread osc emit <type>";
    return result;
}
```

- [ ] **Step 5: Handle OscEmit in main.cpp**

In `main.cpp`, where LocalCmd cases are handled:

```cpp
case LocalCmd::OscEmit:
    return bread::emitOsc(argc - 3, argv + 3);  // skip "bread osc emit"
```

- [ ] **Step 6: Add to CMakeLists.txt**

Add `osc_emitter.cpp` to `tools/bread/CMakeLists.txt`.

- [ ] **Step 7: Write tests**

```cpp
// tests/test_osc_emitter.cpp
#include <gtest/gtest.h>
#include "osc_emitter.h"

#include <nlohmann/json.hpp>

using namespace bread;

TEST(OscEmitterTest, BuildOscSequence) {
    auto seq = buildOscSequence("{\"event\":\"test\"}");
    EXPECT_EQ(seq, "\033]7770;{\"event\":\"test\"}\033\\");
}

TEST(OscEmitterTest, BuildOscSequenceEmpty) {
    auto seq = buildOscSequence("{}");
    EXPECT_EQ(seq, "\033]7770;{}\033\\");
}
```

Add `test_osc_emitter.cpp` to a new test executable in `tests/CMakeLists.txt` that links the bread osc_emitter object (or add to an existing bread test target).

- [ ] **Step 8: Build and test**

```bash
cmake --build build --config Debug
ctest --test-dir build -C Debug -R OscEmitter
```

- [ ] **Step 9: Commit**

```bash
git add tools/bread/osc_emitter.h tools/bread/osc_emitter.cpp \
        tools/bread/arg_parser.h tools/bread/arg_parser.cpp \
        tools/bread/main.cpp tools/bread/CMakeLists.txt \
        tests/test_osc_emitter.cpp tests/CMakeLists.txt
git commit -m "feat: add 'bread osc emit' command for OSC 7770 event output"
```

---

### Task 7: Universal hook installer refactoring

**Files:**
- Modify: `tools/bread/hooks_installer.h` — update function signatures
- Modify: `tools/bread/hooks_installer.cpp` — refactor to use ProviderRegistry metadata
- Modify: `tools/bread/arg_parser.cpp` — `bread hooks install --provider <id>` parsing
- Modify: `tools/bread/main.cpp` — pass provider arg to installer

**Context:** Replace the hardcoded Claude Code hook scripts with a generic generator that reads `ProviderHooksConfig` from ProviderRegistry. The script template uses the env_map to translate tool-specific environment variables to BreadTerminal's JSON format.

- [ ] **Step 1: Update hooks_installer.h**

```cpp
// tools/bread/hooks_installer.h
#pragma once

#include "termcore/provider_registry.h"
#include <string>

namespace bread {

/// Install hooks for a specific provider using its metadata.
int installHooksForProvider(const termcore::ProviderInfo& provider);

/// Install hooks for all providers that have hook configs.
int installAllHooks(const termcore::ProviderRegistry& registry);

/// Show installation status for all providers.
int showHooksStatus(const termcore::ProviderRegistry& registry);

/// Legacy: install Claude Code hooks (calls installHooksForProvider internally).
int installHooks();

}  // namespace bread
```

- [ ] **Step 2: Rewrite hooks_installer.cpp**

The core function `installHooksForProvider()` should:

1. Resolve `config_dir` (expand `~` to `$HOME` or `%USERPROFILE%`)
2. Create `<config_dir>/hooks/` directory
3. For each event in `provider.hooks.events`:
   - Generate bash script that maps env vars to `bread hook-event --json '{...}'`
   - Script template:
     ```bash
     #!/bin/bash
     bread hook-event --json '{"event":"<bread_event>","<field>":"'"$<ENV_VAR>"'",...,"pane_id":"'"$BREADTERMINAL_PANE_ID"'"}'
     ```
4. Update settings file (JSON format) to register hooks
5. Print status

The env_map entries are iterated to build the JSON fields dynamically.

Keep the existing `installHooks()` function as a wrapper that calls `installHooksForProvider()` with the claude_code provider info (for backwards compatibility).

- [ ] **Step 3: Update arg_parser.cpp for --provider flag**

```cpp
if (positional[0] == "hooks") {
    if (positional.size() >= 2 && positional[1] == "install") {
        result.type = CommandType::LocalCommand;
        result.local_cmd = LocalCmd::HooksInstall;
        // Extract --provider flag
        if (flags.count("provider")) {
            result.params = {{"provider", flags["provider"]}};
        }
        result.valid = true;
        return result;
    }
    if (positional.size() >= 2 && positional[1] == "status") {
        result.type = CommandType::LocalCommand;
        result.local_cmd = LocalCmd::HooksStatus;
        result.valid = true;
        return result;
    }
    // ...
}
```

- [ ] **Step 4: Build and verify**

```bash
cmake --build build --config Debug
```

No new tests for this task (hook installation requires filesystem interaction — tested manually or with integration tests).

- [ ] **Step 5: Commit**

```bash
git add tools/bread/hooks_installer.h tools/bread/hooks_installer.cpp \
        tools/bread/arg_parser.cpp tools/bread/main.cpp
git commit -m "feat: refactor hook installer to use ProviderRegistry metadata"
```

---

## Chunk 4: Auto-Detection and Notification

### Task 8: Auto-detection notification flow

**Files:**
- Modify: `core/src/agent.cpp` — after detecting an agent, check ProviderRegistry and fire install notification
- Modify: where `AgentTracker` is constructed — pass `ProviderRegistry*` and `NotificationStore*` references

**Context:** When AgentTracker detects a new tool in a pane, it queries ProviderRegistry: does this provider have hooks? Are they installed? If not, it adds a System notification prompting installation.

- [ ] **Step 1: Add ProviderRegistry and NotificationStore references to AgentTracker**

In `core/include/termcore/agent.h`, add:

```cpp
void setProviderRegistry(ProviderRegistry* registry) { provider_registry_ = registry; }
void setNotificationStore(NotificationStore* store) { notification_store_ = store; }
```

Add to private:
```cpp
ProviderRegistry* provider_registry_ = nullptr;
NotificationStore* notification_store_ = nullptr;
std::set<std::string> notified_providers_;  // prevent duplicate notifications
```

- [ ] **Step 2: Add notification logic after detection**

In `core/src/agent.cpp`, in `reportStart()` (or wherever the agent is first detected for a pane), after setting the agent info, add:

```cpp
// Check if provider has uninstalled hooks
if (provider_registry_ && notification_store_) {
    auto agent_type_str = agentTypeToString(type);
    auto* provider = provider_registry_->findByAgentType(agent_type_str);
    if (provider && !provider->hooks.empty()
        && !provider_registry_->isInstalled(provider->id)
        && notified_providers_.count(provider->id) == 0) {
        notified_providers_.insert(provider->id);
        notification_store_->add(
            pane_id,
            NotificationSource::System,
            NotificationUrgency::Critical,
            provider->display_name + " detected",
            "Install hooks for subagent tracking and notifications: "
            "bread hooks install --provider " + provider->id);
    }
}
```

- [ ] **Step 3: Wire references in controller**

Where `AgentTracker` is initialized (likely `TerminalController` constructor or init), add:

```cpp
agentTracker_.setProviderRegistry(&providerRegistry_);
agentTracker_.setNotificationStore(&notificationStore_);
```

- [ ] **Step 4: Build and verify**

```bash
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

All tests should pass. The notification logic is tested by verifying that when an agent is reported, a notification appears.

- [ ] **Step 5: Commit**

```bash
git add core/include/termcore/agent.h core/src/agent.cpp
git commit -m "feat: auto-detect AI CLI tools and notify for hook installation"
```

---

### Task 9: bread hooks status command

**Files:**
- Modify: `tools/bread/hooks_installer.cpp` — implement `showHooksStatus()`
- Modify: `tools/bread/main.cpp` — handle `LocalCmd::HooksStatus`

**Context:** `bread hooks status` shows which providers are registered and whether their hooks are installed. This helps users understand what's available.

- [ ] **Step 1: Implement showHooksStatus()**

```cpp
int showHooksStatus(const termcore::ProviderRegistry& registry) {
    auto& providers = registry.all();
    if (providers.empty()) {
        std::cout << "No providers registered.\n";
        return 0;
    }

    std::cout << "AI CLI Provider Status:\n\n";
    for (const auto& p : providers) {
        std::cout << "  " << p.display_name;
        if (p.hooks.empty()) {
            std::cout << " — OSC channel only (no hook system)\n";
        } else if (registry.isInstalled(p.id)) {
            std::cout << " — hooks installed ✓\n";
        } else {
            std::cout << " — hooks not installed"
                      << " (run: bread hooks install --provider " << p.id << ")\n";
        }
    }
    return 0;
}
```

- [ ] **Step 2: Handle in main.cpp**

```cpp
case LocalCmd::HooksStatus:
    return bread::showHooksStatus(/* pass registry */);
```

Note: This requires access to the ProviderRegistry. Since `bread hooks status` is a local command, the registry needs to be populated from providers.lua at CLI startup. This can be done by creating a minimal LuaEngine instance that loads only providers.lua, or by reading a cached providers list from a known path. The simplest approach: load providers.lua in the CLI tool directly.

- [ ] **Step 3: Build and verify**

```bash
cmake --build build --config Debug
bread hooks status
```

Expected output shows all 8 providers with their installation status.

- [ ] **Step 4: Commit**

```bash
git add tools/bread/hooks_installer.cpp tools/bread/main.cpp
git commit -m "feat: add 'bread hooks status' command showing provider installation state"
```

---

## Parallel Agent Assignment

Tasks can be parallelized as follows:

| Agent | Tasks | Files touched |
|-------|-------|---------------|
| Agent 1 | Task 1 + Task 2 + Task 3 | core/ (provider_registry, lua_bindings, defaults) |
| Agent 2 | Task 4 + Task 5 | core/ (screen_osc, screen.h, pane_environment) |
| Agent 3 | Task 6 | tools/bread/ (osc_emitter), tests/ |
| Agent 4 | Task 7 + Task 9 | tools/bread/ (hooks_installer, arg_parser, main) |
| Agent 5 | Task 8 | core/ (agent.h, agent.cpp) — depends on Agent 1 completing |

Agent 5 depends on Agent 1 (needs ProviderRegistry to exist). All others are independent.
