#include <gtest/gtest.h>
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include "termcore/lua_engine.h"
#include "lua_bindings/lua_event_module.h"

using namespace termcore;

class CustomEventsTest : public ::testing::Test {
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

    std::unique_ptr<LuaEngine> engine_;
    std::shared_ptr<LuaEventModule> event_module_;
};

TEST_F(CustomEventsTest, EventEmitFiresHandlers) {
    // Register a handler via Lua, then emit from Lua
    auto r1 = engine_->loadString(R"(
        __emit_count = 0
        terminal.event.on("custom:test", function()
            __emit_count = __emit_count + 1
        end)
    )");
    EXPECT_TRUE(r1.ok()) << engine_->lastError();

    // Emit the event
    auto r2 = engine_->loadString(R"(
        terminal.event.emit("custom:test")
    )");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();

    // Verify handler was called
    auto r3 = engine_->loadString("assert(__emit_count == 1, 'expected 1, got ' .. __emit_count)");
    EXPECT_TRUE(r3.ok()) << engine_->lastError();

    // Emit again
    engine_->loadString("terminal.event.emit('custom:test')");

    auto r4 = engine_->loadString("assert(__emit_count == 2, 'expected 2, got ' .. __emit_count)");
    EXPECT_TRUE(r4.ok()) << engine_->lastError();
}

TEST_F(CustomEventsTest, EventOnceFiresOnce) {
    auto r1 = engine_->loadString(R"(
        __once_count = 0
        terminal.event.once("custom:oneshot", function()
            __once_count = __once_count + 1
        end)
    )");
    EXPECT_TRUE(r1.ok()) << engine_->lastError();

    // Emit twice
    engine_->loadString("terminal.event.emit('custom:oneshot')");
    engine_->loadString("terminal.event.emit('custom:oneshot')");

    // Should have fired only once
    auto r2 = engine_->loadString("assert(__once_count == 1, 'expected 1, got ' .. __once_count)");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();
}

TEST_F(CustomEventsTest, EventOffRemovesHandlers) {
    auto r1 = engine_->loadString(R"(
        __off_count = 0
        terminal.event.on("custom:removable", function()
            __off_count = __off_count + 1
        end)
    )");
    EXPECT_TRUE(r1.ok()) << engine_->lastError();

    // Emit once to verify handler works
    engine_->loadString("terminal.event.emit('custom:removable')");
    auto r2 = engine_->loadString("assert(__off_count == 1)");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();

    // Remove handlers
    engine_->loadString("terminal.event.off('custom:removable')");

    // Emit again — should not fire
    engine_->loadString("terminal.event.emit('custom:removable')");
    auto r3 = engine_->loadString("assert(__off_count == 1, 'expected 1 after off, got ' .. __off_count)");
    EXPECT_TRUE(r3.ok()) << engine_->lastError();
}

TEST_F(CustomEventsTest, CustomEventNamespaced) {
    // Events with colons work as custom namespaced events
    auto r1 = engine_->loadString(R"(
        __ns_count = 0
        terminal.event.on("myplugin:data_ready", function()
            __ns_count = __ns_count + 1
        end)
        terminal.event.emit("myplugin:data_ready")
    )");
    EXPECT_TRUE(r1.ok()) << engine_->lastError();

    auto r2 = engine_->loadString("assert(__ns_count == 1)");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();
}

TEST_F(CustomEventsTest, EmitWithData) {
    auto r1 = engine_->loadString(R"(
        __event_data = nil
        terminal.event.on("custom:with_data", function(data)
            __event_data = data
        end)
        terminal.event.emit("custom:with_data", { key = "value", num = 42 })
    )");
    EXPECT_TRUE(r1.ok()) << engine_->lastError();

    auto r2 = engine_->loadString(R"(
        assert(__event_data ~= nil, 'data should not be nil')
        assert(__event_data.key == 'value', 'key mismatch')
        assert(__event_data.num == 42, 'num mismatch')
    )");
    EXPECT_TRUE(r2.ok()) << engine_->lastError();
}
