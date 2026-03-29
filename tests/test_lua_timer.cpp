#include <gtest/gtest.h>

#if !TERMCORE_HAS_LUA
TEST(LuaTimerTest, Disabled) { GTEST_SKIP() << "Lua not available"; }
#else

#include <termcore/lua_engine.h>
#include <termcore/lua_module.h>
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_bindings/lua_timer_module.h"

using termcore::LuaEngine;
using termcore::LuaTimerModule;

class LuaTimerTest : public ::testing::Test {
protected:
    void SetUp() override {
        timer_ = std::make_shared<LuaTimerModule>();
        engine_.registerModule(timer_);
        engine_.initializeModules();

        engine_.registerFunction("test_capture",
            [this](const std::string& s) -> std::string {
                captured_ += s;
                return "";
            });
    }

    LuaEngine engine_;
    std::shared_ptr<LuaTimerModule> timer_;
    std::string captured_;
};

TEST_F(LuaTimerTest, TimerOnceFiresAfterDelay) {
    ASSERT_TRUE(engine_.loadString(R"(
        terminal.timer.once(100, function()
            terminal.test_capture("fired")
        end)
    )").ok());

    // Not enough time has passed
    EXPECT_EQ(timer_->tick(50), 0);
    EXPECT_EQ(captured_, "");

    // Now enough time has passed
    EXPECT_EQ(timer_->tick(100), 1);
    EXPECT_EQ(captured_, "fired");

    // Should not fire again (one-shot)
    EXPECT_EQ(timer_->tick(200), 0);
    EXPECT_EQ(captured_, "fired");
}

TEST_F(LuaTimerTest, TimerEveryFiresRepeatedly) {
    ASSERT_TRUE(engine_.loadString(R"(
        terminal.timer.every(100, function()
            terminal.test_capture("tick;")
        end)
    )").ok());

    EXPECT_EQ(timer_->tick(100), 1);
    EXPECT_EQ(captured_, "tick;");

    EXPECT_EQ(timer_->tick(200), 1);
    EXPECT_EQ(captured_, "tick;tick;");

    EXPECT_EQ(timer_->tick(300), 1);
    EXPECT_EQ(captured_, "tick;tick;tick;");
}

TEST_F(LuaTimerTest, TimerCancelStopsFiring) {
    ASSERT_TRUE(engine_.loadString(R"(
        timer_id = terminal.timer.every(100, function()
            terminal.test_capture("tick;")
        end)
    )").ok());

    EXPECT_EQ(timer_->tick(100), 1);
    EXPECT_EQ(captured_, "tick;");

    // Cancel the timer
    ASSERT_TRUE(engine_.loadString("terminal.timer.cancel(timer_id)").ok());

    EXPECT_EQ(timer_->tick(200), 0);
    EXPECT_EQ(captured_, "tick;");
}

TEST_F(LuaTimerTest, TimerDebounceResetsOnReCall) {
    ASSERT_TRUE(engine_.loadString(R"(
        debounced = terminal.timer.debounce(100, function()
            terminal.test_capture("debounced")
        end)
    )").ok());

    // Call debounce function
    ASSERT_TRUE(engine_.loadString("debounced()").ok());
    EXPECT_EQ(timer_->activeTimerCount(), 1u);

    // Call again before timer fires - should reset
    ASSERT_TRUE(engine_.loadString("debounced()").ok());
    // Old timer cancelled, new timer added
    EXPECT_EQ(timer_->activeTimerCount(), 1u);

    // Tick past the delay
    EXPECT_EQ(timer_->tick(100), 1);
    EXPECT_EQ(captured_, "debounced");
}

TEST_F(LuaTimerTest, ClearCallbacksRemovesAllTimers) {
    ASSERT_TRUE(engine_.loadString(R"(
        terminal.timer.once(100, function()
            terminal.test_capture("a")
        end)
        terminal.timer.every(200, function()
            terminal.test_capture("b")
        end)
    )").ok());

    EXPECT_TRUE(timer_->hasPendingTimers());

    timer_->clearCallbacks();

    EXPECT_FALSE(timer_->hasPendingTimers());
    EXPECT_EQ(timer_->tick(300), 0);
    EXPECT_EQ(captured_, "");
}

TEST_F(LuaTimerTest, TickWithNoTimersDoesNothing) {
    EXPECT_EQ(timer_->tick(0), 0);
    EXPECT_EQ(timer_->tick(1000), 0);
    EXPECT_FALSE(timer_->hasPendingTimers());
}

#endif // TERMCORE_HAS_LUA
