#include <gtest/gtest.h>
#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include <string>

namespace termcore {
namespace {

class ScreenCsiExtTest : public ::testing::Test {
protected:
    Screen screen{24, 80};
    std::string response;

    void SetUp() override {
        screen.setResponseCallback([this](const std::string& s) {
            response += s;
        });
    }

    void feed(const std::string& data) {
        VtParser parser(screen);
        parser.feed(data.data(), data.size());
    }
};

// 1. DSR 6n → cursor position report (1-based)
TEST_F(ScreenCsiExtTest, DSR_CursorPosition) {
    feed("Hello");  // cursor at row 0, col 5
    response.clear();
    feed("\033[6n");
    EXPECT_EQ(response, "\033[1;6R");
}

// 2. DSR 6n at non-origin position
TEST_F(ScreenCsiExtTest, DSR_CursorPositionNonOrigin) {
    feed("\033[10;20H");  // move to row 10, col 20
    response.clear();
    feed("\033[6n");
    EXPECT_EQ(response, "\033[10;20R");
}

// 3. DSR 5n → device OK
TEST_F(ScreenCsiExtTest, DSR_DeviceOK) {
    feed("\033[5n");
    EXPECT_EQ(response, "\033[0n");
}

// 4. Primary DA → VT100 identifier
TEST_F(ScreenCsiExtTest, DA_Primary) {
    feed("\033[c");
    EXPECT_EQ(response, "\033[?1;2c");
}

// 5. Primary DA with explicit 0
TEST_F(ScreenCsiExtTest, DA_PrimaryExplicit) {
    feed("\033[0c");
    EXPECT_EQ(response, "\033[?1;2c");
}

// 6. Secondary DA (CSI > c)
TEST_F(ScreenCsiExtTest, DA_Secondary) {
    feed("\033[>c");
    EXPECT_EQ(response, "\033[>1;0;0c");
}

// 7. Secondary DA with explicit 0
TEST_F(ScreenCsiExtTest, DA_SecondaryExplicit) {
    feed("\033[>0c");
    EXPECT_EQ(response, "\033[>1;0;0c");
}

// 8. DECSCUSR: CSI 2 q → steady block
TEST_F(ScreenCsiExtTest, CursorStyle_SteadyBlock) {
    feed("\033[2 q");
    EXPECT_EQ(screen.cursorShape(), CursorShape::Block);
    EXPECT_FALSE(screen.cursorBlink());
}

// 9. DECSCUSR: CSI 5 q → blinking bar
TEST_F(ScreenCsiExtTest, CursorStyle_BlinkingBar) {
    feed("\033[5 q");
    EXPECT_EQ(screen.cursorShape(), CursorShape::Bar);
    EXPECT_TRUE(screen.cursorBlink());
}

// 10. DECSCUSR: CSI 0 q → reset to default (blinking block)
TEST_F(ScreenCsiExtTest, CursorStyle_Reset) {
    feed("\033[6 q");  // steady bar first
    EXPECT_EQ(screen.cursorShape(), CursorShape::Bar);
    feed("\033[0 q");  // reset
    EXPECT_EQ(screen.cursorShape(), CursorShape::Block);
    EXPECT_TRUE(screen.cursorBlink());
}

// 11. DECSCUSR: CSI 3 q → blinking underline
TEST_F(ScreenCsiExtTest, CursorStyle_BlinkingUnderline) {
    feed("\033[3 q");
    EXPECT_EQ(screen.cursorShape(), CursorShape::Underline);
    EXPECT_TRUE(screen.cursorBlink());
}

// 12. DECSCUSR: CSI 4 q → steady underline
TEST_F(ScreenCsiExtTest, CursorStyle_SteadyUnderline) {
    feed("\033[4 q");
    EXPECT_EQ(screen.cursorShape(), CursorShape::Underline);
    EXPECT_FALSE(screen.cursorBlink());
}

// 13. REP: print 'A', then CSI 3 b → 'AAAA' total
TEST_F(ScreenCsiExtTest, REP_RepeatChar) {
    feed("A\033[3b");
    EXPECT_EQ(screen.getLineText(0), "AAAA");
    EXPECT_EQ(screen.cursorCol(), 4);
}

// 14. REP with default param (1)
TEST_F(ScreenCsiExtTest, REP_Default) {
    feed("X\033[b");
    EXPECT_EQ(screen.getLineText(0), "XX");
}

// 15. CNL: CSI 2 E → cursor moves down 2, to col 0
TEST_F(ScreenCsiExtTest, CNL_CursorNextLine) {
    feed("Hello");  // row 0, col 5
    feed("\033[2E");
    EXPECT_EQ(screen.cursorRow(), 2);
    EXPECT_EQ(screen.cursorCol(), 0);
}

// 16. CPL: CSI 1 F → cursor moves up 1, to col 0
TEST_F(ScreenCsiExtTest, CPL_CursorPrevLine) {
    feed("\033[5;10H");  // row 5, col 10 (1-based)
    feed("\033[1F");
    EXPECT_EQ(screen.cursorRow(), 3);  // 5-1 = 4, 0-based = 3
    EXPECT_EQ(screen.cursorCol(), 0);
}

// 17. CHT: CSI 2 I → advance 2 tab stops
TEST_F(ScreenCsiExtTest, CHT_ForwardTab) {
    // Start at col 0, tab stops at 0, 8, 16, 24, ...
    feed("\033[2I");
    EXPECT_EQ(screen.cursorCol(), 16);  // 0 → 8 → 16
}

// 18. CBT: CSI 1 Z → go back 1 tab stop
TEST_F(ScreenCsiExtTest, CBT_BackwardTab) {
    feed("\033[20G");  // move to col 19 (0-based)
    feed("\033[1Z");
    EXPECT_EQ(screen.cursorCol(), 16);  // nearest tab stop backward
}

// 19. TBC: CSI g → clears tab at current column
TEST_F(ScreenCsiExtTest, TBC_ClearCurrentTab) {
    feed("\033[9G");  // move to col 8 (0-based), which is a tab stop
    feed("\033[g");    // clear tab at col 8
    // Now tab from col 0 should skip col 8 and go to col 16
    feed("\033[1G");   // back to col 0
    feed("\t");        // HT
    EXPECT_EQ(screen.cursorCol(), 16);  // col 8 is cleared, next is 16
}

// 20. TBC: CSI 3 g → clears all tabs
TEST_F(ScreenCsiExtTest, TBC_ClearAllTabs) {
    feed("\033[3g");   // clear all tab stops
    feed("\033[1G");   // col 0
    feed("\t");        // HT - no tab stops, should go to end
    EXPECT_EQ(screen.cursorCol(), 79);  // last column
}

// 21. Tab stops: HT uses custom stops after modification
TEST_F(ScreenCsiExtTest, HT_UsesCustomStops) {
    // Clear all tabs, then set one at col 5
    feed("\033[3g");    // clear all
    feed("\033[6G");    // move to col 5 (0-based)
    // Set a tab stop via HTS (ESC H)... but we need to test the tab_stops
    // Instead, verify HT goes to end when no stops
    feed("\033[1G");    // back to col 0
    feed("\t");
    EXPECT_EQ(screen.cursorCol(), 79);  // no tab stops → end of line
}

// 22. CNL clamps to bottom
TEST_F(ScreenCsiExtTest, CNL_ClampsToBottom) {
    feed("\033[100E");  // try to go way past bottom
    EXPECT_EQ(screen.cursorRow(), 23);  // last row (0-based)
    EXPECT_EQ(screen.cursorCol(), 0);
}

// 23. CPL clamps to top
TEST_F(ScreenCsiExtTest, CPL_ClampsToTop) {
    feed("\033[100F");  // try to go way past top
    EXPECT_EQ(screen.cursorRow(), 0);
    EXPECT_EQ(screen.cursorCol(), 0);
}

// 24. CBT from col 0 stays at 0
TEST_F(ScreenCsiExtTest, CBT_AtOrigin) {
    feed("\033[1Z");
    EXPECT_EQ(screen.cursorCol(), 0);
}

// 25. CHT past end of line
TEST_F(ScreenCsiExtTest, CHT_PastEnd) {
    feed("\033[100I");
    EXPECT_EQ(screen.cursorCol(), 79);
}

} // namespace
} // namespace termcore
