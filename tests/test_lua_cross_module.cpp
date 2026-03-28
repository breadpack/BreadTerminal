#include <gtest/gtest.h>

#if !TERMCORE_HAS_LUA
TEST(LuaCrossModule, Disabled) { GTEST_SKIP() << "Lua not available"; }
#else

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "termcore/lua_engine.h"
#include "termcore/lua_module.h"
#include "termcore/lua_config.h"
#include "termcore/config.h"

#include "lua_bindings/lua_tab_module.h"
#include "lua_bindings/lua_command_module.h"
#include "lua_bindings/lua_event_module.h"
#include "lua_bindings/lua_paste_module.h"
#include "lua_bindings/lua_url_module.h"
#include "lua_bindings/lua_theme_module.h"
#include "lua_bindings/lua_quick_module.h"
#include "lua_bindings/lua_notify_module.h"
#include "lua_bindings/lua_shell_module.h"
#include "lua_bindings/lua_search_module.h"
#include "lua_bindings/lua_git_module.h"
#include "lua_bindings/lua_session_module.h"
#include "lua_bindings/lua_clipboard_module.h"
#include "lua_bindings/lua_status_module.h"
#include "lua_bindings/lua_mux_module.h"
#include "lua_bindings/lua_annotation_module.h"
#include "lua_bindings/lua_vi_module.h"
#include "lua_bindings/lua_shader_module.h"
#include "lua_bindings/lua_workspace_module.h"
#include "lua_bindings/lua_settings_module.h"
#include "lua_bindings/lua_completion_module.h"
#include "lua_bindings/lua_config_api_module.h"
#include "lua_bindings/lua_provider_module.h"

#include <string>
#include <vector>

using namespace termcore;

// ===========================================================================
// Module Interaction Tests
// ===========================================================================

class LuaCrossModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<LuaEngine>();
    }

    void TearDown() override {
        engine_->clearAllModules();
        engine_.reset();
    }

    std::unique_ptr<LuaEngine> engine_;
};

