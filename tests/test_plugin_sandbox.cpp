#include <gtest/gtest.h>
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include "termcore/config.h"
#include "termcore/lua_engine.h"
#include "termcore/lua_module.h"
#include "termcore/plugin_manager.h"

// Module headers for registration
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

#include <filesystem>
#include <fstream>

using namespace termcore;

class PluginSandbox : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<LuaEngine>();
    }

    void TearDown() override {
        if (engine_) {
            engine_->clearAllModules();
        }
        engine_.reset();
    }

    void registerAllModules() {
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
    }

    // Helper to create a temporary plugin directory structure
    struct TempPluginDir {
        std::filesystem::path root;

        TempPluginDir() {
            root = std::filesystem::temp_directory_path() / "bread_test_plugins";
            std::filesystem::create_directories(root);
        }

        ~TempPluginDir() {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }

        // Create a plugin subdirectory with plugin.lua and init.lua
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

        // Create a plugin subdirectory with only plugin.lua (missing init.lua)
        void createPluginMetadataOnly(const std::string& name,
                                      const std::string& plugin_lua) {
            auto dir = root / name;
            std::filesystem::create_directories(dir);
            {
                std::ofstream f((dir / "plugin.lua").string());
                f << plugin_lua;
            }
        }

        // Create a single-file plugin (.lua file directly in root)
        void createSingleFilePlugin(const std::string& filename,
                                    const std::string& content) {
            std::ofstream f((root / filename).string());
            f << content;
        }
    };

    std::unique_ptr<LuaEngine> engine_;
};

// --- Sandbox capability filtering tests ---

TEST_F(PluginSandbox, PluginWithNoCapabilitiesGetsNothing) {
    // Register all modules but initialize with empty capabilities
    registerAllModules();
    std::vector<PluginCapability> empty_caps = {};
    engine_->initializeModules(empty_caps);

    // All module sub-tables should be nil
    auto result = engine_->loadString(R"(
        local modules = {
            "tab", "command", "event", "theme", "url", "mux",
            "shader", "search", "clipboard", "paste", "notify",
            "status", "git", "session", "annotation", "shell",
            "workspace", "settings", "vi", "quick"
        }
        local accessible = {}
        for _, name in ipairs(modules) do
            if terminal[name] ~= nil then
                table.insert(accessible, name)
            end
        end
        if #accessible > 0 then
            error("Should have no modules, but found: " .. table.concat(accessible, ", "))
        end
    )");
    EXPECT_TRUE(result.ok()) << engine_->lastError();
}

TEST_F(PluginSandbox, PluginWithConfigCapabilityOnly) {
    registerAllModules();
    std::vector<PluginCapability> caps = { PluginCapability::Config };
    engine_->initializeModules(caps);

    // Config modules: theme, shader, session, quick (all require Config)
    auto r1 = engine_->loadString("assert(terminal.theme ~= nil, 'theme should exist')");
    EXPECT_TRUE(r1.ok()) << engine_->lastError();

    auto r2 = engine_->loadString("assert(terminal.shader ~= nil, 'shader should exist')");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();

    auto r3 = engine_->loadString("assert(terminal.session ~= nil, 'session should exist')");
    EXPECT_TRUE(r3.ok()) << engine_->lastError();

    auto r4 = engine_->loadString("assert(terminal.quick ~= nil, 'quick should exist')");
    EXPECT_TRUE(r4.ok()) << engine_->lastError();

    // Non-Config modules should be nil
    auto r5 = engine_->loadString("assert(terminal.tab == nil, 'tab should be nil')");
    EXPECT_TRUE(r5.ok()) << engine_->lastError();

    auto r6 = engine_->loadString("assert(terminal.event == nil, 'event should be nil')");
    EXPECT_TRUE(r6.ok()) << engine_->lastError();

    auto r7 = engine_->loadString("assert(terminal.mux == nil, 'mux should be nil')");
    EXPECT_TRUE(r7.ok()) << engine_->lastError();

    auto r8 = engine_->loadString("assert(terminal.clipboard == nil, 'clipboard should be nil')");
    EXPECT_TRUE(r8.ok()) << engine_->lastError();
}

