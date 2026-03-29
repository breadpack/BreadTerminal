#include <gtest/gtest.h>
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include "termcore/lua_engine.h"
#include "termcore/plugin_manager.h"
#include "lua_bindings/lua_event_module.h"

#include <filesystem>
#include <fstream>

using namespace termcore;

class PluginDependenciesTest : public ::testing::Test {
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
            root = std::filesystem::temp_directory_path() / "bread_test_deps";
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

TEST_F(PluginDependenciesTest, TopologicalSortBasic) {
    TempPluginDir tmp;

    // B depends on A, so A should load first
    tmp.createPlugin("plugin_a",
        R"(return { name = "plugin_a", version = "1.0.0", capabilities = {"events"} })",
        R"(__load_order = __load_order or {} ; table.insert(__load_order, "a"))");

    tmp.createPlugin("plugin_b",
        R"(return { name = "plugin_b", version = "1.0.0", capabilities = {"events"}, dependencies = {"plugin_a"} })",
        R"(__load_order = __load_order or {} ; table.insert(__load_order, "b"))");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    auto order_result = mgr.resolveLoadOrder();
    ASSERT_TRUE(order_result.ok()) << order_result.errorMessage();

    const auto& order = order_result.value();
    ASSERT_EQ(order.size(), 2u);

    // Find positions
    auto pos_a = std::find(order.begin(), order.end(), "plugin_a");
    auto pos_b = std::find(order.begin(), order.end(), "plugin_b");
    ASSERT_NE(pos_a, order.end());
    ASSERT_NE(pos_b, order.end());
    EXPECT_LT(pos_a, pos_b) << "plugin_a should come before plugin_b";
}

TEST_F(PluginDependenciesTest, TopologicalSortWithAfter) {
    TempPluginDir tmp;

    // C should load after A (via 'after' field)
    tmp.createPlugin("plugin_a",
        R"(return { name = "plugin_a", version = "1.0.0", capabilities = {"events"} })",
        R"(--)");

    tmp.createPlugin("plugin_c",
        R"(return { name = "plugin_c", version = "1.0.0", capabilities = {"events"}, after = {"plugin_a"} })",
        R"(--)");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    auto order_result = mgr.resolveLoadOrder();
    ASSERT_TRUE(order_result.ok()) << order_result.errorMessage();

    const auto& order = order_result.value();
    auto pos_a = std::find(order.begin(), order.end(), "plugin_a");
    auto pos_c = std::find(order.begin(), order.end(), "plugin_c");
    ASSERT_NE(pos_a, order.end());
    ASSERT_NE(pos_c, order.end());
    EXPECT_LT(pos_a, pos_c) << "plugin_a should come before plugin_c";
}

TEST_F(PluginDependenciesTest, CircularDependencyDetected) {
    TempPluginDir tmp;

    // A depends on B, B depends on A
    tmp.createPlugin("plugin_a",
        R"(return { name = "plugin_a", version = "1.0.0", capabilities = {"events"}, dependencies = {"plugin_b"} })",
        R"(--)");

    tmp.createPlugin("plugin_b",
        R"(return { name = "plugin_b", version = "1.0.0", capabilities = {"events"}, dependencies = {"plugin_a"} })",
        R"(--)");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    auto order_result = mgr.resolveLoadOrder();
    EXPECT_FALSE(order_result.ok());
    EXPECT_NE(order_result.errorMessage().find("circular"), std::string::npos);
}

TEST_F(PluginDependenciesTest, DependencyVersionCheck) {
    TempPluginDir tmp;

    tmp.createPlugin("lib_utils",
        R"(return { name = "lib_utils", version = "0.3.0", capabilities = {"events"} })",
        R"(--)");

    // Requires lib_utils >= 0.2.0 — should succeed since 0.3.0 >= 0.2.0
    tmp.createPlugin("my_plugin",
        R"(return { name = "my_plugin", version = "1.0.0", capabilities = {"events"}, dependencies = {"lib_utils >= 0.2.0"} })",
        R"(--)");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    auto order_result = mgr.resolveLoadOrder();
    EXPECT_TRUE(order_result.ok()) << order_result.errorMessage();
}

TEST_F(PluginDependenciesTest, DependencyVersionCheckFails) {
    TempPluginDir tmp;

    tmp.createPlugin("lib_utils",
        R"(return { name = "lib_utils", version = "0.1.0", capabilities = {"events"} })",
        R"(--)");

    // Requires lib_utils >= 0.2.0 — should fail since 0.1.0 < 0.2.0
    tmp.createPlugin("my_plugin",
        R"(return { name = "my_plugin", version = "1.0.0", capabilities = {"events"}, dependencies = {"lib_utils >= 0.2.0"} })",
        R"(--)");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    auto order_result = mgr.resolveLoadOrder();
    EXPECT_FALSE(order_result.ok());
    EXPECT_NE(order_result.errorMessage().find("lib_utils"), std::string::npos);
}

TEST_F(PluginDependenciesTest, LoadAllRespectsOrder) {
    TempPluginDir tmp;

    // B depends on A — A should load first
    tmp.createPlugin("plugin_a",
        R"(return { name = "plugin_a", version = "1.0.0", capabilities = {"events"} })",
        R"(__load_order = __load_order or {} ; table.insert(__load_order, "a"))");

    tmp.createPlugin("plugin_b",
        R"(return { name = "plugin_b", version = "1.0.0", capabilities = {"events"}, dependencies = {"plugin_a"} })",
        R"(__load_order = __load_order or {} ; table.insert(__load_order, "b"))");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    auto result = mgr.loadAll();
    EXPECT_TRUE(result.ok()) << result.errorMessage();

    // Verify load order: a before b
    auto r = engine_->loadString(R"(
        assert(__load_order ~= nil, 'load_order not set')
        assert(#__load_order == 2, 'expected 2 plugins loaded, got ' .. #__load_order)
        assert(__load_order[1] == 'a', 'expected a first, got ' .. __load_order[1])
        assert(__load_order[2] == 'b', 'expected b second, got ' .. __load_order[2])
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(PluginDependenciesTest, MissingDependencyReportsError) {
    TempPluginDir tmp;

    // Depends on nonexistent plugin
    tmp.createPlugin("orphan",
        R"(return { name = "orphan", version = "1.0.0", capabilities = {"events"}, dependencies = {"missing_plugin"} })",
        R"(--)");

    PluginManager mgr(*engine_);
    mgr.scanDirectory(tmp.root.string());

    auto order_result = mgr.resolveLoadOrder();
    EXPECT_FALSE(order_result.ok());
    EXPECT_NE(order_result.errorMessage().find("missing_plugin"), std::string::npos);
}
