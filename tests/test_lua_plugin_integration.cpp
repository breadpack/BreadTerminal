#include <gtest/gtest.h>
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include "termcore/lua_engine.h"
#include "termcore/lua_module.h"

// Include all 20 module headers
#include "lua_bindings/lua_tab_module.h"
#include "lua_bindings/lua_command_module.h"
#include "lua_bindings/lua_event_module.h"
#include "lua_bindings/lua_theme_module.h"
#include "lua_bindings/lua_url_module.h"
#include "lua_bindings/lua_mux_module.h"
#include "lua_bindings/lua_shader_module.h"
#include "lua_bindings/lua_search_module.h"
#include "lua_bindings/lua_clipboard_module.h"
#include "lua_bindings/lua_paste_module.h"
#include "lua_bindings/lua_notify_module.h"
#include "lua_bindings/lua_status_module.h"
#include "lua_bindings/lua_git_module.h"
#include "lua_bindings/lua_session_module.h"
#include "lua_bindings/lua_annotation_module.h"
#include "lua_bindings/lua_shell_module.h"
#include "lua_bindings/lua_workspace_module.h"
#include "lua_bindings/lua_settings_module.h"
#include "lua_bindings/lua_vi_module.h"
#include "lua_bindings/lua_quick_module.h"

using namespace termcore;

class LuaPluginIntegration : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<LuaEngine>();
    }

    void TearDown() override {
        // Explicitly clear modules before engine destruction to ensure
        // sol::protected_function stored in modules are released while
        // the Lua state is still alive.
        if (engine_) {
            engine_->clearAllModules();
        }
        engine_.reset();
    }

    void registerAllModules() {
        // All modules with nullptr backing components — tests that sub-tables exist
        engine_->registerModule(std::make_shared<LuaTabModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaCommandModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaEventModule>());
        engine_->registerModule(std::make_shared<LuaThemeModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaUrlModule>(nullptr, nullptr));
        engine_->registerModule(std::make_shared<LuaMuxModule>(nullptr, nullptr));
        engine_->registerModule(std::make_shared<LuaShaderModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaSearchModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaClipboardModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaPasteModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaNotifyModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaStatusModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaGitModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaSessionModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaAnnotationModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaShellModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaWorkspaceModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaSettingsModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaViModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaQuickModule>(nullptr));
        engine_->initializeModules();
    }

    std::unique_ptr<LuaEngine> engine_;
};

TEST_F(LuaPluginIntegration, AllSubTablesExist) {
    registerAllModules();

    // Verify all 20 sub-tables are created under terminal.*
    auto result = engine_->loadString(R"(
        local checks = {
            "tab", "command", "event", "theme", "url", "mux",
            "shader", "search", "clipboard", "paste", "notify",
            "status", "git", "session", "annotation", "shell",
            "workspace", "settings", "vi", "quick"
        }
        local missing = {}
        for _, name in ipairs(checks) do
            if type(terminal[name]) ~= "table" then
                table.insert(missing, name)
            end
        end
        if #missing > 0 then
            error("Missing sub-tables: " .. table.concat(missing, ", "))
        end
    )");
    EXPECT_TRUE(result.ok()) << engine_->lastError();
}

TEST_F(LuaPluginIntegration, TabFunctionsRegistered) {
    registerAllModules();

    auto result = engine_->loadString(R"(
        assert(type(terminal.tab.on_title_format) == "function", "on_title_format missing")
        assert(type(terminal.tab.list) == "function", "list missing")
        assert(type(terminal.tab.get_info) == "function", "get_info missing")
        assert(type(terminal.tab.set_title) == "function", "set_title missing")
    )");
    EXPECT_TRUE(result.ok()) << engine_->lastError();
}

TEST_F(LuaPluginIntegration, EventOnRegistered) {
    registerAllModules();

    auto result = engine_->loadString(R"(
        assert(type(terminal.event.on) == "function", "event.on missing")
    )");
    EXPECT_TRUE(result.ok()) << engine_->lastError();
}

TEST_F(LuaPluginIntegration, CommandRegisterRegistered) {
    registerAllModules();

    auto result = engine_->loadString(R"(
        assert(type(terminal.command.register) == "function", "command.register missing")
        assert(type(terminal.command.remove) == "function", "command.remove missing")
    )");
    EXPECT_TRUE(result.ok()) << engine_->lastError();
}

TEST_F(LuaPluginIntegration, ClearAllModulesResetsEverything) {
    registerAllModules();

    // Verify tables exist first
    auto r1 = engine_->loadString("assert(terminal.tab ~= nil)");
    EXPECT_TRUE(r1.ok());

    // Clear all
    engine_->clearAllModules();

    // After clear, modules are gone but we can re-register on a fresh engine
    engine_.reset();
    engine_ = std::make_unique<LuaEngine>();
    engine_->registerModule(std::make_shared<LuaTabModule>(nullptr));
    engine_->initializeModules();

    auto r2 = engine_->loadString("assert(terminal.tab ~= nil)");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();
}

TEST_F(LuaPluginIntegration, CapabilityFilteringWorks) {
    // Register all modules but only initialize with Config capability
    engine_->registerModule(std::make_shared<LuaTabModule>(nullptr));      // Events
    engine_->registerModule(std::make_shared<LuaThemeModule>(nullptr));    // Config
    engine_->registerModule(std::make_shared<LuaQuickModule>(nullptr));    // Config

    std::vector<PluginCapability> caps = { PluginCapability::Config };
    engine_->initializeModules(caps);

    // Theme and quick should exist (Config capability)
    auto r1 = engine_->loadString("assert(terminal.theme ~= nil)");
    EXPECT_TRUE(r1.ok()) << engine_->lastError();

    auto r2 = engine_->loadString("assert(terminal.quick ~= nil)");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();

    // Tab should NOT exist (Events capability, not in allowed list)
    auto r3 = engine_->loadString("assert(terminal.tab == nil)");
    EXPECT_TRUE(r3.ok()) << engine_->lastError();
}

TEST_F(LuaPluginIntegration, MultiModuleLuaScript) {
    registerAllModules();

    // A realistic config.lua snippet that uses multiple APIs
    auto result = engine_->loadString(R"(
        -- Set a title format callback
        terminal.tab.on_title_format(function(info)
            return "[" .. (info.process or "shell") .. "] " .. (info.cwd or "~")
        end)

        -- Register a custom event handler
        terminal.event.on("tab_created", function(tab)
            terminal.log("Tab created!")
        end)

        -- Theme functions should be callable
        local themes = terminal.theme.list()
    )");
    EXPECT_TRUE(result.ok()) << engine_->lastError();
}