TEST_F(PluginSandbox, PluginWithEventsCapabilityOnly) {
    registerAllModules();
    std::vector<PluginCapability> caps = { PluginCapability::Events };
    engine_->initializeModules(caps);

    // Events modules: event, tab, url, git, workspace
    auto r1 = engine_->loadString("assert(terminal.event ~= nil, 'event should exist')");
    EXPECT_TRUE(r1.ok()) << engine_->lastError();

    auto r2 = engine_->loadString("assert(terminal.tab ~= nil, 'tab should exist')");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();

    auto r3 = engine_->loadString("assert(terminal.url ~= nil, 'url should exist')");
    EXPECT_TRUE(r3.ok()) << engine_->lastError();

    auto r4 = engine_->loadString("assert(terminal.git ~= nil, 'git should exist')");
    EXPECT_TRUE(r4.ok()) << engine_->lastError();

    auto r5 = engine_->loadString("assert(terminal.workspace ~= nil, 'workspace should exist')");
    EXPECT_TRUE(r5.ok()) << engine_->lastError();

    // Non-Events modules should be nil
    auto r6 = engine_->loadString("assert(terminal.theme == nil, 'theme should be nil')");
    EXPECT_TRUE(r6.ok()) << engine_->lastError();

    auto r7 = engine_->loadString("assert(terminal.command == nil, 'command should be nil')");
    EXPECT_TRUE(r7.ok()) << engine_->lastError();

    auto r8 = engine_->loadString("assert(terminal.notify == nil, 'notify should be nil')");
    EXPECT_TRUE(r8.ok()) << engine_->lastError();
}

TEST_F(PluginSandbox, PluginCannotAccessDisallowedModules) {
    // Only register event + theme, init with Events only
    engine_->registerModule(std::make_shared<LuaEventModule>());
    engine_->registerModule(std::make_shared<LuaThemeModule>(nullptr));
    engine_->registerModule(std::make_shared<LuaNotifyModule>(nullptr));

    std::vector<PluginCapability> caps = { PluginCapability::Events };
    engine_->initializeModules(caps);

    // event requires Events -> accessible
    auto r1 = engine_->loadString("assert(terminal.event ~= nil)");
    EXPECT_TRUE(r1.ok()) << engine_->lastError();

    // theme requires Config -> NOT accessible
    auto r2 = engine_->loadString("assert(terminal.theme == nil)");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();

    // notify requires Notifications -> NOT accessible
    auto r3 = engine_->loadString("assert(terminal.notify == nil)");
    EXPECT_TRUE(r3.ok()) << engine_->lastError();
}

// --- Dangerous function removal tests ---

TEST_F(PluginSandbox, DangerousLuaFunctionsRemoved) {
    registerAllModules();
    engine_->initializeModules();

    // The os library is not opened, so the entire 'os' global should be nil
    auto r1 = engine_->loadString("assert(os == nil, 'os should be nil')");
    EXPECT_TRUE(r1.ok()) << engine_->lastError();

    // io library is also not opened
    auto r2 = engine_->loadString("assert(io == nil, 'io should be nil')");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();

    // load and loadfile are removed by the engine's sandbox initialization
    auto r5 = engine_->loadString("assert(load == nil, 'load should be nil')");
    EXPECT_TRUE(r5.ok()) << engine_->lastError();

    auto r6 = engine_->loadString("assert(loadfile == nil, 'loadfile should be nil')");
    EXPECT_TRUE(r6.ok()) << engine_->lastError();
}

TEST_F(PluginSandbox, RequireIsDisabled) {
    registerAllModules();
    engine_->initializeModules();

    // Disable require as sandbox would
    engine_->loadString("require = nil");

    auto result = engine_->loadString("assert(require == nil, 'require should be nil')");
    EXPECT_TRUE(result.ok()) << engine_->lastError();
}

// --- Plugin discovery tests ---

