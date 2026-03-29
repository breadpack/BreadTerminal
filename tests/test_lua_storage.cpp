#include <gtest/gtest.h>

#if !TERMCORE_HAS_LUA
TEST(LuaStorage, Disabled) { GTEST_SKIP() << "Lua not available"; }
#else

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include "termcore/lua_engine.h"
#include "lua_bindings/lua_storage_module.h"

#include <filesystem>
#include <fstream>

using namespace termcore;
namespace fs = std::filesystem;

class LuaStorageTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Use a temp directory for test data
        data_dir_ = (fs::temp_directory_path() / "bt_test_storage").string();
        fs::create_directories(data_dir_);

        engine_ = std::make_unique<LuaEngine>();
        storage_ = std::make_shared<LuaStorageModule>();
        storage_->setDataDirectory(data_dir_);
        engine_->registerModule(storage_);
        engine_->initializeModules();
    }

    void TearDown() override {
        if (engine_) {
            engine_->clearAllModules();
        }
        engine_.reset();
        storage_.reset();

        // Clean up temp directory
        std::error_code ec;
        fs::remove_all(data_dir_, ec);
    }

    std::string data_dir_;
    std::unique_ptr<LuaEngine> engine_;
    std::shared_ptr<LuaStorageModule> storage_;
};

TEST_F(LuaStorageTest, StorageOpenCreatesNamespace) {
    auto result = engine_->loadString(R"(
        local store = terminal.storage.open("test-plugin")
        assert(store ~= nil, "store should not be nil")
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
}

TEST_F(LuaStorageTest, StorageSetAndGet) {
    auto result = engine_->loadString(R"(
        local store = terminal.storage.open("test-plugin")
        store:set("name", "hello")
        store:set("count", 42)
        store:set("flag", true)

        assert(store:get("name") == "hello", "string get failed")
        assert(store:get("count") == 42, "number get failed")
        assert(store:get("flag") == true, "bool get failed")
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
}

TEST_F(LuaStorageTest, StorageGetWithDefault) {
    auto result = engine_->loadString(R"(
        local store = terminal.storage.open("test-plugin")
        local val = store:get("missing", "default_value")
        assert(val == "default_value", "default value not returned, got: " .. tostring(val))

        -- nil key should return default
        local val2 = store:get("also_missing", 99)
        assert(val2 == 99, "numeric default not returned")

        -- existing key should NOT use default
        store:set("exists", "real")
        local val3 = store:get("exists", "default")
        assert(val3 == "real", "should return actual value, not default")
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
}

TEST_F(LuaStorageTest, StorageDeleteKey) {
    auto result = engine_->loadString(R"(
        local store = terminal.storage.open("test-plugin")
        store:set("key", "value")
        assert(store:has("key") == true)
        store:delete("key")
        assert(store:has("key") == false)
        assert(store:get("key") == nil)
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
}

TEST_F(LuaStorageTest, StorageHasKey) {
    auto result = engine_->loadString(R"(
        local store = terminal.storage.open("test-plugin")
        assert(store:has("missing") == false)
        store:set("present", 1)
        assert(store:has("present") == true)
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
}

TEST_F(LuaStorageTest, StorageKeys) {
    auto result = engine_->loadString(R"(
        local store = terminal.storage.open("test-plugin")
        store:set("a", 1)
        store:set("b", 2)
        store:set("c", 3)
        local k = store:keys()
        assert(#k == 3, "expected 3 keys, got " .. #k)
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
}

TEST_F(LuaStorageTest, StorageClear) {
    auto result = engine_->loadString(R"(
        local store = terminal.storage.open("test-plugin")
        store:set("a", 1)
        store:set("b", 2)
        store:clear()
        assert(#store:keys() == 0, "expected 0 keys after clear")
        assert(store:has("a") == false)
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
}

TEST_F(LuaStorageTest, StoragePersistsToDisk) {
    // Write data and save
    {
        auto result = engine_->loadString(R"(
            local store = terminal.storage.open("persist-test")
            store:set("name", "persisted")
            store:set("count", 100)
            store:save()
        )");
        EXPECT_TRUE(result.ok()) << result.errorMessage();
    }

    // Verify file exists
    auto file_path = fs::path(data_dir_) / "persist-test.json";
    EXPECT_TRUE(fs::exists(file_path));

    // Create a new module and read back
    engine_->clearAllModules();
    engine_.reset();

    engine_ = std::make_unique<LuaEngine>();
    auto storage2 = std::make_shared<LuaStorageModule>();
    storage2->setDataDirectory(data_dir_);
    engine_->registerModule(storage2);
    engine_->initializeModules();

    auto result = engine_->loadString(R"(
        local store = terminal.storage.open("persist-test")
        assert(store:get("name") == "persisted",
               "expected 'persisted', got: " .. tostring(store:get("name")))
        assert(store:get("count") == 100,
               "expected 100, got: " .. tostring(store:get("count")))
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
}

TEST_F(LuaStorageTest, StorageNestedTableRoundTrip) {
    auto result = engine_->loadString(R"(
        local store = terminal.storage.open("nested-test")
        store:set("data", {
            a = 1,
            b = "hello",
            c = {10, 20, 30},
            d = {nested = true}
        })
        store:save()

        -- Re-open and verify
        local store2 = terminal.storage.open("nested-test")
        local data = store2:get("data")
        assert(data.a == 1, "nested a")
        assert(data.b == "hello", "nested b")
        assert(#data.c == 3, "nested c length")
        assert(data.c[1] == 10, "nested c[1]")
        assert(data.c[2] == 20, "nested c[2]")
        assert(data.c[3] == 30, "nested c[3]")
        assert(data.d.nested == true, "nested d.nested")
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
}

#endif // TERMCORE_HAS_LUA
