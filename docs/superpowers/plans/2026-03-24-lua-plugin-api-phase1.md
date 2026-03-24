# Lua Plugin API — Phase 1: Registration Infrastructure

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the modular Lua binding registration infrastructure so that Phase 2 agents can independently implement 20 binding modules without touching shared code.

**Architecture:** Add `ILuaModule` interface to termcore. Extend `LuaEngine` to accept module registrations and call `registerBindings()` during initialization. Each module creates its own sub-table under the `terminal` global (e.g., `terminal.tab`). Existing APIs remain unchanged.

**Tech Stack:** C++17, sol2 (sol3), Lua 5.4, Google Test

**Spec:** `docs/superpowers/specs/2026-03-24-lua-plugin-api-design.md`

---

## File Structure

| Action | File | Responsibility |
|--------|------|----------------|
| Create | `core/include/termcore/lua_module.h` | `ILuaModule` interface + `PluginCapability` integration |
| Modify | `core/include/termcore/lua_engine.h` | Add `registerModule()`, `initializeModules()`, module storage |
| Modify | `core/src/lua_engine.cpp` | Implement module registration and binding initialization |
| Modify | `core/include/termcore/plugin.h` | Add UI, Clipboard capabilities |
| Modify | `core/CMakeLists.txt` | Add `lua_bindings/` sources and include path |
| Create | `core/src/lua_bindings/lua_tab_module.h` | Sample module header (Phase 1 validation) |
| Create | `core/src/lua_bindings/lua_tab_module.cpp` | Sample module impl (Phase 1 validation) |
| Modify | `core/include/termcore/tab_controller.h` | Add `TitleFormatFn` callback slot |
| Modify | `core/src/tab_controller.cpp` | Invoke callback in `tabBarInfo()` |
| Modify | `tests/test_lua_engine.cpp` | Add module registration tests |

---

## Chunk 1: ILuaModule Interface and LuaEngine Extension

### Task 1: Create ILuaModule interface and add new PluginCapability values

**Files:**
- Create: `core/include/termcore/lua_module.h`
- Modify: `core/include/termcore/plugin.h`

- [ ] **Step 1: Add UI and Clipboard capabilities to plugin.h**

In `core/include/termcore/plugin.h`, add to `PluginCapability` enum after `FileSystem`:

```cpp
    UI,          // Extend settings UI
    Clipboard,   // Access clipboard history and paste guard
```

- [ ] **Step 2: Write the ILuaModule header**

```cpp
// core/include/termcore/lua_module.h
#pragma once

#include <memory>
#include <string_view>
#include <string>
#include "termcore/plugin.h"

namespace termcore {

class ILuaModule {
public:
    virtual ~ILuaModule() = default;

    // Short name used as sub-table key (e.g. "tab" -> terminal.tab)
    virtual std::string_view moduleName() const = 0;

    // Minimum capability a plugin must declare to access this module.
    // Config context (config.lua) bypasses this check.
    virtual PluginCapability requiredCapability() const = 0;

    // Called once during LuaEngine::initializeModules().
    // Implementations create terminal.<moduleName> sub-table and register functions.
    // Parameters use void* to avoid sol.hpp in public header:
    //   luaState  -- pointer to sol::state
    //   terminal  -- pointer to sol::table (the "terminal" global)
    virtual void registerBindings(void* luaState, void* terminalTable) = 0;

    // Called on plugin unload or engine shutdown.
    // Implementations must clear all stored sol::protected_function references
    // and reset any callback slots on the backing C++ component.
    virtual void clearCallbacks() = 0;
};

} // namespace termcore
```

- [ ] **Step 3: Verify the header compiles**

Run: `cd D:/Projects/BreadTerminal/build && cmake --build . --target termcore 2>&1 | tail -5`
Expected: Successful build (header is not yet included anywhere)

- [ ] **Step 4: Commit**

```bash
git add core/include/termcore/lua_module.h core/include/termcore/plugin.h
git commit -m "feat: add ILuaModule interface and new plugin capabilities"
```

---

### Task 2: Extend LuaEngine to support module registration

**Files:**
- Modify: `core/include/termcore/lua_engine.h`
- Modify: `core/src/lua_engine.cpp`
- Modify: `tests/test_lua_engine.cpp`

- [ ] **Step 1: Write failing test — registerModule + sub-table creation**

In `tests/test_lua_engine.cpp`, add these includes right after `#include <termcore/lua_engine.h>` (inside the `#else` block):

```cpp
#include <termcore/lua_module.h>
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
```

Add before the `#endif` at end of file:

