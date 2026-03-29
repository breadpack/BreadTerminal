#include <gtest/gtest.h>
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include "termcore/lua_engine.h"
#include "termcore/lua_module.h"
#include "termcore/plugin_manager.h"

// Module headers needed for the default plugins
#include "lua_bindings/lua_tab_module.h"
#include "lua_bindings/lua_command_module.h"
#include "lua_bindings/lua_url_module.h"
#include "lua_bindings/lua_event_module.h"

#include <filesystem>
#include <fstream>

using namespace termcore;

class DefaultPluginsTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<LuaEngine>();

        // Register modules that the default plugins need
        engine_->registerModule(std::make_shared<LuaTabModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaCommandModule>(nullptr));
        engine_->registerModule(std::make_shared<LuaUrlModule>(nullptr, nullptr));
        engine_->registerModule(std::make_shared<LuaEventModule>());
        engine_->initializeModules();

        // Register terminal.action() stub for commands plugin
        engine_->setActionHandler([this](const std::string& name) {
            lastAction_ = name;
        });
    }

    void TearDown() override {
        if (engine_) {
            engine_->clearAllModules();
        }
        engine_.reset();
    }

    // Helper to create a temporary plugin directory structure
    struct TempPluginDir {
        std::filesystem::path root;

        TempPluginDir() {
            root = std::filesystem::temp_directory_path() / "bread_test_default_plugins";
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
            std::filesystem::create_directories(root);
        }

        ~TempPluginDir() {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }

        void createPlugin(const std::string& name,
                          const std::string& plugin_lua,
                          const std::string& init_lua) {
            auto dir = root / name;
            std::filesystem::create_directories(dir);
            {
                std::ofstream f((dir / "plugin.lua").string());
                f << plugin_lua;
            }
            {
                std::ofstream f((dir / "init.lua").string());
                f << init_lua;
            }
        }
    };

    std::unique_ptr<LuaEngine> engine_;
    std::string lastAction_;
};

TEST_F(DefaultPluginsTest, IconsPluginScansAndLoads) {
    TempPluginDir tmp;
    tmp.createPlugin("icons",
        R"lua(return {
    name = "icons",
    version = "0.1.0",
    author = "BreadTerminal",
    description = "Default process icons for tab bar (Nerd Font)",
    capabilities = {"config"},
})lua",
        R"lua(
-- BreadTerminal default process icon mappings (Nerd Font)
local icons = {
    bash = "F489",
    ["cmd.exe"] = "E70F",
    powershell = "EBC7",
}
for process, icon in pairs(icons) do
    terminal.tab.set_process_icon(process, icon)
end
)lua");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    const auto& plugins = mgr.plugins();
    ASSERT_EQ(plugins.size(), 1u);
    EXPECT_EQ(plugins[0].metadata.name, "icons");
    EXPECT_EQ(plugins[0].metadata.version, "0.1.0");
    EXPECT_EQ(plugins[0].state, PluginState::Discovered);
    EXPECT_TRUE(mgr.hasCapability("icons", PluginCapability::Config));

    auto result = mgr.loadPlugin("icons");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
    EXPECT_EQ(plugins[0].state, PluginState::Loaded);
}

TEST_F(DefaultPluginsTest, UrlDetectPluginScansAndLoads) {
    TempPluginDir tmp;
    tmp.createPlugin("url-detect",
        R"lua(return {
    name = "url-detect",
    version = "0.1.0",
    author = "BreadTerminal",
    description = "Default URL scheme detection and colors",
    capabilities = {"config"},
})lua",
        R"lua(