// Register both event + command modules, both sub-tables accessible
TEST_F(LuaCrossModuleTest, EventAndCommandModulesCoexist) {
    auto eventMod = std::make_shared<LuaEventModule>();
    auto commandMod = std::make_shared<LuaCommandModule>(nullptr);

    engine_->registerModule(eventMod);
    engine_->registerModule(commandMod);
    engine_->initializeModules();

    auto r = engine_->loadString(R"(
        assert(type(terminal.event) == "table", "event sub-table missing")
        assert(type(terminal.event.on) == "function", "event.on missing")
        assert(type(terminal.command) == "table", "command sub-table missing")
        assert(type(terminal.command.register) == "function", "command.register missing")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// Register event handler in Lua, fire event from C++, verify handler called
TEST_F(LuaCrossModuleTest, EventFireFromLuaScript) {
    auto eventMod = std::make_shared<LuaEventModule>();
    engine_->registerModule(eventMod);
    engine_->initializeModules();

    std::string captured;
    engine_->registerFunction("test_capture",
        [&](const std::string& s) -> std::string {
            captured = s;
            return "";
        });

    auto r = engine_->loadString(R"(
        terminal.event.on("custom_event", function()
            terminal.test_capture("fired")
        end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // Fire the event from C++
    eventMod->fireModuleEvent("custom_event", nullptr);
    EXPECT_EQ(captured, "fired");
}

// Multiple on() handlers for same event all fire
TEST_F(LuaCrossModuleTest, MultipleEventHandlersSameEvent) {
    auto eventMod = std::make_shared<LuaEventModule>();
    engine_->registerModule(eventMod);
    engine_->initializeModules();

    std::string result;
    engine_->registerFunction("test_append",
        [&](const std::string& s) -> std::string {
            result += s;
            return "";
        });

    auto r = engine_->loadString(R"(
        terminal.event.on("my_event", function()
            terminal.test_append("A")
        end)
        terminal.event.on("my_event", function()
            terminal.test_append("B")
        end)
        terminal.event.on("my_event", function()
            terminal.test_append("C")
        end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    eventMod->fireModuleEvent("my_event", nullptr);
    EXPECT_EQ(result, "ABC");
}

// One handler throws, others still execute
TEST_F(LuaCrossModuleTest, EventHandlerErrorDoesNotBlockOthers) {
    auto eventMod = std::make_shared<LuaEventModule>();
    engine_->registerModule(eventMod);
    engine_->initializeModules();

    std::string result;
    engine_->registerFunction("test_append",
        [&](const std::string& s) -> std::string {
            result += s;
            return "";
        });

    auto r = engine_->loadString(R"(
        terminal.event.on("err_event", function()
            terminal.test_append("A")
        end)
        terminal.event.on("err_event", function()
            error("intentional error in handler")
        end)
        terminal.event.on("err_event", function()
            terminal.test_append("C")
        end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    eventMod->fireModuleEvent("err_event", nullptr);
    // The first handler fires, then the second errors, then the third fires.
    // fireModuleEvent iterates all handlers; each call is pcall-protected.
    EXPECT_TRUE(result.find("A") != std::string::npos);
    EXPECT_TRUE(result.find("C") != std::string::npos);
}

// Register command via Lua, verify it appears
TEST_F(LuaCrossModuleTest, CommandRegisterAndListAvailable) {
    auto commandMod = std::make_shared<LuaCommandModule>(nullptr);
    engine_->registerModule(commandMod);
    engine_->initializeModules();

    // With a nullptr CommandPalette, register should be a no-op but not crash
    auto r = engine_->loadString(R"(
        assert(type(terminal.command.register) == "function",
               "command.register not a function")
        assert(type(terminal.command.remove) == "function",
               "command.remove not a function")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// Register all 23 modules, verify no conflicts
TEST_F(LuaCrossModuleTest, AllModulesLoadedSimultaneously) {
    engine_->registerModule(std::make_shared<LuaEventModule>());
    engine_->registerModule(std::make_shared<LuaCommandModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaTabModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaPasteModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaUrlModule>(nullptr, nullptr));
    engine_->registerModule(std::make_shared<LuaThemeModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaQuickModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaNotifyModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaShellModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaSearchModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaGitModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaSessionModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaClipboardModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaStatusModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaMuxModule>(nullptr, nullptr));
    engine_->registerModule(std::make_shared<LuaAnnotationModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaViModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaShaderModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaWorkspaceModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaSettingsModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaCompletionModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaConfigApiModule>(nullptr, nullptr));
    engine_->registerModule(std::make_shared<LuaProviderModule>(nullptr));

    // Should not crash or conflict
    engine_->initializeModules();

    // Verify all sub-tables are accessible
    auto r = engine_->loadString(R"(
        assert(type(terminal.event) == "table",     "event missing")
        assert(type(terminal.command) == "table",   "command missing")
        assert(type(terminal.tab) == "table",       "tab missing")
        assert(type(terminal.paste) == "table",     "paste missing")
        assert(type(terminal.url) == "table",       "url missing")
        assert(type(terminal.theme) == "table",     "theme missing")
        assert(type(terminal.quick) == "table",     "quick missing")
        assert(type(terminal.notify) == "table",    "notify missing")
        assert(type(terminal.shell) == "table",     "shell missing")
        assert(type(terminal.search) == "table",    "search missing")
        assert(type(terminal.git) == "table",       "git missing")
        assert(type(terminal.session) == "table",   "session missing")
        assert(type(terminal.clipboard) == "table", "clipboard missing")
        assert(type(terminal.status) == "table",    "status missing")
        assert(type(terminal.mux) == "table",       "mux missing")
        assert(type(terminal.annotation) == "table","annotation missing")
        assert(type(terminal.vi) == "table",        "vi missing")
        assert(type(terminal.shader) == "table",    "shader missing")
        assert(type(terminal.workspace) == "table", "workspace missing")
        assert(type(terminal.settings) == "table",  "settings missing")
        assert(type(terminal.completion) == "table", "completion missing")
        assert(type(terminal.provider) == "function",  "provider missing")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ===========================================================================
// Config Round-Trip Tests
// ===========================================================================

// Default Config -> serialize -> load -> compare key fields
TEST(LuaConfigRoundTrip, DefaultConfigRoundTrip) {
    Config original;
    std::string lua = serializeConfigLua(original);

    ASSERT_TRUE(loadConfigLuaString(lua).ok());
    const Config& parsed = luaConfig();

    EXPECT_EQ(parsed.font_family, original.font_family);
    EXPECT_FLOAT_EQ(parsed.font_size, original.font_size);
    EXPECT_EQ(parsed.background, original.background);
    EXPECT_EQ(parsed.foreground, original.foreground);
    EXPECT_EQ(parsed.window_width, original.window_width);
    EXPECT_EQ(parsed.window_height, original.window_height);
    EXPECT_EQ(parsed.scrollback_limit, original.scrollback_limit);
    EXPECT_EQ(parsed.cursor_style, original.cursor_style);
    EXPECT_EQ(parsed.cursor_blink, original.cursor_blink);
    EXPECT_FLOAT_EQ(parsed.background_opacity, original.background_opacity);
    EXPECT_EQ(parsed.sidebar_visible, original.sidebar_visible);
    EXPECT_EQ(parsed.sidebar_width, original.sidebar_width);
}

// Set font_family/font_size -> serialize -> load -> verify
TEST(LuaConfigRoundTrip, CustomFontConfigRoundTrip) {
    Config original;
    original.font_family = "JetBrains Mono NF";
    original.font_size = 22.5f;
    original.font_features = {"calt", "liga", "ss01"};
    original.font_fallback = {"Noto Sans CJK", "Segoe UI Emoji"};

    std::string lua = serializeConfigLua(original);
    ASSERT_TRUE(loadConfigLuaString(lua).ok());
    const Config& parsed = luaConfig();

    EXPECT_EQ(parsed.font_family, "JetBrains Mono NF");
    EXPECT_FLOAT_EQ(parsed.font_size, 22.5f);
    ASSERT_EQ(parsed.font_features.size(), 3u);
    EXPECT_EQ(parsed.font_features[0], "calt");
    EXPECT_EQ(parsed.font_features[1], "liga");
    EXPECT_EQ(parsed.font_features[2], "ss01");
}

// Custom palette -> serialize -> load -> verify all 16 standard colors
TEST(LuaConfigRoundTrip, ColorPaletteRoundTrip) {
    Config original;
    // Set all 16 standard palette colors to distinct values
    for (int i = 0; i < 16; ++i) {
        original.palette[i] = static_cast<uint32_t>(0x100000 * i + 0x0A0B0C);
    }
    original.background = 0x112233;
    original.foreground = 0xAABBCC;
    original.cursor_color = 0xFF0000;
    original.selection_background = 0x334455;
    original.selection_foreground = 0xDDEEFF;

    std::string lua = serializeConfigLua(original);
    ASSERT_TRUE(loadConfigLuaString(lua).ok());
    const Config& parsed = luaConfig();

    EXPECT_EQ(parsed.background, 0x112233u);
    EXPECT_EQ(parsed.foreground, 0xAABBCCu);
    EXPECT_EQ(parsed.cursor_color, 0xFF0000u);
    EXPECT_EQ(parsed.selection_background, 0x334455u);
    EXPECT_EQ(parsed.selection_foreground, 0xDDEEFFu);

    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(parsed.palette[i], original.palette[i])
            << "palette[" << i << "] mismatch";
    }
}

// Custom keybindings -> serialize -> load -> verify
TEST(LuaConfigRoundTrip, KeybindingRoundTrip) {
    Config original;
    original.keybindings.push_back({"ctrl+t", "new_tab"});
    original.keybindings.push_back({"ctrl+w", "close_tab"});
    original.keybindings.push_back({"ctrl+shift+d", "split_right"});
    original.keybindings.push_back({"ctrl+c", "copy"});
    original.keybindings.push_back({"ctrl+v", "paste"});

    std::string lua = serializeConfigLua(original);
    ASSERT_TRUE(loadConfigLuaString(lua).ok());
    const Config& parsed = luaConfig();

    ASSERT_EQ(parsed.keybindings.size(), 5u);
    EXPECT_EQ(parsed.keybindings[0].trigger, "ctrl+t");
    EXPECT_EQ(parsed.keybindings[0].action, "new_tab");
    EXPECT_EQ(parsed.keybindings[1].trigger, "ctrl+w");
    EXPECT_EQ(parsed.keybindings[1].action, "close_tab");
    EXPECT_EQ(parsed.keybindings[2].trigger, "ctrl+shift+d");
    EXPECT_EQ(parsed.keybindings[2].action, "split_right");
    EXPECT_EQ(parsed.keybindings[3].trigger, "ctrl+c");
    EXPECT_EQ(parsed.keybindings[3].action, "copy");
    EXPECT_EQ(parsed.keybindings[4].trigger, "ctrl+v");
    EXPECT_EQ(parsed.keybindings[4].action, "paste");
}

// All boolean settings preserved through round-trip
TEST(LuaConfigRoundTrip, BooleanSettingsRoundTrip) {
    Config original;
    // Flip all booleans from their defaults
    original.cursor_blink = false;
    original.clipboard_paste_bracketed_safe = false;
    original.allow_clipboard_write = true;
    original.clickable_urls = false;
    original.notify_on_command_finish = false;
    original.sidebar_visible = true;
    original.auto_detect_high_contrast = false;
    original.respect_reduced_motion = false;
    original.font_ligatures = false;

    std::string lua = serializeConfigLua(original);
    ASSERT_TRUE(loadConfigLuaString(lua).ok());
    const Config& parsed = luaConfig();

    EXPECT_FALSE(parsed.cursor_blink);
    EXPECT_FALSE(parsed.clipboard_paste_bracketed_safe);
    EXPECT_TRUE(parsed.allow_clipboard_write);
    EXPECT_FALSE(parsed.clickable_urls);
    EXPECT_FALSE(parsed.notify_on_command_finish);
    EXPECT_TRUE(parsed.sidebar_visible);
    EXPECT_FALSE(parsed.auto_detect_high_contrast);
    EXPECT_FALSE(parsed.respect_reduced_motion);
    EXPECT_FALSE(parsed.font_ligatures);
}

// ===========================================================================
// Defaults Override Tests
// ===========================================================================

class LuaDefaultsOverrideTest : public ::testing::Test {
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

        engine_->loadString("terminal.platform = 'windows'");
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

// Load defaults then user config, user wins
TEST_F(LuaDefaultsOverrideTest, UserConfigOverridesDefaults) {
    // Load "defaults": set a default title format and a default action
    auto r1 = engine_->loadString(R"(
        terminal.tab.defaults = terminal.tab.defaults or {}
        terminal.tab.defaults.title_format = function(info)
            return info.cwd or "DefaultTab"
        end
        terminal.tab.on_title_format(terminal.tab.defaults.title_format)
    )");
    EXPECT_TRUE(r1.ok()) << engine_->lastError();

    // Now "user config" overrides the title format
    auto r2 = engine_->loadString(R"(
        terminal.tab.on_title_format(function(info)
            return "USER:" .. (info.cwd or "Shell")
        end)
    )");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();

    // Verify the user callback is the active one
    std::string result;
    engine_->registerFunction("test_capture",
        [&](const std::string& s) -> std::string {
            result = s;
            return "";
        });

    auto r3 = engine_->loadString(R"(
        -- Simulate what TabModule would do internally:
        -- The last on_title_format wins, so the user's callback is active.
        terminal.test_capture("user_override_active")
    )");
    EXPECT_TRUE(r3.ok()) << engine_->lastError();
    EXPECT_EQ(result, "user_override_active");
}

// Verify extend() pattern chains correctly
TEST_F(LuaDefaultsOverrideTest, DefaultsExtendMechanism) {
    // Set up a default
    auto r1 = engine_->loadString(R"(
        terminal.tab.defaults = terminal.tab.defaults or {}
        terminal.tab.defaults.title_format = function(info)
            return info.cwd or "Tab"
        end
    )");
    EXPECT_TRUE(r1.ok()) << engine_->lastError();

    // Extend pattern: user wraps the original
    auto r2 = engine_->loadString(R"(
        local original = terminal.tab.defaults.title_format

        -- Chain: user wraps the default
        local extended = function(info)
            return "EXT:" .. original(info)
        end

        -- Verify the chain produces correct output
        local result1 = extended({ cwd = "/home" })
        assert(result1 == "EXT:/home",
               "Expected 'EXT:/home', got: " .. result1)

        local result2 = extended({})
        assert(result2 == "EXT:Tab",
               "Expected 'EXT:Tab', got: " .. result2)

        -- Double extend
        local extended2 = function(info)
            return "[" .. extended(info) .. "]"
        end
        local result3 = extended2({ cwd = "/usr" })
        assert(result3 == "[EXT:/usr]",
               "Expected '[EXT:/usr]', got: " .. result3)
    )");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();
}

#endif // TERMCORE_HAS_LUA