```cpp
// 11. Module registration creates sub-table
TEST(LuaEngine, RegisterModuleCreatesSubTable) {
    LuaEngine engine;
    class TestModule : public termcore::ILuaModule {
    public:
        std::string greeting;
        std::string_view moduleName() const override { return "test_mod"; }
        termcore::PluginCapability requiredCapability() const override {
            return termcore::PluginCapability::Events;
        }
        void registerBindings(void* luaState, void* terminalTable) override {
            auto& terminal = *static_cast<sol::table*>(terminalTable);
            auto tbl = terminal.create_named("test_mod");
            tbl.set_function("greet", [this](const std::string& name) {
                greeting = "hello " + name;
            });
        }
        void clearCallbacks() override { greeting.clear(); }
    };

    auto mod = std::make_shared<TestModule>();
    engine.registerModule(mod);
    engine.initializeModules();

    EXPECT_TRUE(engine.loadString("terminal.test_mod.greet('world')").ok());
    EXPECT_EQ(mod->greeting, "hello world");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd D:/Projects/BreadTerminal/build && cmake --build . --target termcore_tests 2>&1 | tail -10`
Expected: FAIL — `registerModule` and `initializeModules` not declared

- [ ] **Step 3: Add registerModule/initializeModules to LuaEngine header**

In `core/include/termcore/lua_engine.h`, add after `#include "termcore/result.h"`:

```cpp
#include "termcore/lua_module.h"
```

Add these public methods after `isValid()`:

```cpp
    // Module registration (call before initializeModules)
    void registerModule(std::shared_ptr<ILuaModule> module);

    // Initialize all registered modules (config.lua context — all modules enabled).
    void initializeModules();

    // Initialize modules filtered by capabilities (plugin context).
    void initializeModules(const std::vector<PluginCapability>& capabilities);

    // Clear all module callbacks and remove modules.
    // Called during shutdown before sol::state is destroyed.
    void clearAllModules();
```

Add private member after `std::vector<std::string> loaded_plugins_;`:

```cpp
    std::vector<std::shared_ptr<ILuaModule>> modules_;
```

- [ ] **Step 4: Implement all module methods in lua_engine.cpp**

Add at end of `lua_engine.cpp` (before closing `} // namespace termcore`):

```cpp
void LuaEngine::registerModule(std::shared_ptr<ILuaModule> module) {
    modules_.push_back(std::move(module));
}

void LuaEngine::initializeModules() {
    for (auto& mod : modules_) {
        mod->registerBindings(
            static_cast<void*>(&impl_->lua),
            static_cast<void*>(&impl_->terminal_table));
    }
}

void LuaEngine::initializeModules(const std::vector<PluginCapability>& capabilities) {
    for (auto& mod : modules_) {
        auto required = mod->requiredCapability();
        bool allowed = false;
        for (auto cap : capabilities) {
            if (cap == required) { allowed = true; break; }
        }
        if (allowed) {
            mod->registerBindings(
                static_cast<void*>(&impl_->lua),
                static_cast<void*>(&impl_->terminal_table));
        }
    }
}

void LuaEngine::clearAllModules() {
    for (auto& mod : modules_) {
        mod->clearCallbacks();
    }
    modules_.clear();
}
```

Update `LuaEngine::~LuaEngine()`:

```cpp
LuaEngine::~LuaEngine() {
    clearAllModules();
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cd D:/Projects/BreadTerminal/build && cmake --build . --target termcore_tests && cd D:/Projects/BreadTerminal/build && ctest -R LuaEngine --output-on-failure`
Expected: All LuaEngine tests PASS including `RegisterModuleCreatesSubTable`

- [ ] **Step 6: Commit**

```bash
git add core/include/termcore/lua_engine.h core/src/lua_engine.cpp tests/test_lua_engine.cpp
git commit -m "feat: add module registration, initialization, and cleanup to LuaEngine"
```

---

### Task 3: Test clearCallbacks and capability filtering

**Files:**
- Modify: `tests/test_lua_engine.cpp`

- [ ] **Step 1: Write clearCallbacks test**

Add before `#endif`:

