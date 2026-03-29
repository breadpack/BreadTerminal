#include <gtest/gtest.h>

#if !TERMCORE_HAS_LUA
TEST(LuaRequireTest, Disabled) { GTEST_SKIP() << "Lua not available"; }
#else

#include <termcore/lua_engine.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using termcore::LuaEngine;

namespace fs = std::filesystem;

class LuaRequireTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temp directory structure for plugin modules
        temp_dir_ = fs::temp_directory_path() / "bread_test_require";
        fs::create_directories(temp_dir_ / "lib");
        fs::create_directories(temp_dir_ / "lib" / "mymod");
        fs::create_directories(temp_dir_ / "myplugin");

        // Create lib/helper.lua
        {
            std::ofstream f(temp_dir_ / "lib" / "helper.lua");
            f << "local M = {}\n"
              << "M.greet = function(name) return 'hello ' .. name end\n"
              << "return M\n";
        }

        // Create lib/mymod/init.lua
        {
            std::ofstream f(temp_dir_ / "lib" / "mymod" / "init.lua");
            f << "local M = {}\n"
              << "M.value = 42\n"
              << "return M\n";
        }

        // Create myplugin/init.lua (top-level plugin module)
        {
            std::ofstream f(temp_dir_ / "myplugin" / "init.lua");
            f << "local M = {}\n"
              << "M.name = 'myplugin'\n"
              << "return M\n";
        }

        engine_.registerFunction("test_capture",
            [this](const std::string& s) -> std::string {
                captured_ = s;
                return "";
            });

        engine_.setPluginsPath(temp_dir_.string());
    }

    void TearDown() override {
        fs::remove_all(temp_dir_);
    }

    LuaEngine engine_;
    fs::path temp_dir_;
    std::string captured_;
};

TEST_F(LuaRequireTest, RequireFromPluginsLib) {
    auto result = engine_.loadString(R"(
        local helper = require("helper")
        terminal.test_capture(helper.greet("world"))
    )");
    ASSERT_TRUE(result.ok()) << engine_.lastError();
    EXPECT_EQ(captured_, "hello world");
}

TEST_F(LuaRequireTest, RequireBlocksSystemPaths) {
    // Attempting to require a system module should fail
    // because package.path is restricted to plugins dir only
    auto result = engine_.loadString(R"(
        local ok, err = pcall(require, "os")
        if not ok then
            terminal.test_capture("blocked")
        else
            terminal.test_capture("allowed")
        end
    )");
    ASSERT_TRUE(result.ok()) << engine_.lastError();
    // os module is not in our restricted paths, so require should fail
    // (note: os is not opened by open_libraries, and require can't find it)
    EXPECT_EQ(captured_, "blocked");
}

TEST_F(LuaRequireTest, RequireBlocksCModules) {
    // loadlib is disabled, so C modules can't be loaded
    auto result = engine_.loadString(R"(
        local ok = pcall(function()
            package.loadlib("some_lib", "some_func")
        end)
        if ok then
            terminal.test_capture("allowed")
        else
            terminal.test_capture("blocked")
        end
    )");
    ASSERT_TRUE(result.ok()) << engine_.lastError();
    EXPECT_EQ(captured_, "blocked");
}

TEST_F(LuaRequireTest, RequireSubdirectoryInit) {
    auto result = engine_.loadString(R"(
        local mymod = require("mymod")
        terminal.test_capture(tostring(mymod.value))
    )");
    ASSERT_TRUE(result.ok()) << engine_.lastError();
    EXPECT_EQ(captured_, "42");
}

#endif // TERMCORE_HAS_LUA