TEST_F(PluginSandbox, PluginScanDirectory) {
    TempPluginDir tmp;

    // Create two valid plugins
    tmp.createPlugin("my_plugin",
        R"(return { name = "my_plugin", version = "1.0", capabilities = {"events"} })",
        "-- init.lua\n");

    tmp.createPlugin("another_plugin",
        R"(return { name = "another_plugin", version = "0.1", capabilities = {"config"} })",
        "-- init.lua\n");

    // Create a directory without plugin.lua (should be ignored)
    std::filesystem::create_directories(tmp.root / "not_a_plugin");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    const auto& plugins = mgr.plugins();
    EXPECT_EQ(plugins.size(), 2u);

    // Verify both plugins were found
    bool found_my = false, found_another = false;
    for (const auto& p : plugins) {
        if (p.metadata.name == "my_plugin") {
            found_my = true;
            EXPECT_EQ(p.state, PluginState::Discovered);
            EXPECT_TRUE(mgr.hasCapability("my_plugin", PluginCapability::Events));
            EXPECT_FALSE(mgr.hasCapability("my_plugin", PluginCapability::Config));
        }
        if (p.metadata.name == "another_plugin") {
            found_another = true;
            EXPECT_EQ(p.state, PluginState::Discovered);
            EXPECT_TRUE(mgr.hasCapability("another_plugin", PluginCapability::Config));
        }
    }
    EXPECT_TRUE(found_my) << "my_plugin not found";
    EXPECT_TRUE(found_another) << "another_plugin not found";
}

TEST_F(PluginSandbox, MalformedPluginLuaHandled) {
    TempPluginDir tmp;

    // Create a plugin with invalid Lua syntax in plugin.lua
    tmp.createPlugin("bad_plugin",
        "this is not valid lua {{{{",
        "-- init.lua\n");

    // Create a plugin where plugin.lua returns non-table
    tmp.createPlugin("non_table_plugin",
        "return 42",
        "-- init.lua\n");

    PluginManager mgr(*engine_);
    // Should not crash
    mgr.scanDirectory(tmp.root.string());

    const auto& plugins = mgr.plugins();
    EXPECT_EQ(plugins.size(), 2u);

    // Both should be in Error state
    for (const auto& p : plugins) {
        EXPECT_EQ(p.state, PluginState::Error)
            << "Plugin " << p.metadata.name << " should be in Error state";
        EXPECT_FALSE(p.error_message.empty())
            << "Plugin " << p.metadata.name << " should have an error message";
    }
}

TEST_F(PluginSandbox, PluginUnloadCleansCallbacks) {
    TempPluginDir tmp;

    tmp.createPlugin("cb_plugin",
        R"(return { name = "cb_plugin", version = "1.0", capabilities = {"events"} })",
        R"(
            terminal.event.on("bell", function() end)
            terminal.event.on("title_change", function() end)
        )");

    // Register event module so plugin can use terminal.event.on
    engine_->registerModule(std::make_shared<LuaEventModule>());
    engine_->initializeModules();

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    auto load_result = mgr.loadPlugin("cb_plugin");
    EXPECT_TRUE(load_result.ok()) << load_result.errorMessage();

    // Verify plugin is loaded
    const auto& plugins = mgr.plugins();
    auto it = std::find_if(plugins.begin(), plugins.end(),
        [](const PluginInfo& p) { return p.metadata.name == "cb_plugin"; });
    ASSERT_NE(it, plugins.end());
    EXPECT_EQ(it->state, PluginState::Loaded);

    // Unload the plugin
    mgr.unloadPlugin("cb_plugin");

    // Verify state is Disabled
    it = std::find_if(mgr.plugins().begin(), mgr.plugins().end(),
        [](const PluginInfo& p) { return p.metadata.name == "cb_plugin"; });
    ASSERT_NE(it, mgr.plugins().end());
    EXPECT_EQ(it->state, PluginState::Disabled);

    // Attempting to reload a disabled plugin should fail
    auto reload_result = mgr.loadPlugin("cb_plugin");
    EXPECT_FALSE(reload_result.ok());
}