```cpp
// 12. clearAllModules clears callbacks
TEST(LuaEngine, ClearAllModulesCallsClearCallbacks) {
    LuaEngine engine;
    class TrackModule : public termcore::ILuaModule {
    public:
        bool cleared = false;
        std::string_view moduleName() const override { return "track"; }
        termcore::PluginCapability requiredCapability() const override {
            return termcore::PluginCapability::Events;
        }
        void registerBindings(void* luaState, void* terminalTable) override {}
        void clearCallbacks() override { cleared = true; }
    };

    auto mod = std::make_shared<TrackModule>();
    engine.registerModule(mod);
    engine.initializeModules();
    engine.clearAllModules();
    EXPECT_TRUE(mod->cleared);
}

// 13. initializeModules with capability filter
TEST(LuaEngine, InitializeModulesCapabilityFilter) {
    LuaEngine engine;

    class AllowedModule : public termcore::ILuaModule {
    public:
        bool registered = false;
        std::string_view moduleName() const override { return "allowed"; }
        termcore::PluginCapability requiredCapability() const override {
            return termcore::PluginCapability::Events;
        }
        void registerBindings(void* luaState, void* terminalTable) override {
            registered = true;
        }
        void clearCallbacks() override {}
    };

    class DeniedModule : public termcore::ILuaModule {
    public:
        bool registered = false;
        std::string_view moduleName() const override { return "denied"; }
        termcore::PluginCapability requiredCapability() const override {
            return termcore::PluginCapability::FileSystem;
        }
        void registerBindings(void* luaState, void* terminalTable) override {
            registered = true;
        }
        void clearCallbacks() override {}
    };

    auto allowed = std::make_shared<AllowedModule>();
    auto denied = std::make_shared<DeniedModule>();
    engine.registerModule(allowed);
    engine.registerModule(denied);

    std::vector<termcore::PluginCapability> caps = {termcore::PluginCapability::Events};
    engine.initializeModules(caps);

    EXPECT_TRUE(allowed->registered);
    EXPECT_FALSE(denied->registered);
}
```

- [ ] **Step 2: Run tests to verify all pass**

Run: `cd D:/Projects/BreadTerminal/build && cmake --build . --target termcore_tests && cd D:/Projects/BreadTerminal/build && ctest -R LuaEngine --output-on-failure`
Expected: All 13 tests PASS

- [ ] **Step 3: Commit**

```bash
git add tests/test_lua_engine.cpp
git commit -m "test: add clearAllModules and capability filtering tests"
```

---

## Chunk 2: CMake, Sample Module (Tab), and Integration Tests

### Task 4: Add lua_bindings to CMakeLists.txt

**Files:**
- Modify: `core/CMakeLists.txt`

- [ ] **Step 1: Add lua_bindings sources and include path**

In `core/CMakeLists.txt`, find the line `list(APPEND TERMCORE_SOURCES src/lua_engine.cpp)` (inside the `if(TERMCORE_HAS_LUA)` block). Add immediately after it:

```cmake
    # Lua binding modules (auto-discovered)
    file(GLOB LUA_BINDING_SOURCES src/lua_bindings/*.cpp)
    list(APPEND TERMCORE_SOURCES ${LUA_BINDING_SOURCES})
```

Find the `if(TERMCORE_HAS_LUA)` block that has `target_include_directories` and `target_link_libraries`. Add within that block:

```cmake
    target_include_directories(termcore PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
```

- [ ] **Step 2: Create the lua_bindings directory**

Run: `mkdir -p D:/Projects/BreadTerminal/core/src/lua_bindings`

- [ ] **Step 3: Build to verify CMake changes**

Run: `cd D:/Projects/BreadTerminal/build && cmake .. && cmake --build . --target termcore 2>&1 | tail -5`
Expected: Successful build (no sources in lua_bindings/ yet, glob finds nothing)

- [ ] **Step 4: Commit**

```bash
git add core/CMakeLists.txt
git commit -m "build: add lua_bindings directory to CMake with auto-discovery"
```

---

### Task 5: Create LuaTabModule as validation sample

**Files:**
- Create: `core/src/lua_bindings/lua_tab_module.h`
- Create: `core/src/lua_bindings/lua_tab_module.cpp`

- [ ] **Step 1: Create LuaTabModule header**

```cpp
// core/src/lua_bindings/lua_tab_module.h
#pragma once

#include "termcore/lua_module.h"
#include <functional>
#include <string>

namespace termcore {

class TabController;

// Info struct passed to Lua title format callback
struct TabTitleInfo {
    int tab_index = 0;
    std::string process_name;
    std::string working_dir;
    std::string title;
    bool is_active = false;
};

class LuaTabModule : public ILuaModule {
public:
    explicit LuaTabModule(TabController* tabCtrl);

    std::string_view moduleName() const override { return "tab"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Events;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

    // Called by TabController to format a tab title via Lua
    using TitleFormatFn = std::function<std::string(const TabTitleInfo&)>;
    TitleFormatFn titleFormatCallback() const { return titleFormatFn_; }

private:
    TabController* tabCtrl_;
    TitleFormatFn titleFormatFn_;
};

} // namespace termcore
```

