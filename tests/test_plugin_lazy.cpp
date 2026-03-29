#include <gtest/gtest.h>
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include "termcore/lua_engine.h"
#include "termcore/plugin_manager.h"
#include "lua_bindings/lua_event_module.h"

#include <filesystem>
#include <fstream>

using namespace termcore;

class PluginLazyTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<LuaEngine>();
        event_module_ = std::make_shared<LuaEventModule>();
        engine_->registerModule(event_module_);
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
            root = std::filesystem::temp_directory_path() / "bread_test_lazy";
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
    std::shared_ptr<LuaEventModule> event_module_;
};

TEST_F(PluginLazyTest, LazyPluginNotLoadedAtStartup) {
    TempPluginDir tmp;

    tmp.createPlugin("lazy_plugin",
        R"(return { name = "lazy_plugin", version = "1.0", capabilities = {"events"}, lazy = true, on_event = "bell" })",
        R"(__lazy_loaded = true)");

    tmp.createPlugin("eager_plugin",
        R"(return { name = "eager_plugin", version = "1.0", capabilities = {"events"} })",
        R"(__eager_loaded = true)");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    auto result = mgr.loadAll();
    EXPECT_TRUE(result.ok()) << result.errorMessage();

    // Eager plugin should be loaded
    auto r1 = engine_->loadString("assert(__eager_loaded == true, 'eager should be loaded')");
    EXPECT_TRUE(r1.ok()) << engine_->lastError();

    // Lazy plugin should NOT be loaded
    auto r2 = engine_->loadString("assert(__lazy_loaded == nil, 'lazy should not be loaded')");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();

    // Verify state
    for (const auto& p : mgr.plugins()) {
        if (p.metadata.name == "lazy_plugin") {
            EXPECT_EQ(p.state, PluginState::Lazy);
        }
        if (p.metadata.name == "eager_plugin") {
            EXPECT_EQ(p.state, PluginState::Loaded);
        }
    }
}

TEST_F(PluginLazyTest, LazyPluginLoadedOnEvent) {
    TempPluginDir tmp;

    tmp.createPlugin("lazy_bell",
        R"(return { name = "lazy_bell", version = "1.0", capabilities = {"events"}, lazy = true, on_event = "bell" })",
        R"(__lazy_bell_loaded = true)");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());
    mgr.loadAll();

    // Not loaded yet
    auto r1 = engine_->loadString("assert(__lazy_bell_loaded == nil, 'should not be loaded yet')");
    EXPECT_TRUE(r1.ok()) << engine_->lastError();

    // Trigger the event
    mgr.checkLazyEvent("bell");

    // Now it should be loaded
    auto r2 = engine_->loadString("assert(__lazy_bell_loaded == true, 'should be loaded after event')");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();
}

TEST_F(PluginLazyTest, LazyPluginLoadedOnCommand) {
    TempPluginDir tmp;

    tmp.createPlugin("lazy_cmd",
        R"(return { name = "lazy_cmd", version = "1.0", capabilities = {"events"}, lazy = true, on_command = "open_panel" })",
        R"(__lazy_cmd_loaded = true)");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());
    mgr.loadAll();

    // Not loaded yet
    auto r1 = engine_->loadString("assert(__lazy_cmd_loaded == nil)");
    EXPECT_TRUE(r1.ok()) << engine_->lastError();

    // Trigger the command
    mgr.checkLazyCommand("open_panel");

    // Now it should be loaded
    auto r2 = engine_->loadString("assert(__lazy_cmd_loaded == true, 'should be loaded after command')");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();
}

TEST_F(PluginLazyTest, LazyPluginTransitionsToLoaded) {
    TempPluginDir tmp;

    tmp.createPlugin("lazy_transition",
        R"(return { name = "lazy_transition", version = "1.0", capabilities = {"events"}, lazy = true, on_event = "resize" })",
        R"(__lazy_transition_loaded = true)");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());
    mgr.loadAll();

    // Check initial state is Lazy
    const auto& plugins = mgr.plugins();
    auto it = std::find_if(plugins.begin(), plugins.end(),
        [](const PluginInfo& p) { return p.metadata.name == "lazy_transition"; });
    ASSERT_NE(it, plugins.end());
    EXPECT_EQ(it->state, PluginState::Lazy);

    // Trigger event
    mgr.checkLazyEvent("resize");

    // State should now be Loaded
    it = std::find_if(mgr.plugins().begin(), mgr.plugins().end(),
        [](const PluginInfo& p) { return p.metadata.name == "lazy_transition"; });
    ASSERT_NE(it, mgr.plugins().end());
    EXPECT_EQ(it->state, PluginState::Loaded);
}