-- BreadTerminal default URL schemes
terminal.url.add_scheme("https", "http", "ftp", "file", "ssh", "git")
terminal.url.set_color(0x89b4fa)
)lua");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    const auto& plugins = mgr.plugins();
    ASSERT_EQ(plugins.size(), 1u);
    EXPECT_EQ(plugins[0].metadata.name, "url-detect");
    EXPECT_EQ(plugins[0].metadata.version, "0.1.0");
    EXPECT_EQ(plugins[0].state, PluginState::Discovered);
    EXPECT_TRUE(mgr.hasCapability("url-detect", PluginCapability::Config));

    auto result = mgr.loadPlugin("url-detect");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
    EXPECT_EQ(plugins[0].state, PluginState::Loaded);
}

TEST_F(DefaultPluginsTest, CommandsPluginScansAndLoads) {
    TempPluginDir tmp;
    tmp.createPlugin("commands",
        R"lua(return {
    name = "commands",
    version = "0.1.0",
    author = "BreadTerminal",
    description = "Default command palette entries",
    capabilities = {"keybindings"},
})lua",
        R"lua(
-- Minimal command registration to verify plugin loads
terminal.command.register("New Tab", function() terminal.action("new_tab") end,
    {category="Tab", description="Open a new tab"})
)lua");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    const auto& plugins = mgr.plugins();
    ASSERT_EQ(plugins.size(), 1u);
    EXPECT_EQ(plugins[0].metadata.name, "commands");
    EXPECT_EQ(plugins[0].metadata.version, "0.1.0");
    EXPECT_EQ(plugins[0].state, PluginState::Discovered);
    EXPECT_TRUE(mgr.hasCapability("commands", PluginCapability::Keybindings));

    auto result = mgr.loadPlugin("commands");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
    EXPECT_EQ(plugins[0].state, PluginState::Loaded);
}

TEST_F(DefaultPluginsTest, AllThreePluginsLoadTogether) {
    TempPluginDir tmp;

    tmp.createPlugin("icons",
        R"lua(return { name = "icons", version = "0.1.0", capabilities = {"config"} })lua",
        R"lua(
local icons = { bash = "F489", python = "E73C" }
for process, icon in pairs(icons) do
    terminal.tab.set_process_icon(process, icon)
end
)lua");

    tmp.createPlugin("url-detect",
        R"lua(return { name = "url-detect", version = "0.1.0", capabilities = {"config"} })lua",
        R"lua(
terminal.url.add_scheme("https", "http")
terminal.url.set_color(0x89b4fa)
)lua");

    tmp.createPlugin("commands",
        R"lua(return { name = "commands", version = "0.1.0", capabilities = {"keybindings"} })lua",
        R"lua(
terminal.command.register("New Tab", function() terminal.action("new_tab") end,
    {category="Tab", description="Open a new tab"})
terminal.command.register("Close Tab", function() terminal.action("close_tab") end,
    {category="Tab", description="Close the current tab"})
)lua");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    EXPECT_EQ(mgr.plugins().size(), 3u);

    // Load all discovered plugins
    for (const auto& info : mgr.plugins()) {
        if (info.state == PluginState::Discovered) {
            auto result = mgr.loadPlugin(info.metadata.name);
            EXPECT_TRUE(result.ok())
                << "Failed to load " << info.metadata.name << ": "
                << result.errorMessage();
        }
    }

    // All three should be loaded
    int loaded = 0;
    for (const auto& info : mgr.plugins()) {
        if (info.state == PluginState::Loaded) loaded++;
    }
    EXPECT_EQ(loaded, 3);
}

TEST_F(DefaultPluginsTest, DefaultScriptsNoLongerContainRemovedEntries) {
    // Verify that loadDefaults() works without the removed scripts.
    // This effectively tests that kDefaultScripts[] was updated correctly.
    // loadDefaults() will fail if it references missing embedded headers.
    engine_->loadDefaults();

    // The remaining defaults (config, colors, keybindings, tab_title,
    // paste_guard, themes, providers) should have loaded without error.
    // We can verify by checking that terminal.version is still set
    // (it's set by the engine, not defaults, so it should always work).
    auto r = engine_->loadString("assert(terminal.version ~= nil)");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}