- [ ] **Step 2: Create LuaTabModule implementation**

```cpp
// core/src/lua_bindings/lua_tab_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_tab_module.h"
#include "termcore/tab_controller.h"

namespace termcore {

LuaTabModule::LuaTabModule(TabController* tabCtrl)
    : tabCtrl_(tabCtrl) {}

void LuaTabModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    auto& terminal = *static_cast<sol::table*>(terminalTable);

    // Store lua state pointer for creating tables in callbacks
    sol::state* luaPtr = &lua;

    auto tab = terminal.create_named("tab");

    // terminal.tab.on_title_format(function(info) return "..." end)
    tab.set_function("on_title_format",
        [this, luaPtr](sol::protected_function fn) {
            auto luaFn = std::make_shared<sol::protected_function>(std::move(fn));
            titleFormatFn_ = [luaFn, luaPtr](const TabTitleInfo& info) -> std::string {
                sol::table tbl = luaPtr->create_table();
                tbl["tab_index"] = info.tab_index;
                tbl["process"] = info.process_name;
                tbl["cwd"] = info.working_dir;
                tbl["title"] = info.title;
                tbl["is_active"] = info.is_active;

                auto result = (*luaFn)(tbl);
                if (result.valid()) {
                    sol::object val = result;
                    if (val.is<std::string>()) {
                        return val.as<std::string>();
                    }
                }
                return "";  // fallback: use C++ default
            };
        });

    // terminal.tab.set_title(tab_id, title) -- stub, full impl in Phase 2
    tab.set_function("set_title", [](int, std::string) {});

    // terminal.tab.get_info(tab_id) -- stub, full impl in Phase 2
    tab.set_function("get_info", [](int) -> sol::object { return sol::nil; });

    // terminal.tab.list() -- stub, full impl in Phase 2
    tab.set_function("list", []() -> sol::object { return sol::nil; });
}

void LuaTabModule::clearCallbacks() {
    titleFormatFn_ = nullptr;
}

} // namespace termcore
```

- [ ] **Step 3: Build to verify compilation**

Run: `cd D:/Projects/BreadTerminal/build && cmake .. && cmake --build . --target termcore 2>&1 | tail -10`
Expected: Successful build

- [ ] **Step 4: Commit**

```bash
git add core/src/lua_bindings/lua_tab_module.h core/src/lua_bindings/lua_tab_module.cpp
git commit -m "feat: add LuaTabModule as sample binding module"
```

---

### Task 6: Add title format callback to TabController

**Files:**
- Modify: `core/include/termcore/tab_controller.h`
- Modify: `core/src/tab_controller.cpp`

- [ ] **Step 1: Read tabBarInfo() to find insertion point**

Run: Read `core/src/tab_controller.cpp` and find the `tabBarInfo()` method. Note the exact line where `TabInfo::title` is set.

- [ ] **Step 2: Add callback slot to TabController header**

In `core/include/termcore/tab_controller.h`, add a forward declaration before `class TabController`:

```cpp
struct TabTitleInfo;  // defined in lua_bindings/lua_tab_module.h
```

Add inside `class TabController` public section (after `setProfileManager`):

```cpp
    // Lua callback for custom tab title formatting.
    // Receives TabTitleInfo, returns custom title string (empty = use default).
    using TitleFormatFn = std::function<std::string(const TabTitleInfo&)>;
    void setTitleFormatCallback(TitleFormatFn fn) { titleFormatFn_ = std::move(fn); }
```

Add private members:

```cpp
    TitleFormatFn titleFormatFn_;
    bool inTitleFormat_ = false;  // reentrancy guard
```

- [ ] **Step 3: Invoke callback in tabBarInfo()**

In `core/src/tab_controller.cpp`, add include at top:

```cpp
#include "lua_bindings/lua_tab_module.h"  // for TabTitleInfo
```

In the `tabBarInfo()` method, after each `TabInfo` has its `title`, `process_name`, and `active` fields set, insert:

```cpp
    // Try Lua title format callback first
    if (titleFormatFn_ && !inTitleFormat_) {
        TabTitleInfo luaInfo;
        luaInfo.tab_index = static_cast<int>(i);
        luaInfo.process_name = info.process_name;
        luaInfo.title = info.title;
        luaInfo.is_active = info.active;
        // working_dir: set from pane CWD if available
        inTitleFormat_ = true;
        auto custom = titleFormatFn_(luaInfo);
        inTitleFormat_ = false;
        if (!custom.empty()) {
            info.title = std::move(custom);
        }
    }
```

