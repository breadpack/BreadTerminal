// Tests for LuaWindowModule (terminal.window floating window API).

#include <gtest/gtest.h>

#if !TERMCORE_HAS_LUA
TEST(LuaWindow, Disabled) { GTEST_SKIP() << "Lua not available"; }
#else

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include "termcore/lua_engine.h"
#include "termcore/lua_module.h"
#include "lua_bindings/lua_window_module.h"

using namespace termcore;

class LuaWindowTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<LuaEngine>();
        windowMod_ = std::make_shared<LuaWindowModule>();
        engine_->registerModule(windowMod_);
        engine_->initializeModules();
    }

    void TearDown() override {
        if (engine_) {
            engine_->clearAllModules();
        }
        engine_.reset();
    }

    std::unique_ptr<LuaEngine> engine_;
    std::shared_ptr<LuaWindowModule> windowMod_;
};

// ---------------------------------------------------------------------------
TEST_F(LuaWindowTest, WindowOpenReturnsUserdata) {
    auto r = engine_->loadString(R"(
        local win = terminal.window.open({
            width = 80,
            height = 25,
            title = "Test Window",
            border = "single",
        })
        assert(win ~= nil, "window.open must return a value")
        assert(win:is_open() == true, "newly opened window should be open")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // Verify from C++ side
    ASSERT_EQ(windowMod_->windows().size(), 1u);
    const auto& w = *windowMod_->windows()[0];
    EXPECT_EQ(w.width, 80);
    EXPECT_EQ(w.height, 25);
    EXPECT_EQ(w.title, "Test Window");
    EXPECT_EQ(w.border_style, "single");
    EXPECT_TRUE(w.open);
}

// ---------------------------------------------------------------------------
TEST_F(LuaWindowTest, WindowSetLinesAndGetLines) {
    auto r = engine_->loadString(R"(
        local win = terminal.window.open({ width = 40, height = 10 })
        win:set_lines({"Hello", "World", "Foo"})

        local lines = win:get_lines()
        assert(#lines == 3, "expected 3 lines, got " .. #lines)
        assert(lines[1] == "Hello")
        assert(lines[2] == "World")
        assert(lines[3] == "Foo")

        -- get single line (0-indexed)
        assert(win:get_line(0) == "Hello")
        assert(win:get_line(1) == "World")

        -- set_lines with start_row
        win:set_lines({"Replaced"}, 1)
        assert(win:get_line(1) == "Replaced")
        assert(win:get_line(0) == "Hello")  -- unchanged
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
TEST_F(LuaWindowTest, WindowCursorMovement) {
    auto r = engine_->loadString(R"(
        local win = terminal.window.open({ width = 40, height = 10 })

        win:set_cursor(5, 10)
        local row, col = win:get_cursor()
        assert(row == 5, "expected row 5, got " .. row)
        assert(col == 10, "expected col 10, got " .. col)

        -- Relative move
        win:move_cursor(-2, 3)
        row, col = win:get_cursor()
        assert(row == 3, "expected row 3, got " .. row)
        assert(col == 13, "expected col 13, got " .. col)

        -- Clamp to zero
        win:move_cursor(-100, -100)
        row, col = win:get_cursor()
        assert(row == 0, "expected row 0")
        assert(col == 0, "expected col 0")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
TEST_F(LuaWindowTest, WindowKeyHandler) {
    auto r = engine_->loadString(R"(
        _G.key_pressed = ""
        local win = terminal.window.open({ width = 40, height = 10 })
        win:on_key("q", function() _G.key_pressed = "q_handler" end)
        win:on_key("enter", function() _G.key_pressed = "enter_handler" end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // Dispatch from C++ side
    uint64_t wid = windowMod_->windows()[0]->id;
    windowMod_->dispatchKey(wid, "q");

    r = engine_->loadString("assert(_G.key_pressed == 'q_handler')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    windowMod_->dispatchKey(wid, "enter");
    r = engine_->loadString("assert(_G.key_pressed == 'enter_handler')");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
TEST_F(LuaWindowTest, WindowCloseCallsCallback) {
    auto r = engine_->loadString(R"(
        _G.close_called = false
        local win = terminal.window.open({ width = 40, height = 10 })
        win:on_close(function() _G.close_called = true end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // Close from C++
    uint64_t wid = windowMod_->windows()[0]->id;
    windowMod_->closeWindow(wid);

    r = engine_->loadString("assert(_G.close_called == true)");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // Also verify is_open returns false
    r = engine_->loadString(R"(
        -- Window was captured in earlier chunk; re-query from C++ state
    )");
    EXPECT_FALSE(windowMod_->windows()[0]->open);
}

// ---------------------------------------------------------------------------
TEST_F(LuaWindowTest, WindowHighlightRange) {
    auto r = engine_->loadString(R"(
        local win = terminal.window.open({ width = 40, height = 10 })
        win:set_highlight(0, 2, 8, {
            fg = "#f38ba8",
            bg = "#313244",
            bold = true,
            italic = false,
            underline = true,
        })
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    const auto& w = *windowMod_->windows()[0];
    ASSERT_EQ(w.highlights.size(), 1u);
    EXPECT_EQ(w.highlights[0].row, 0);
    EXPECT_EQ(w.highlights[0].start_col, 2);
    EXPECT_EQ(w.highlights[0].end_col, 8);
    EXPECT_EQ(w.highlights[0].fg, 0xf38ba8u);
    EXPECT_EQ(w.highlights[0].bg, 0x313244u);
    EXPECT_TRUE(w.highlights[0].bold);
    EXPECT_FALSE(w.highlights[0].italic);
    EXPECT_TRUE(w.highlights[0].underline);
}

// ---------------------------------------------------------------------------
TEST_F(LuaWindowTest, ClearCallbacksClosesAllWindows) {
    auto r = engine_->loadString(R"(
        _G.close_count = 0
        local w1 = terminal.window.open({ width = 40, height = 10, title = "A" })
        w1:on_close(function() _G.close_count = _G.close_count + 1 end)
        local w2 = terminal.window.open({ width = 40, height = 10, title = "B" })
        w2:on_close(function() _G.close_count = _G.close_count + 1 end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
    EXPECT_EQ(windowMod_->windows().size(), 2u);

    // clearCallbacks should close all windows and fire on_close
    windowMod_->clearCallbacks();
    EXPECT_TRUE(windowMod_->windows().empty());

    r = engine_->loadString("assert(_G.close_count == 2)");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
TEST_F(LuaWindowTest, MultipleWindowsIndependent) {
    auto r = engine_->loadString(R"(
        local w1 = terminal.window.open({ width = 30, height = 10, title = "Win1" })
        local w2 = terminal.window.open({ width = 50, height = 20, title = "Win2" })

        w1:set_lines({"w1 line"})
        w2:set_lines({"w2 line"})

        assert(w1:get_line(0) == "w1 line")
        assert(w2:get_line(0) == "w2 line")

        w1:close()
        assert(w1:is_open() == false)
        assert(w2:is_open() == true)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    EXPECT_FALSE(windowMod_->windows()[0]->open);
    EXPECT_TRUE(windowMod_->windows()[1]->open);
}

// ---------------------------------------------------------------------------
TEST_F(LuaWindowTest, WindowCloseFromLua) {
    auto r = engine_->loadString(R"(
        _G.was_closed = false
        local win = terminal.window.open({ width = 40, height = 10 })
        win:on_close(function() _G.was_closed = true end)
        win:close()
        assert(win:is_open() == false)
        assert(_G.was_closed == true)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
TEST_F(LuaWindowTest, WindowResizeAndSetTitle) {
    auto r = engine_->loadString(R"(
        local win = terminal.window.open({ width = 40, height = 10, title = "Old" })
        win:resize(80, 30)
        win:set_title("New Title")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    const auto& w = *windowMod_->windows()[0];
    EXPECT_EQ(w.width, 80);
    EXPECT_EQ(w.height, 30);
    EXPECT_EQ(w.title, "New Title");
}

// ---------------------------------------------------------------------------
TEST_F(LuaWindowTest, WindowScrollAndCallback) {
    auto r = engine_->loadString(R"(
        _G.scroll_offset_received = -1
        local win = terminal.window.open({ width = 40, height = 10 })
        win:on_scroll(function(offset)
            _G.scroll_offset_received = offset
        end)
        win:scroll_to(42)
        assert(win:get_scroll_offset() == 42)
        assert(_G.scroll_offset_received == 42)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

// ---------------------------------------------------------------------------
TEST_F(LuaWindowTest, WindowStyleParsing) {
    auto r = engine_->loadString(R"(
        local win = terminal.window.open({
            width = 40, height = 10,
            style = {
                background = "#112233",
                foreground = "#aabbcc",
                border_color = "#ddeeff",
                title_color = "#001122",
            },
        })
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    const auto& w = *windowMod_->windows()[0];
    EXPECT_EQ(w.style.background, 0x112233u);
    EXPECT_EQ(w.style.foreground, 0xaabbccu);
    EXPECT_EQ(w.style.border_color, 0xddeeffu);
    EXPECT_EQ(w.style.title_color, 0x001122u);
}

#endif // TERMCORE_HAS_LUA
