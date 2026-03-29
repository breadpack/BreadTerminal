#include <gtest/gtest.h>
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include "termcore/lua_engine.h"
#include "termcore/plugin_manager.h"
#include "lua_bindings/lua_event_module.h"

#include <filesystem>
#include <fstream>

using namespace termcore;

class PluginLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<LuaEngine>();
        engine_->registerModule(std::make_shared<LuaEventModule>());
        engine_->initializeModules();
    }

    void TearDown() override {
        if (engine_) {
            engine_->clearAllModules();
        }
        engine_.reset();
    }

    struct TempPluginDir {
        std::filesystem::path root;

        TempPluginDir() {
            root = std::filesystem::temp_directory_path() / "bread_test_lifecycle";
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
};

TEST_F(PluginLifecycleTest, SetupFunctionCalledOnLoad) {
    TempPluginDir tmp;
    tmp.createPlugin("setup_test",
        R"(return { name = "setup_test", version = "1.0", capabilities = {"events"} })",
        R"(
            __setup_called = false
            function setup(opts)
                __setup_called = true
                __setup_opts_type = type(opts)
            end
        )");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    auto result = mgr.loadPlugin("setup_test");
    EXPECT_TRUE(result.ok()) << result.errorMessage();

    auto r = engine_->loadString("assert(__setup_called == true, 'setup was not called')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    auto r2 = engine_->loadString("assert(__setup_opts_type == 'table', 'opts should be a table')");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();
}

TEST_F(PluginLifecycleTest, OnUnloadCalledOnUnload) {
    TempPluginDir tmp;
    tmp.createPlugin("unload_test",
        R"(return { name = "unload_test", version = "1.0", capabilities = {"events"} })",
        R"(
            __unload_called = false
            function on_unload()
                __unload_called = true
            end
        )");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    auto result = mgr.loadPlugin("unload_test");
    EXPECT_TRUE(result.ok()) << result.errorMessage();

    // Verify on_unload not called yet
    auto r1 = engine_->loadString("assert(__unload_called == false, 'on_unload called too early')");
    EXPECT_TRUE(r1.ok()) << engine_->lastError();

    mgr.unloadPlugin("unload_test");

    auto r2 = engine_->loadString("assert(__unload_called == true, 'on_unload was not called')");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();
}

TEST_F(PluginLifecycleTest, SetupNotCalledIfNotDefined) {
    TempPluginDir tmp;
    tmp.createPlugin("no_setup",
        R"(return { name = "no_setup", version = "1.0", capabilities = {"events"} })",
        R"(
            __no_setup_loaded = true
            -- No setup() function defined
        )");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    // Should not crash
    auto result = mgr.loadPlugin("no_setup");
    EXPECT_TRUE(result.ok()) << result.errorMessage();

    auto r = engine_->loadString("assert(__no_setup_loaded == true)");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(PluginLifecycleTest, OnUnloadNotCalledIfNotDefined) {
    TempPluginDir tmp;
    tmp.createPlugin("no_unload",
        R"(return { name = "no_unload", version = "1.0", capabilities = {"events"} })",
        R"(
            __no_unload_loaded = true
            -- No on_unload() function defined
        )");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    auto result = mgr.loadPlugin("no_unload");
    EXPECT_TRUE(result.ok()) << result.errorMessage();

    // Should not crash
    mgr.unloadPlugin("no_unload");

    // Verify plugin state is Disabled
    const auto& plugins = mgr.plugins();
    auto it = std::find_if(plugins.begin(), plugins.end(),
        [](const PluginInfo& p) { return p.metadata.name == "no_unload"; });
    ASSERT_NE(it, plugins.end());
    EXPECT_EQ(it->state, PluginState::Disabled);
}