Note: Adapt the variable names (`i`, `info`) to match the actual loop in `tabBarInfo()`.

- [ ] **Step 4: Build to verify compilation**

Run: `cd D:/Projects/BreadTerminal/build && cmake --build . --target termcore 2>&1 | tail -5`
Expected: Successful build

- [ ] **Step 5: Commit**

```bash
git add core/include/termcore/tab_controller.h core/src/tab_controller.cpp
git commit -m "feat: add Lua title format callback slot to TabController"
```

---

### Task 7: Integration tests — full round-trip

**Files:**
- Modify: `tests/test_lua_engine.cpp`

- [ ] **Step 1: Write integration tests**

Add include after the other includes in the `#else` block:

```cpp
#include "lua_bindings/lua_tab_module.h"
```

Add before `#endif`:

```cpp
// 14. LuaTabModule registers terminal.tab sub-table
TEST(LuaEngine, TabModuleRegistersSubTable) {
    LuaEngine engine;
    auto tabMod = std::make_shared<termcore::LuaTabModule>(nullptr);
    engine.registerModule(tabMod);
    engine.initializeModules();

    std::string result;
    engine.registerFunction("test_capture", [&](const std::string& s) -> std::string {
        result = s;
        return "";
    });
    EXPECT_TRUE(engine.loadString(R"(
        if terminal.tab then
            terminal.test_capture("tab_exists")
        end
    )").ok());
    EXPECT_EQ(result, "tab_exists");
}

// 15. Full round-trip: Lua callback -> LuaTabModule -> title format
TEST(LuaEngine, TabModuleTitleFormatRoundTrip) {
    LuaEngine engine;
    auto tabMod = std::make_shared<termcore::LuaTabModule>(nullptr);
    engine.registerModule(tabMod);
    engine.initializeModules();

    EXPECT_TRUE(engine.loadString(R"(
        terminal.tab.on_title_format(function(info)
            return "[" .. info.process .. "] " .. info.cwd
        end)
    )").ok());

    ASSERT_TRUE(tabMod->titleFormatCallback() != nullptr);

    termcore::TabTitleInfo info;
    info.process_name = "vim";
    info.working_dir = "/home/user";
    auto result = tabMod->titleFormatCallback()(info);
    EXPECT_EQ(result, "[vim] /home/user");
}

// 16. clearCallbacks resets title format
TEST(LuaEngine, TabModuleClearCallbacksResetsFormat) {
    LuaEngine engine;
    auto tabMod = std::make_shared<termcore::LuaTabModule>(nullptr);
    engine.registerModule(tabMod);
    engine.initializeModules();

    EXPECT_TRUE(engine.loadString(R"(
        terminal.tab.on_title_format(function(info) return "custom" end)
    )").ok());
    EXPECT_TRUE(tabMod->titleFormatCallback() != nullptr);

    tabMod->clearCallbacks();
    EXPECT_TRUE(tabMod->titleFormatCallback() == nullptr);
}
```

- [ ] **Step 2: Run all tests**

Run: `cd D:/Projects/BreadTerminal/build && cmake --build . --target termcore_tests && cd D:/Projects/BreadTerminal/build && ctest -R LuaEngine --output-on-failure`
Expected: All 16 tests PASS

- [ ] **Step 3: Commit**

```bash
git add tests/test_lua_engine.cpp
git commit -m "test: add LuaTabModule integration tests with round-trip verification"
```

---

## Phase 1 Complete Checklist

After all tasks:
- [ ] `ILuaModule` interface exists with `moduleName()`, `requiredCapability()`, `registerBindings()`, `clearCallbacks()`
- [ ] `LuaEngine` supports `registerModule()`, `initializeModules()` (with and without capability filter), `clearAllModules()`
- [ ] Destructor calls `clearAllModules()` before `sol::state` destruction
- [ ] `PluginCapability` has `UI` and `Clipboard` entries
- [ ] `CMakeLists.txt` auto-discovers `lua_bindings/*.cpp` files with PRIVATE include path
- [ ] `LuaTabModule` demonstrates the full pattern: sub-table creation, callback registration with safe state reference
- [ ] `TabController` has a `TitleFormatFn` callback slot with reentrancy guard used in `tabBarInfo()`
- [ ] All 16 tests pass

Phase 2 agents can now independently create their modules by:
1. Creating `core/src/lua_bindings/lua_<name>_module.h` and `.cpp`
2. Adding callback slots to corresponding C++ headers
3. No shared-file conflicts — each agent touches different component headers
