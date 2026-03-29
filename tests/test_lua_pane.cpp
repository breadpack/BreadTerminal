// Tests for LuaPaneModule — pane access, overlays, and callbacks.

#include <gtest/gtest.h>

#if !TERMCORE_HAS_LUA
TEST(LuaPaneModule, Disabled) { GTEST_SKIP() << "Lua not available"; }
#else

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include "termcore/lua_engine.h"
#include "termcore/lua_module.h"
#include "lua_bindings/lua_pane_module.h"

using namespace termcore;

// ---- Mock provider ----

class MockPaneDataProvider : public IPaneDataProvider {
public:
    std::vector<PaneInfo> panes;
    PaneInfo active;
    std::vector<std::string> lines;
    std::pair<int, int> cursor_pos{0, 0};
    std::string selection_text;
    std::pair<int, int> sel_start{0, 0};
    std::pair<int, int> sel_end{0, 0};
    int scrollback = 0;
    std::vector<std::string> scrollback_lines;
    std::string last_sent_text;
    std::string last_sent_keys;

    std::vector<PaneInfo> listPanes() const override { return panes; }
    PaneInfo getActivePane() const override { return active; }

    std::vector<std::string> getPaneLines(uint32_t /*pane_id*/, int start, int end) const override {
        std::vector<std::string> result;
        for (int i = start; i <= end && i < static_cast<int>(lines.size()); ++i) {
            if (i >= 0) result.push_back(lines[i]);
        }
        return result;
    }

    std::string getPaneLine(uint32_t /*pane_id*/, int row) const override {
        if (row >= 0 && row < static_cast<int>(lines.size())) return lines[row];
        return "";
    }

    std::pair<int, int> getPaneCursor(uint32_t /*pane_id*/) const override {
        return cursor_pos;
    }

    std::string getPaneSelection(uint32_t /*pane_id*/) const override {
        return selection_text;
    }

    std::pair<int, int> getSelectionStart(uint32_t /*pane_id*/) const override {
        return sel_start;
    }

    std::pair<int, int> getSelectionEnd(uint32_t /*pane_id*/) const override {
        return sel_end;
    }

    int getScrollbackSize(uint32_t /*pane_id*/) const override {
        return scrollback;
    }

    std::vector<std::string> getScrollbackLines(uint32_t /*pane_id*/, int start, int count) const override {
        std::vector<std::string> result;
        for (int i = start; i < start + count && i < static_cast<int>(scrollback_lines.size()); ++i) {
            if (i >= 0) result.push_back(scrollback_lines[i]);
        }
        return result;
    }

    void sendText(uint32_t /*pane_id*/, const std::string& text) override {
        last_sent_text = text;
    }

    void sendKeys(uint32_t /*pane_id*/, const std::string& keys) override {
        last_sent_keys = keys;
    }
};

// ---- Test fixture ----

class LuaPaneTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine_ = std::make_unique<LuaEngine>();
        provider_ = std::make_unique<MockPaneDataProvider>();
        module_ = std::make_shared<LuaPaneModule>();
        module_->setProvider(provider_.get());

        // Setup default mock data
        PaneInfo p1;
        p1.id = 1;
        p1.rows = 24;
        p1.cols = 80;
        p1.title = "bash";
        p1.cwd = "/home/user";
        p1.process = "bash";
        p1.is_active = true;

        PaneInfo p2;
        p2.id = 2;
        p2.rows = 24;
        p2.cols = 80;
        p2.title = "vim";
        p2.cwd = "/home/user/src";
        p2.process = "vim";
        p2.is_active = false;

        provider_->panes = {p1, p2};
        provider_->active = p1;
        provider_->lines = {"hello world", "second line", "third line"};
        provider_->cursor_pos = {1, 5};
        provider_->scrollback = 100;
        provider_->scrollback_lines = {"scrollback line 1", "scrollback line 2"};

        engine_->registerModule(module_);
        engine_->initializeModules();
    }

    void TearDown() override {
        if (engine_) {
            engine_->clearAllModules();
        }
        engine_.reset();
        module_.reset();
        provider_.reset();
    }

    std::unique_ptr<LuaEngine> engine_;
    std::unique_ptr<MockPaneDataProvider> provider_;
    std::shared_ptr<LuaPaneModule> module_;
};

// ---- Tests ----

