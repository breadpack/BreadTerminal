#include <gtest/gtest.h>

#if !TERMCORE_HAS_LUA
TEST(LuaAsyncTest, Disabled) { GTEST_SKIP() << "Lua not available"; }
#else

#include <termcore/lua_engine.h>
#include <termcore/lua_module.h>
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_bindings/lua_async_module.h"
#include "lua_bindings/lua_timer_module.h"

using termcore::LuaEngine;
using termcore::LuaAsyncModule;
using termcore::LuaTimerModule;

class LuaAsyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        timer_ = std::make_shared<LuaTimerModule>();
        async_ = std::make_shared<LuaAsyncModule>();
        async_->setTimerModule(timer_.get());

        engine_.registerModule(timer_);
        engine_.registerModule(async_);
        engine_.initializeModules();

        engine_.registerFunction("test_capture",
            [this](const std::string& s) -> std::string {
                captured_ += s;
                return "";
            });
    }

    LuaEngine engine_;
    std::shared_ptr<LuaTimerModule> timer_;
    std::shared_ptr<LuaAsyncModule> async_;
    std::string captured_;
};

TEST_F(LuaAsyncTest, AsyncFunctionRunsAsCoroutine) {
    ASSERT_TRUE(engine_.loadString(R"(
        terminal.async(function()
            terminal.test_capture("hello")
        end)
    )").ok());

    // The async function should be pending
    EXPECT_TRUE(async_->hasPendingCoroutines());

    // Tick should resume and complete it
    async_->tick(0);
    EXPECT_EQ(captured_, "hello");
    EXPECT_FALSE(async_->hasPendingCoroutines());
}

TEST_F(LuaAsyncTest, YieldAndResumeOnTick) {
    ASSERT_TRUE(engine_.loadString(R"(
        terminal.async(function()
            terminal.test_capture("before;")
            coroutine.yield()
            terminal.test_capture("after;")
        end)
    )").ok());

    // First tick: runs until yield
    async_->tick(0);
    EXPECT_EQ(captured_, "before;");

    // Second tick: resumes after yield
    async_->tick(1);
    EXPECT_EQ(captured_, "before;after;");
    EXPECT_FALSE(async_->hasPendingCoroutines());
}

TEST_F(LuaAsyncTest, MultipleAsyncTasksRunConcurrently) {
    ASSERT_TRUE(engine_.loadString(R"(
        terminal.async(function()
            terminal.test_capture("A1;")
            coroutine.yield()
            terminal.test_capture("A2;")
        end)
        terminal.async(function()
            terminal.test_capture("B1;")
            coroutine.yield()
            terminal.test_capture("B2;")
        end)
    )").ok());

    EXPECT_EQ(async_->activeCoroutineCount(), 2u);

    // First tick resumes both up to their yields
    async_->tick(0);
    EXPECT_EQ(captured_, "A1;B1;");

    // Second tick completes both
    async_->tick(1);
    EXPECT_EQ(captured_, "A1;B1;A2;B2;");
    EXPECT_FALSE(async_->hasPendingCoroutines());
}

TEST_F(LuaAsyncTest, ClearCallbacksStopsCoroutines) {
    ASSERT_TRUE(engine_.loadString(R"(
        terminal.async(function()
            terminal.test_capture("start;")
            coroutine.yield()
            terminal.test_capture("should_not_run;")
        end)
    )").ok());

    async_->tick(0);
    EXPECT_EQ(captured_, "start;");

    async_->clearCallbacks();
    EXPECT_FALSE(async_->hasPendingCoroutines());

    async_->tick(1);
    EXPECT_EQ(captured_, "start;");
}

#endif // TERMCORE_HAS_LUA
