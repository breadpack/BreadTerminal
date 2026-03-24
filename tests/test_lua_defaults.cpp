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
        engine_->registerModule(std::make_shared<LuaTabModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaCommandModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaEventModule>());
        engine_->registerModule(std::make_shared<LuaPasteModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaUrlModule>(nullptr, nullptr));
        engine_->registerModule(std::make_shared<LuaThemeModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaQuickModule>(nullptr));
        engine_->initializeModules();

        // Set terminal.platform for keybindings tests
        engine_->loadString("terminal.platform = 'windows'");

        // Register terminal.action() stub for command tests
        engine_->setActionHandler([this](const std::string& name) {
            lastAction_ = name;
        });
    }

    void TearDown() override {
        engine_->clearAllModules();
        engine_.reset();
    }

    std::unique_ptr<LuaEngine> engine_;
    std::string lastAction_;
};

TEST_F(LuaDefaultsTest, ActionHandlerDispatches) {
    engine_->loadString(R"(
        terminal.action("copy")
    )");
    EXPECT_EQ(lastAction_, "copy");
}

TEST_F(LuaDefaultsTest, PasteGuardApis) {
    // paste module calls go to nullptr PasteGuard so they are no-ops,
    // but the Lua bindings themselves should resolve without error.
    auto r = engine_->loadString(R"(
        assert(type(terminal.paste.set_mode) == "function", "set_mode missing")
        assert(type(terminal.paste.add_danger) == "function", "add_danger missing")
        assert(type(terminal.paste.whitelist) == "function", "whitelist missing")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaDefaultsTest, UrlSchemeApis) {
    // url module calls go to nullptr detector/highlight so they are no-ops,
    // but the Lua bindings themselves should resolve without error.
    auto r = engine_->loadString(R"(
        assert(type(terminal.url.add_scheme) == "function", "add_scheme missing")
        assert(type(terminal.url.set_color) == "function", "set_color missing")
        assert(type(terminal.url.on_click) == "function", "on_click missing")
        assert(type(terminal.url.set_color_by_scheme) == "function", "set_color_by_scheme missing")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaDefaultsTest, TabTitleFormatCallback) {
    auto r = engine_->loadString(R"(
        local function default_title_format(info)
            if info.cwd and info.cwd ~= "" then
                return info.cwd:match("([^/\\]+)$") or info.cwd
            end
            return "Tab " .. (info.tab_index or 0)
        end

        terminal.tab.defaults = terminal.tab.defaults or {}
        terminal.tab.defaults.title_format = default_title_format
        terminal.tab.on_title_format(default_title_format)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaDefaultsTest, ExtendMechanism) {
    auto r = engine_->loadString(R"(
        -- Simulate defaults loading
        local function default_format(info)
            return info.cwd or "Tab"
        end
        terminal.tab.defaults = terminal.tab.defaults or {}
        terminal.tab.defaults.title_format = default_format

        -- User extends defaults
        local original = terminal.tab.defaults.title_format
        terminal.tab.on_title_format(function(info)
            return "PREFIX:" .. original(info)
        end)

        -- Verify defaults table is preserved
        assert(terminal.tab.defaults.title_format ~= nil)
        assert(type(terminal.tab.defaults.title_format) == "function")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaDefaultsTest, ExtendMechanismCallsOriginal) {
    // Verify that the extend pattern actually chains correctly
    auto r = engine_->loadString(R"(
        local function default_format(info)
            return info.cwd or "Tab"
        end
        terminal.tab.defaults = terminal.tab.defaults or {}
        terminal.tab.defaults.title_format = default_format

        local original = terminal.tab.defaults.title_format
        local extended = function(info)
            return "PREFIX:" .. original(info)
        end

        -- Verify the chain produces expected output
        local result = extended({ cwd = "/home/user" })
        assert(result == "PREFIX:/home/user", "Expected 'PREFIX:/home/user', got: " .. result)

        local result2 = extended({})
        assert(result2 == "PREFIX:Tab", "Expected 'PREFIX:Tab', got: " .. result2)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaDefaultsTest, ThemeApis) {
    auto r = engine_->loadString(R"(
        assert(type(terminal.theme.switch) == "function", "switch missing")
        assert(type(terminal.theme.current) == "function", "current missing")
        assert(type(terminal.theme.list) == "function", "list missing")
        assert(type(terminal.theme.on_schedule) == "function", "on_schedule missing")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaDefaultsTest, ProcessIconApi) {
    // set_process_icon calls to nullptr TabController are guarded, verify binding exists
    auto r = engine_->loadString(R"(
        assert(type(terminal.tab.set_process_icon) == "function", "set_process_icon missing")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaDefaultsTest, PlatformBasedKeybindings) {
    // Test that platform-conditional logic works in Lua
    auto r = engine_->loadString(R"(
        local mod = terminal.platform == "macos" and "super" or "ctrl"
        assert(mod == "ctrl", "Expected ctrl for windows platform")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaDefaultsTest, CommandRegisterWithAction) {
    // command.register requires a non-null CommandPalette, so just verify binding exists
    auto r = engine_->loadString(R"(
        assert(type(terminal.command.register) == "function", "register missing")
        assert(type(terminal.command.remove) == "function", "remove missing")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaDefaultsTest, EventOnRegistered) {
    auto r = engine_->loadString(R"(
        assert(type(terminal.event.on) == "function", "event.on missing")
        terminal.event.on("bell", function() end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaDefaultsTest, MultipleDefaultsPattern) {
    // Simulate a full defaults-style script using all available module APIs
    auto r = engine_->loadString(R"(
        -- 1. Tab title defaults
        terminal.tab.defaults = terminal.tab.defaults or {}
        terminal.tab.defaults.title_format = function(info)
            if info.cwd and info.cwd ~= "" then
                return info.cwd:match("([^/\\]+)$") or info.cwd
            end
            return "Tab " .. (info.tab_index or 0)
        end
        terminal.tab.on_title_format(terminal.tab.defaults.title_format)

        -- 2. Event handler
        terminal.event.on("bell", function() end)

        -- 3. Action dispatch
        terminal.action("new_tab")

        -- 4. Platform-conditional logic
        local mod = terminal.platform == "macos" and "super" or "ctrl"
        assert(mod == "ctrl")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
    EXPECT_EQ(lastAction_, "new_tab");
}

TEST_F(LuaDefaultsTest, UserOverridesDefaultTitleFormat) {
    // Load defaults, then user overrides the title format
    engine_->loadString(R"(
        terminal.tab.defaults = terminal.tab.defaults or {}
        terminal.tab.defaults.title_format = function(info)
            return info.cwd or "Tab"
        end
        terminal.tab.on_title_format(terminal.tab.defaults.title_format)
    )");

    // User config overrides
    auto r = engine_->loadString(R"(
        terminal.tab.on_title_format(function(info)
            return "MY:" .. (info.cwd or "Shell")
        end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaDefaultsTest, DefaultsTablePersistsAcrossLoads) {
    // First load sets defaults
    engine_->loadString(R"(
        terminal.tab.defaults = terminal.tab.defaults or {}
        terminal.tab.defaults.title_format = function(info) return "default" end
        terminal.tab.defaults.custom_value = 42
    )");

    // Second load can read the defaults
    auto r = engine_->loadString(R"(
        assert(terminal.tab.defaults ~= nil, "defaults table missing")
        assert(terminal.tab.defaults.custom_value == 42, "custom_value lost")
        assert(type(terminal.tab.defaults.title_format) == "function", "title_format lost")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}