TEST_F(LuaPaneTest, PaneActiveReturnsHandle) {
    auto r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        assert(pane ~= nil, "active pane should not be nil")
        assert(pane:id() == 1, "active pane id should be 1")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaPaneTest, PaneGetLinesReturnsContent) {
    auto r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        local lines = pane:get_lines(1, 2)
        assert(#lines == 2, "should return 2 lines, got " .. #lines)
        assert(lines[1] == "hello world", "first line mismatch: " .. lines[1])
        assert(lines[2] == "second line", "second line mismatch: " .. lines[2])
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaPaneTest, PaneGetLineReturnsSingleLine) {
    auto r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        local line = pane:get_line(1)
        assert(line == "hello world", "line mismatch: " .. line)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaPaneTest, PaneCursorPosition) {
    auto r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        local row, col = pane:cursor()
        assert(row == 2, "cursor row should be 2, got " .. row)
        assert(col == 6, "cursor col should be 6, got " .. col)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaPaneTest, PaneGetSelection) {
    provider_->selection_text = "selected text";
    provider_->sel_start = {0, 3};
    provider_->sel_end = {0, 15};

    auto r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        local sel = pane:get_selection()
        assert(sel == "selected text", "selection mismatch: " .. tostring(sel))
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaPaneTest, PaneGetSelectionNil) {
    provider_->selection_text = "";  // no selection

    auto r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        local sel = pane:get_selection()
        assert(sel == nil, "should be nil when no selection")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaPaneTest, PaneSendText) {
    auto r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        pane:send_text("ls -la\n")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
    EXPECT_EQ(provider_->last_sent_text, "ls -la\n");
}

TEST_F(LuaPaneTest, PaneSendKeys) {
    auto r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        pane:send_keys("ctrl+c")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
    EXPECT_EQ(provider_->last_sent_keys, "ctrl+c");
}

TEST_F(LuaPaneTest, PaneVirtualTextAddRemove) {
    // Add virtual text via Lua
    auto r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        _G.mark_id = pane:add_virtual_text(1, 1, "virtual", {
            fg = "#89b4fa",
            bold = false,
            italic = true,
        })
        assert(_G.mark_id > 0, "mark id should be > 0")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    // Verify via C++ API
    auto& vts = module_->virtualTexts(1);
    ASSERT_EQ(vts.size(), 1u);
    EXPECT_EQ(vts[0].text, "virtual");
    EXPECT_EQ(vts[0].row, 0);  // 0-indexed internally
    EXPECT_EQ(vts[0].col, 0);
    EXPECT_EQ(vts[0].fg, 0x89b4fa);
    EXPECT_TRUE(vts[0].italic);
    EXPECT_FALSE(vts[0].bold);

    // Remove via Lua
    r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        pane:remove_virtual_text(_G.mark_id)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
    EXPECT_TRUE(module_->virtualTexts(1).empty());
}

TEST_F(LuaPaneTest, PaneHighlightAddRemove) {
    auto r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        _G.hl_id = pane:add_highlight(1, 1, 5, {
            fg = "#f38ba8",
            bg = "#1e1e2e",
            bold = true,
        })
        assert(_G.hl_id > 0, "highlight id should be > 0")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    auto& hls = module_->highlights(1);
    ASSERT_EQ(hls.size(), 1u);
    EXPECT_EQ(hls[0].row, 0);
    EXPECT_EQ(hls[0].start_col, 0);
    EXPECT_EQ(hls[0].end_col, 4);
    EXPECT_EQ(hls[0].fg, 0xf38ba8);
    EXPECT_EQ(hls[0].bg, 0x1e1e2e);
    EXPECT_TRUE(hls[0].bold);

    r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        pane:remove_highlight(_G.hl_id)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
    EXPECT_TRUE(module_->highlights(1).empty());
}

TEST_F(LuaPaneTest, PaneLineSignAddRemove) {
    auto r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        pane:set_line_sign(1, {
            text = ">>",
            color = "#f38ba8",
        })
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    auto& signs = module_->lineSigns(1);
    ASSERT_EQ(signs.size(), 1u);
    auto it = signs.find(0);  // row 0 (0-indexed)
    ASSERT_NE(it, signs.end());
    EXPECT_EQ(it->second.text, ">>");
    EXPECT_EQ(it->second.color, 0xf38ba8);

    r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        pane:remove_line_sign(1)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
    EXPECT_TRUE(module_->lineSigns(1).empty());
}

TEST_F(LuaPaneTest, PaneOnOutputCallback) {
    engine_->registerFunction("test_capture",
        [](const std::string& s) -> std::string {
            // Store in global for checking
            return s;
        });

    std::string captured;
    engine_->registerFunction("capture_output",
        [&](const std::string& s) -> std::string {
            captured = s;
            return "";
        });

    auto r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        pane:on_output(function(text)
            terminal.capture_output("got:" .. text)
        end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    module_->fireOutput(1, "hello");
    EXPECT_EQ(captured, "got:hello");
}

TEST_F(LuaPaneTest, PaneOnExitCallback) {
    std::string captured;
    engine_->registerFunction("capture_exit",
        [&](const std::string& s) -> std::string {
            captured = s;
            return "";
        });

    auto r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        pane:on_exit(function(code)
            terminal.capture_exit("exit:" .. tostring(code))
        end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    module_->fireExit(1, 42);
    EXPECT_EQ(captured, "exit:42");
}

TEST_F(LuaPaneTest, PaneListReturnAllPanes) {
    auto r = engine_->loadString(R"(
        local panes = terminal.pane.list()
        assert(#panes == 2, "should have 2 panes, got " .. #panes)
        assert(panes[1]:id() == 1, "first pane id should be 1")
        assert(panes[2]:id() == 2, "second pane id should be 2")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaPaneTest, ClearCallbacksClearsEverything) {
    // Add some overlays and callbacks
    auto r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        pane:add_virtual_text(1, 1, "test", {})
        pane:add_highlight(1, 1, 5, {})
        pane:set_line_sign(1, { text = ">>", color = 0 })
        pane:on_output(function(text) end)
        pane:on_exit(function(code) end)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();

    EXPECT_FALSE(module_->virtualTexts(1).empty());
    EXPECT_FALSE(module_->highlights(1).empty());
    EXPECT_FALSE(module_->lineSigns(1).empty());

    module_->clearCallbacks();

    EXPECT_TRUE(module_->virtualTexts(1).empty());
    EXPECT_TRUE(module_->highlights(1).empty());
    EXPECT_TRUE(module_->lineSigns(1).empty());

    // fireOutput should not crash after clear
    module_->fireOutput(1, "no crash");
    module_->fireExit(1, 0);
}

TEST_F(LuaPaneTest, PaneProperties) {
    auto r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        assert(pane:rows() == 24, "rows should be 24")
        assert(pane:cols() == 80, "cols should be 80")
        assert(pane:title() == "bash", "title should be bash")
        assert(pane:cwd() == "/home/user", "cwd mismatch")
        assert(pane:process() == "bash", "process mismatch")
        assert(pane:is_active() == true, "should be active")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaPaneTest, PaneGetById) {
    auto r = engine_->loadString(R"(
        local pane = terminal.pane.get(2)
        assert(pane ~= nil, "pane 2 should exist")
        assert(pane:id() == 2, "pane id should be 2")
        assert(pane:title() == "vim", "title should be vim")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaPaneTest, PaneGetInvalidId) {
    auto r = engine_->loadString(R"(
        local pane = terminal.pane.get(999)
        assert(pane == nil, "nonexistent pane should be nil")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaPaneTest, PaneGetVisibleLines) {
    auto r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        local lines = pane:get_visible_lines()
        assert(lines ~= nil, "visible lines should not be nil")
        assert(#lines == 3, "should have 3 visible lines, got " .. #lines)
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaPaneTest, PaneScrollback) {
    auto r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        assert(pane:scrollback_size() == 100, "scrollback size should be 100")
        local lines = pane:get_scrollback(1, 2)
        assert(#lines == 2, "should have 2 scrollback lines")
        assert(lines[1] == "scrollback line 1", "scrollback line 1 mismatch")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaPaneTest, PaneGetText) {
    auto r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        local text = pane:get_text(1, 1, 5)
        assert(text == "hello", "text should be 'hello', got '" .. text .. "'")
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
}

TEST_F(LuaPaneTest, PaneClearVirtualText) {
    auto r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        pane:add_virtual_text(1, 1, "a", {})
        pane:add_virtual_text(1, 2, "b", {})
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
    EXPECT_EQ(module_->virtualTexts(1).size(), 2u);

    r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        pane:clear_virtual_text()
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
    EXPECT_TRUE(module_->virtualTexts(1).empty());
}

TEST_F(LuaPaneTest, PaneClearHighlights) {
    auto r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        pane:add_highlight(1, 1, 5, {})
        pane:add_highlight(2, 1, 3, {})
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
    EXPECT_EQ(module_->highlights(1).size(), 2u);

    r = engine_->loadString(R"(
        local pane = terminal.pane.active()
        pane:clear_highlights()
    )");
    EXPECT_TRUE(r.ok()) << engine_->lastError();
    EXPECT_TRUE(module_->highlights(1).empty());
}

#endif // TERMCORE_HAS_LUA
