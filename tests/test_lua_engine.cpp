#include <gtest/gtest.h>

#if !TERMCORE_HAS_LUA
// Skip all Lua tests when Lua is not available
TEST(LuaEngineTest, Disabled) { GTEST_SKIP() << "Lua not available"; }
#else

#include <termcore/lua_engine.h>

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
    EXPECT_TRUE(engine.loadString("x = 1 + 2"));
}

// 3. loadString syntax error -> false, lastError non-empty
TEST(LuaEngine, LoadStringSyntaxError) {
    LuaEngine engine;
    EXPECT_FALSE(engine.loadString("if then else end end"));
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
    )"));

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
    )"));

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
    )"));

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
    )"));

    EXPECT_EQ(version, "0.1.0");
}

// 8. Error in handler doesn't crash
TEST(LuaEngine, ErrorInHandlerNoCrash) {
    LuaEngine engine;
    EXPECT_TRUE(engine.loadString(R"(
        terminal.on("bell", function(data)
            error("intentional error")
        end)
    )"));

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

    EXPECT_TRUE(engine.loadPlugin(tmp_path));
    EXPECT_EQ(engine.loadedPlugins().size(), 1u);
    EXPECT_EQ(engine.loadedPlugins()[0], tmp_path);

    engine.fireEvent(LuaEvent::OnResize, "80x24");
    EXPECT_EQ(captured, "resized:80x24");

    std::remove(tmp_path.c_str());
}

// 10. loadPlugin nonexistent -> false
TEST(LuaEngine, LoadPluginNonexistent) {
    LuaEngine engine;
    EXPECT_FALSE(engine.loadPlugin("/nonexistent/path/plugin.lua"));
    EXPECT_FALSE(engine.lastError().empty());
}

#endif // TERMCORE_HAS_LUA
