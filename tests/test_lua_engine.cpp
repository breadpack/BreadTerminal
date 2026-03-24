#include <gtest/gtest.h>

#if !TERMCORE_HAS_LUA
// Skip all Lua tests when Lua is not available
TEST(LuaEngineTest, Disabled) { GTEST_SKIP() << "Lua not available"; }
#else

#include <termcore/lua_engine.h>
#include <termcore/lua_module.h>
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_bindings/lua_tab_module.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

using termcore::LuaEngine;
using termcore::LuaEvent;

// 1. Create engine -> isValid
TEST(LuaEngine, CreateEngineIsValid) {
    LuaEngine engine;
    EXPECT_TRUE(engine.isValid());
}

// 2. loadString valid code -> true
TEST(LuaEngine, LoadStringValid) {
    LuaEngine engine;
    EXPECT_TRUE(engine.loadString("x = 1 + 2").ok());
}

// 3. loadString syntax error -> false, lastError non-empty
TEST(LuaEngine, LoadStringSyntaxError) {
    LuaEngine engine;
    EXPECT_FALSE(engine.loadString("if then else end end").ok());
    EXPECT_FALSE(engine.lastError().empty());
}

// 4. fireEvent calls registered handler
TEST(LuaEngine, FireEventCallsHandler) {
    LuaEngine engine;
    std::string captured;
    engine.registerFunction(
        "test_capture",
        [&](const std::string& s) -> std::string {
            captured = s;
            return "";
        });

    EXPECT_TRUE(engine.loadString(R"(
        terminal.on("bell", function(data)
            terminal.test_capture("bell:" .. data)
        end)
    )").ok());

    engine.fireEvent(LuaEvent::OnBell, "test_data");
    EXPECT_EQ(captured, "bell:test_data");
}

// 5. Multiple handlers for same event
TEST(LuaEngine, MultipleHandlersSameEvent) {
    LuaEngine engine;
    std::string result;
    engine.registerFunction(
        "test_append",
        [&](const std::string& s) -> std::string {
            result += s;
            return "";
        });

    EXPECT_TRUE(engine.loadString(R"(
        terminal.on("bell", function(data)
            terminal.test_append("A")
        end)
        terminal.on("bell", function(data)
            terminal.test_append("B")
        end)
    )").ok());

    engine.fireEvent(LuaEvent::OnBell);
    EXPECT_EQ(result, "AB");
}

// 6. registerFunction callable from Lua
TEST(LuaEngine, RegisterFunctionCallable) {
    LuaEngine engine;
    std::string received;
    engine.registerFunction(
        "my_func",
        [&](const std::string& s) -> std::string {
            received = s;
            return "reply_" + s;
        });

    EXPECT_TRUE(engine.loadString(R"(
        result = terminal.my_func("hello")
    )").ok());

    EXPECT_EQ(received, "hello");
}

// 7. terminal.version accessible
TEST(LuaEngine, TerminalVersionAccessible) {
    LuaEngine engine;
    std::string version;
    engine.registerFunction(
        "test_capture",
        [&](const std::string& s) -> std::string {
            version = s;
            return "";
        });

    EXPECT_TRUE(engine.loadString(R"(
        terminal.test_capture(terminal.version)
    )").ok());

    EXPECT_EQ(version, "0.1.0");
}

// 8. Error in handler doesn't crash
TEST(LuaEngine, ErrorInHandlerNoCrash) {
    LuaEngine engine;
    EXPECT_TRUE(engine.loadString(R"(
        terminal.on("bell", function(data)
            error("intentional error")
        end)
    )").ok());

    // Should not crash
    engine.fireEvent(LuaEvent::OnBell);
    EXPECT_FALSE(engine.lastError().empty());
}

// 9. loadPlugin with temp file
TEST(LuaEngine, LoadPluginTempFile) {
    LuaEngine engine;
    std::string captured;
    engine.registerFunction(
        "test_capture",
        [&](const std::string& s) -> std::string {
            captured = s;
            return "";
        });

    // Create a temporary file
    std::string tmp_path = std::string(std::tmpnam(nullptr)) + ".lua";
    {
        std::ofstream f(tmp_path);
        f << R"(
            terminal.on("resize", function(data)
                terminal.test_capture("resized:" .. data)
            end)
        )";
    }

    EXPECT_TRUE(engine.loadPlugin(tmp_path).ok());
    EXPECT_EQ(engine.loadedPlugins().size(), 1u);
    EXPECT_EQ(engine.loadedPlugins()[0], tmp_path);

    engine.fireEvent(LuaEvent::OnResize, "80x24");
    EXPECT_EQ(captured, "resized:80x24");

    std::remove(tmp_path.c_str());
}

// 10. loadPlugin nonexistent -> false
TEST(LuaEngine, LoadPluginNonexistent) {
    LuaEngine engine;
    EXPECT_FALSE(engine.loadPlugin("/nonexistent/path/plugin.lua").ok());
    EXPECT_FALSE(engine.lastError().empty());
}

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

#endif // TERMCORE_HAS_LUA
