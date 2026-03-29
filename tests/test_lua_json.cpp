#include <gtest/gtest.h>

#if !TERMCORE_HAS_LUA
TEST(LuaJson, Disabled) { GTEST_SKIP() << "Lua not available"; }
#else

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include "termcore/lua_engine.h"
#include "lua_bindings/lua_json_module.h"

using namespace termcore;

class LuaJsonTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<LuaEngine>();
        engine_->registerModule(std::make_shared<LuaJsonModule>());
        engine_->initializeModules();
    }

    void TearDown() override {
        if (engine_) {
            engine_->clearAllModules();
        }
        engine_.reset();
    }

    std::unique_ptr<LuaEngine> engine_;
};

TEST_F(LuaJsonTest, JsonEncodeString) {
    auto result = engine_->loadString(R"(
        local s = terminal.json.encode("hello")
        assert(s == '"hello"', "expected '\"hello\"', got: " .. s)
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
}

TEST_F(LuaJsonTest, JsonEncodeNumber) {
    auto result = engine_->loadString(R"(
        local s = terminal.json.encode(42)
        assert(s == "42", "expected '42', got: " .. s)

        local s2 = terminal.json.encode(3.14)
        assert(s2:find("3.14") ~= nil, "expected '3.14' in: " .. s2)
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
}

TEST_F(LuaJsonTest, JsonEncodeBool) {
    auto result = engine_->loadString(R"(
        assert(terminal.json.encode(true) == "true")
        assert(terminal.json.encode(false) == "false")
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
}

TEST_F(LuaJsonTest, JsonEncodeTable) {
    auto result = engine_->loadString(R"(
        local s = terminal.json.encode({name = "test", count = 42})
        -- Should be a JSON object
        assert(s:find('"name"') ~= nil, "missing name key")
        assert(s:find('"test"') ~= nil, "missing test value")
        assert(s:find('"count"') ~= nil, "missing count key")
        assert(s:find('42') ~= nil, "missing 42 value")
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
}

TEST_F(LuaJsonTest, JsonEncodeArray) {
    auto result = engine_->loadString(R"(
        local s = terminal.json.encode({1, 2, 3})
        assert(s == "[1,2,3]", "expected '[1,2,3]', got: " .. s)
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
}

TEST_F(LuaJsonTest, JsonEncodeNested) {
    auto result = engine_->loadString(R"(
        local s = terminal.json.encode({
            items = {1, 2, 3},
            meta = {nested = true}
        })
        assert(s:find('"items"') ~= nil, "missing items")
        assert(s:find('"nested"') ~= nil, "missing nested")
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
}

TEST_F(LuaJsonTest, JsonDecodeString) {
    auto result = engine_->loadString(R"(
        local v = terminal.json.decode('"hello"')
        assert(v == "hello", "expected 'hello', got: " .. tostring(v))
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
}

TEST_F(LuaJsonTest, JsonDecodeToTable) {
    auto result = engine_->loadString(R"(
        local data = terminal.json.decode('{"name":"test","count":42}')
        assert(data.name == "test", "name mismatch")
        assert(data.count == 42, "count mismatch")
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
}

TEST_F(LuaJsonTest, JsonRoundTrip) {
    auto result = engine_->loadString(R"(
        local original = {
            name = "round-trip",
            count = 99,
            items = {10, 20, 30},
            flag = true,
        }
        local encoded = terminal.json.encode(original)
        local decoded = terminal.json.decode(encoded)

        assert(decoded.name == "round-trip", "name mismatch")
        assert(decoded.count == 99, "count mismatch")
        assert(#decoded.items == 3, "items length mismatch")
        assert(decoded.items[1] == 10, "items[1]")
        assert(decoded.items[2] == 20, "items[2]")
        assert(decoded.items[3] == 30, "items[3]")
        assert(decoded.flag == true, "flag mismatch")
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
}

TEST_F(LuaJsonTest, JsonPrettyPrint) {
    auto result = engine_->loadString(R"(
        local s = terminal.json.encode({a = 1}, {pretty = true, indent = 2})
        -- Pretty output should contain newlines
        assert(s:find("\n") ~= nil, "expected newlines in pretty output")
        assert(s:find('"a"') ~= nil, "missing key")
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
}

TEST_F(LuaJsonTest, JsonEncodeDepthLimit) {
    // Build a deeply nested table that exceeds depth 50
    auto result = engine_->loadString(R"(
        local t = {val = "leaf"}
        for i = 1, 55 do
            t = {child = t}
        end
        local ok, err = pcall(function()
            terminal.json.encode(t)
        end)
        assert(not ok, "expected depth limit error")
        assert(tostring(err):find("depth") ~= nil,
               "expected 'depth' in error: " .. tostring(err))
    )");
    EXPECT_TRUE(result.ok()) << result.errorMessage();
}

#endif // TERMCORE_HAS_LUA