// --- Plugin auto-load and sandbox restore tests ---

TEST_F(PluginSandbox, ScanAndAutoLoadAllDiscoveredPlugins) {
    TempPluginDir tmp;

    // Create plugins that set global markers when loaded
    tmp.createPlugin("plugin_a",
        R"(return { name = "plugin_a", version = "1.0", capabilities = {"events"} })",
        R"(__plugin_a_loaded = true)");

    tmp.createPlugin("plugin_b",
        R"(return { name = "plugin_b", version = "2.0", capabilities = {"config"} })",
        R"(__plugin_b_loaded = true)");

    engine_->registerModule(std::make_shared<LuaEventModule>());
    engine_->initializeModules();

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    // Auto-load all discovered plugins (mirrors initTerminal() logic)
    for (const auto& info : mgr.plugins()) {
        if (info.state == PluginState::Discovered) {
            mgr.loadPlugin(info.metadata.name);
        }
    }

    // Verify both plugins were loaded
    int loaded = 0;
    for (const auto& info : mgr.plugins()) {
        if (info.state == PluginState::Loaded) loaded++;
    }
    EXPECT_EQ(loaded, 2);

    // Verify init.lua was executed for both
    auto r1 = engine_->loadString("assert(__plugin_a_loaded == true, 'plugin_a not loaded')");
    EXPECT_TRUE(r1.ok()) << engine_->lastError();

    auto r2 = engine_->loadString("assert(__plugin_b_loaded == true, 'plugin_b not loaded')");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();
}

TEST_F(PluginSandbox, SandboxRestoresGlobalsAfterPluginLoad) {
    TempPluginDir tmp;

    // Plugin without Events capability — terminal.on should be nil'd during load
    tmp.createPlugin("restricted_plugin",
        R"(return { name = "restricted_plugin", version = "1.0", capabilities = {"config"} })",
        R"(
            -- terminal.on should be nil inside this plugin's sandbox
            __on_was_nil = (terminal.on == nil)
        )");

    engine_->registerModule(std::make_shared<LuaEventModule>());
    engine_->initializeModules();

    // Verify terminal.on exists before plugin load
    auto r_before = engine_->loadString("assert(terminal.on ~= nil, 'on should exist before')");
    EXPECT_TRUE(r_before.ok()) << engine_->lastError();

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());
    auto result = mgr.loadPlugin("restricted_plugin");
    EXPECT_TRUE(result.ok()) << result.errorMessage();

    // Verify terminal.on was nil'd during plugin execution
    auto r_check = engine_->loadString("assert(__on_was_nil == true, 'on should have been nil in sandbox')");
    EXPECT_TRUE(r_check.ok()) << engine_->lastError();

    // Verify terminal.on is restored after plugin load
    auto r_after = engine_->loadString("assert(terminal.on ~= nil, 'on should be restored after')");
    EXPECT_TRUE(r_after.ok()) << engine_->lastError();
}

TEST_F(PluginSandbox, MultiplePluginsDontInterfereWithEachOther) {
    TempPluginDir tmp;

    // First plugin: has events only
    tmp.createPlugin("events_only",
        R"(return { name = "events_only", version = "1.0", capabilities = {"events"} })",
        R"(
            __events_plugin_on = (terminal.on ~= nil)
            __events_plugin_config = (terminal.config ~= nil)
        )");

    // Second plugin: has config only
    tmp.createPlugin("config_only",
        R"(return { name = "config_only", version = "1.0", capabilities = {"config"} })",
        R"(
            __config_plugin_on = (terminal.on ~= nil)
            __config_plugin_config = (terminal.config ~= nil)
        )");

    engine_->registerModule(std::make_shared<LuaEventModule>());
    engine_->initializeModules();

    // Also set terminal.config to a value so sandbox can nil/restore it
    engine_->loadString("terminal.config = terminal.config or function() end");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    // Load events_only first
    mgr.loadPlugin("events_only");
    // Load config_only second
    mgr.loadPlugin("config_only");

    // events_only should have had terminal.on available
    auto r1 = engine_->loadString("assert(__events_plugin_on == true, 'events plugin should see on')");
    EXPECT_TRUE(r1.ok()) << engine_->lastError();

    // config_only should NOT have had terminal.on available (events not in caps)
    auto r2 = engine_->loadString("assert(__config_plugin_on == false, 'config plugin should not see on')");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();

    // After all plugins loaded, terminal.on should still be available
    auto r3 = engine_->loadString("assert(terminal.on ~= nil, 'on should be restored')");
    EXPECT_TRUE(r3.ok()) << engine_->lastError();
}

TEST_F(PluginSandbox, ErrorPluginDoesNotBlockOthers) {
    TempPluginDir tmp;

    // First plugin: will error during init
    tmp.createPlugin("bad_init",
        R"(return { name = "bad_init", version = "1.0", capabilities = {"events"} })",
        R"(error("intentional failure"))");

    // Second plugin: should load fine
    tmp.createPlugin("good_plugin",
        R"(return { name = "good_plugin", version = "1.0", capabilities = {"events"} })",
        R"(__good_plugin_loaded = true)");

    engine_->registerModule(std::make_shared<LuaEventModule>());
    engine_->initializeModules();

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    // Load all discovered plugins (bad_init will fail)
    for (const auto& info : mgr.plugins()) {
        if (info.state == PluginState::Discovered) {
            mgr.loadPlugin(info.metadata.name);
        }
    }

    // bad_init should be in Error state
    bool found_bad = false, found_good = false;
    for (const auto& p : mgr.plugins()) {
        if (p.metadata.name == "bad_init") {
            found_bad = true;
            EXPECT_EQ(p.state, PluginState::Error);
            EXPECT_FALSE(p.error_message.empty());
        }
        if (p.metadata.name == "good_plugin") {
            found_good = true;
            EXPECT_EQ(p.state, PluginState::Loaded);
        }
    }
    EXPECT_TRUE(found_bad);
    EXPECT_TRUE(found_good);

    // good_plugin should have executed successfully
    auto r = engine_->loadString("assert(__good_plugin_loaded == true)");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(PluginSandbox, EmptyDirectoryProducesNoPlugins) {
    TempPluginDir tmp;
    // Don't create any plugins

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    EXPECT_EQ(mgr.plugins().size(), 0u);
}

TEST_F(PluginSandbox, NonexistentDirectoryProducesNoPlugins) {
    PluginManager mgr(*engine_);
    mgr.scanDirectory("/nonexistent/path/that/does/not/exist");

    EXPECT_EQ(mgr.plugins().size(), 0u);
}

TEST_F(PluginSandbox, PluginsDirectoryHelperReturnsValidPath) {
    std::string dir = pluginsDirectory();
    // Should end with "plugins" and contain .bt
    if (!dir.empty()) {
        EXPECT_NE(dir.find("plugins"), std::string::npos);
        EXPECT_NE(dir.find(".bt"), std::string::npos);
    }
    // If HOME/USERPROFILE not set, empty is acceptable
}

// --- Single-file plugin tests ---

TEST_F(PluginSandbox, SingleFilePluginScansAndLoads) {
    TempPluginDir tmp;

    tmp.createSingleFilePlugin("hello.lua",
        R"(
            plugin = {
                name = "hello",
                version = "1.0",
                capabilities = {"events"},
            }
            __hello_single_loaded = true
        )");

    engine_->registerModule(std::make_shared<LuaEventModule>());
    engine_->initializeModules();

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    // Should be discovered
    const auto& plugins = mgr.plugins();
    ASSERT_EQ(plugins.size(), 1u);
    EXPECT_EQ(plugins[0].metadata.name, "hello");
    EXPECT_EQ(plugins[0].state, PluginState::Discovered);

    // Should load successfully
    auto result = mgr.loadPlugin("hello");
    EXPECT_TRUE(result.ok()) << result.errorMessage();

    // Verify init code executed
    auto r = engine_->loadString(
        "assert(__hello_single_loaded == true, 'single file not loaded')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(PluginSandbox, SingleFilePluginWithMetadata) {
    TempPluginDir tmp;

    tmp.createSingleFilePlugin("my-tool.lua",
        R"(
            plugin = {
                name = "my-tool",
                version = "2.5.0",
                author = "Test Author",
                description = "A test tool plugin",
                capabilities = {"config", "events"},
            }
        )");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    const auto& plugins = mgr.plugins();
    ASSERT_EQ(plugins.size(), 1u);

    const auto& meta = plugins[0].metadata;
    EXPECT_EQ(meta.name, "my-tool");
    EXPECT_EQ(meta.version, "2.5.0");
    EXPECT_EQ(meta.author, "Test Author");
    EXPECT_EQ(meta.description, "A test tool plugin");
    EXPECT_TRUE(mgr.hasCapability("my-tool", PluginCapability::Config));
    EXPECT_TRUE(mgr.hasCapability("my-tool", PluginCapability::Events));
    EXPECT_FALSE(mgr.hasCapability("my-tool", PluginCapability::FileSystem));
}

TEST_F(PluginSandbox, SingleFilePluginWithoutMetadata) {
    TempPluginDir tmp;

    // No 'plugin' table — should use filename and grant all capabilities
    tmp.createSingleFilePlugin("auto-plugin.lua",
        R"(__auto_plugin_loaded = true)");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    const auto& plugins = mgr.plugins();
    ASSERT_EQ(plugins.size(), 1u);

    EXPECT_EQ(plugins[0].metadata.name, "auto-plugin");
    EXPECT_EQ(plugins[0].state, PluginState::Discovered);

    // All mapped capabilities should be granted
    EXPECT_TRUE(mgr.hasCapability("auto-plugin", PluginCapability::Events));
    EXPECT_TRUE(mgr.hasCapability("auto-plugin", PluginCapability::Config));
    EXPECT_TRUE(mgr.hasCapability("auto-plugin", PluginCapability::FileSystem));
    EXPECT_TRUE(mgr.hasCapability("auto-plugin", PluginCapability::Keybindings));
    EXPECT_TRUE(mgr.hasCapability("auto-plugin", PluginCapability::Notifications));
    EXPECT_TRUE(mgr.hasCapability("auto-plugin", PluginCapability::PaneRead));
    EXPECT_TRUE(mgr.hasCapability("auto-plugin", PluginCapability::PaneWrite));
}

TEST_F(PluginSandbox, DirectoryPluginTakesPrecedence) {
    TempPluginDir tmp;

    // Create both a directory-based and single-file plugin with the same name
    tmp.createPlugin("dupe",
        R"(return { name = "dupe", version = "1.0", capabilities = {"events"} })",
        R"(__dupe_dir_loaded = true)");

    tmp.createSingleFilePlugin("dupe.lua",
        R"(
            plugin = {
                name = "dupe",
                version = "2.0",
                capabilities = {"config"},
            }
            __dupe_file_loaded = true
        )");

    engine_->registerModule(std::make_shared<LuaEventModule>());
    engine_->initializeModules();

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    // Only one plugin should be discovered (the directory-based one)
    const auto& plugins = mgr.plugins();
    ASSERT_EQ(plugins.size(), 1u);
    EXPECT_EQ(plugins[0].metadata.name, "dupe");
    EXPECT_EQ(plugins[0].metadata.version, "1.0");
    EXPECT_TRUE(mgr.hasCapability("dupe", PluginCapability::Events));
    EXPECT_FALSE(mgr.hasCapability("dupe", PluginCapability::Config));

    // Load and verify directory version runs
    auto result = mgr.loadPlugin("dupe");
    EXPECT_TRUE(result.ok()) << result.errorMessage();

    auto r = engine_->loadString(
        "assert(__dupe_dir_loaded == true, 'directory plugin should load')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    auto r2 = engine_->loadString(
        "assert(__dupe_file_loaded == nil, 'file plugin should not load')");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();
}
