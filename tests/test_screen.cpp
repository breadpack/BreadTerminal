#include <gtest/gtest.h>
#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include <string>

namespace termcore {
namespace {

class ScreenTest : public ::testing::Test {
protected:
    Screen screen{24, 80};

    void feed(const std::string& data) {
        VtParser parser(screen);
        parser.feed(data.data(), data.size());
    }
};

// 1. Initial state
TEST_F(ScreenTest, InitialState) {
    EXPECT_EQ(screen.cursorRow(), 0);
    EXPECT_EQ(screen.cursorCol(), 0);
    EXPECT_TRUE(screen.cursorVisible());
    EXPECT_EQ(screen.rows(), 24);
    EXPECT_EQ(screen.cols(), 80);
    EXPECT_EQ(screen.cellAt(0, 0).codepoint, U' ');
    EXPECT_EQ(screen.scrollbackSize(), 0u);
}

// 2. Print characters
TEST_F(ScreenTest, PrintCharacters) {
    feed("Hello");
    EXPECT_EQ(screen.cellAt(0, 0).codepoint, U'H');
    EXPECT_EQ(screen.cellAt(0, 1).codepoint, U'e');
    EXPECT_EQ(screen.cellAt(0, 2).codepoint, U'l');
    EXPECT_EQ(screen.cellAt(0, 3).codepoint, U'l');
    EXPECT_EQ(screen.cellAt(0, 4).codepoint, U'o');
    EXPECT_EQ(screen.cursorRow(), 0);
    EXPECT_EQ(screen.cursorCol(), 5);
}

// 3. Line wrap
TEST_F(ScreenTest, LineWrap) {
    Screen small(2, 5);
    VtParser parser(small);
    std::string data = "ABCDEFG";
    parser.feed(data.data(), data.size());

    // First line: ABCDE
    EXPECT_EQ(small.cellAt(0, 0).codepoint, U'A');
    EXPECT_EQ(small.cellAt(0, 4).codepoint, U'E');
    // Second line: FG
    EXPECT_EQ(small.cellAt(1, 0).codepoint, U'F');
    EXPECT_EQ(small.cellAt(1, 1).codepoint, U'G');
    EXPECT_EQ(small.cursorRow(), 1);
    EXPECT_EQ(small.cursorCol(), 2);
}

// 4. LF/CR
TEST_F(ScreenTest, LineFeedCarriageReturn) {
    feed("AB\r\nCD");
    EXPECT_EQ(screen.cellAt(0, 0).codepoint, U'A');
    EXPECT_EQ(screen.cellAt(0, 1).codepoint, U'B');
    EXPECT_EQ(screen.cellAt(1, 0).codepoint, U'C');
    EXPECT_EQ(screen.cellAt(1, 1).codepoint, U'D');
    EXPECT_EQ(screen.cursorRow(), 1);
    EXPECT_EQ(screen.cursorCol(), 2);
}

// 5. Cursor movement: CUU, CUD, CUF, CUB
TEST_F(ScreenTest, CursorMovement) {
    // Move to (5, 10)
    feed("\x1B[6;11H");
    EXPECT_EQ(screen.cursorRow(), 5);
    EXPECT_EQ(screen.cursorCol(), 10);

    // CUU 2
    feed("\x1B[2A");
    EXPECT_EQ(screen.cursorRow(), 3);
    EXPECT_EQ(screen.cursorCol(), 10);

    // CUD 1
    feed("\x1B[1B");
    EXPECT_EQ(screen.cursorRow(), 4);

    // CUF 3
    feed("\x1B[3C");
    EXPECT_EQ(screen.cursorCol(), 13);

    // CUB 5
    feed("\x1B[5D");
    EXPECT_EQ(screen.cursorCol(), 8);
}

// 6. Cursor position CUP
TEST_F(ScreenTest, CursorPosition) {
    feed("\x1B[10;20H");
    EXPECT_EQ(screen.cursorRow(), 9);
    EXPECT_EQ(screen.cursorCol(), 19);

    // Default params = (1,1)
    feed("\x1B[H");
    EXPECT_EQ(screen.cursorRow(), 0);
    EXPECT_EQ(screen.cursorCol(), 0);
}

// 7. Erase display
TEST_F(ScreenTest, EraseDisplayBelow) {
    feed("AAAA\r\nBBBB\r\nCCCC");
    // Cursor at (2, 4), move to (1, 2)
    feed("\x1B[2;3H");
    feed("\x1B[J"); // erase below (from cursor)
    // Row 0 intact
    EXPECT_EQ(screen.cellAt(0, 0).codepoint, U'A');
    // Row 1 col 0,1 intact, col 2+ erased
    EXPECT_EQ(screen.cellAt(1, 0).codepoint, U'B');
    EXPECT_EQ(screen.cellAt(1, 1).codepoint, U'B');
    EXPECT_EQ(screen.cellAt(1, 2).codepoint, U' ');
    // Row 2 erased
    EXPECT_EQ(screen.cellAt(2, 0).codepoint, U' ');
}

TEST_F(ScreenTest, EraseDisplayAbove) {
    feed("AAAA\r\nBBBB\r\nCCCC");
    feed("\x1B[2;3H"); // row 1, col 2
    feed("\x1B[1J");    // erase above
    // Row 0 erased
    EXPECT_EQ(screen.cellAt(0, 0).codepoint, U' ');
    // Row 1 col 0,1,2 erased, col 3 intact
    EXPECT_EQ(screen.cellAt(1, 2).codepoint, U' ');
    EXPECT_EQ(screen.cellAt(1, 3).codepoint, U'B');
    // Row 2 intact
    EXPECT_EQ(screen.cellAt(2, 0).codepoint, U'C');
}

TEST_F(ScreenTest, EraseDisplayAll) {
    feed("Hello\r\nWorld");
    feed("\x1B[2J");
    EXPECT_EQ(screen.cellAt(0, 0).codepoint, U' ');
    EXPECT_EQ(screen.cellAt(1, 0).codepoint, U' ');
}

// 8. Erase line
TEST_F(ScreenTest, EraseLineRight) {
    feed("ABCDEFGH");
    feed("\x1B[1;4H"); // col 3
    feed("\x1B[K");     // erase right
    EXPECT_EQ(screen.cellAt(0, 0).codepoint, U'A');
    EXPECT_EQ(screen.cellAt(0, 2).codepoint, U'C');
    EXPECT_EQ(screen.cellAt(0, 3).codepoint, U' ');
    EXPECT_EQ(screen.cellAt(0, 7).codepoint, U' ');
}

TEST_F(ScreenTest, EraseLineLeft) {
    feed("ABCDEFGH");
    feed("\x1B[1;4H"); // col 3
    feed("\x1B[1K");    // erase left
    EXPECT_EQ(screen.cellAt(0, 0).codepoint, U' ');
    EXPECT_EQ(screen.cellAt(0, 3).codepoint, U' ');
    EXPECT_EQ(screen.cellAt(0, 4).codepoint, U'E');
}

TEST_F(ScreenTest, EraseLineAll) {
    feed("ABCDEFGH");
    feed("\x1B[2K");
    for (int c = 0; c < 8; ++c) {
        EXPECT_EQ(screen.cellAt(0, c).codepoint, U' ');
    }
}

// 9. SGR: bold and colors
TEST_F(ScreenTest, SgrBold) {
    feed("\x1B[1mA");
    EXPECT_EQ(screen.cellAt(0, 0).codepoint, U'A');
    EXPECT_NE(screen.cellAt(0, 0).attributes & AttrBold, 0);
}

TEST_F(ScreenTest, SgrForegroundColor) {
    feed("\x1B[31mR"); // red
    EXPECT_EQ(screen.cellAt(0, 0).codepoint, U'R');
    EXPECT_EQ(screen.cellAt(0, 0).fg_color, 0xAA0000u);
}

TEST_F(ScreenTest, SgrReset) {
    feed("\x1B[1;31mA\x1B[0mB");
    EXPECT_NE(screen.cellAt(0, 0).attributes & AttrBold, 0);
    EXPECT_EQ(screen.cellAt(0, 1).attributes & AttrBold, 0);
    EXPECT_EQ(screen.cellAt(0, 1).fg_color, kColorDefault);
}

// 10. Scroll region
TEST_F(ScreenTest, ScrollRegion) {
    Screen s(5, 10);
    VtParser parser(s);

    // Set scroll region rows 2-4 (1-based)
    std::string cmd = "\x1B[2;4r";
    parser.feed(cmd.data(), cmd.size());

    // Move to row 3 (bottom of region, 0-based)
    cmd = "\x1B[4;1H";
    parser.feed(cmd.data(), cmd.size());

    // Write text in region
    cmd = "Line3";
    parser.feed(cmd.data(), cmd.size());

    // LF at bottom of region should scroll within region
    cmd = "\n";
    parser.feed(cmd.data(), cmd.size());

    // Row 3 (0-based) should now be empty after scroll
    EXPECT_EQ(s.getLineText(3), "");
}

// 11. Scrollback
TEST_F(ScreenTest, Scrollback) {
    Screen s(3, 10);
    VtParser parser(s);

    // Fill screen and scroll
    for (int i = 0; i < 5; ++i) {
        std::string line = "Line" + std::to_string(i) + "\r\n";
        parser.feed(line.data(), line.size());
    }

    EXPECT_GT(s.scrollbackSize(), 0u);
}

// 12. Insert/Delete lines
TEST_F(ScreenTest, InsertLines) {
    feed("AAA\r\nBBB\r\nCCC");
    feed("\x1B[2;1H"); // row 1
    feed("\x1B[1L");    // insert 1 line
    EXPECT_EQ(screen.getLineText(0), "AAA");
    EXPECT_EQ(screen.getLineText(1), "");
    EXPECT_EQ(screen.getLineText(2), "BBB");
}

TEST_F(ScreenTest, DeleteLines) {
    feed("AAA\r\nBBB\r\nCCC");
    feed("\x1B[2;1H"); // row 1
    feed("\x1B[1M");    // delete 1 line
    EXPECT_EQ(screen.getLineText(0), "AAA");
    EXPECT_EQ(screen.getLineText(1), "CCC");
}

// 13. Resize
TEST_F(ScreenTest, Resize) {
    feed("Hello");
    screen.resize(10, 40);
    EXPECT_EQ(screen.rows(), 10);
    EXPECT_EQ(screen.cols(), 40);
    EXPECT_EQ(screen.cellAt(0, 0).codepoint, U'H');
    // Cursor clamped
    EXPECT_LT(screen.cursorRow(), 10);
    EXPECT_LT(screen.cursorCol(), 40);
}

TEST_F(ScreenTest, ResizeClamped) {
    feed("\x1B[20;70H"); // row 19, col 69
    screen.resize(5, 10);
    EXPECT_EQ(screen.cursorRow(), 4);
    EXPECT_EQ(screen.cursorCol(), 9);
}

// 14. Save/Restore cursor
TEST_F(ScreenTest, SaveRestoreCursor) {
    feed("\x1B[5;10H"); // move to (4, 9)
    feed("\x1B" "7");    // DECSC
    feed("\x1B[1;1H");   // move to (0, 0)
    EXPECT_EQ(screen.cursorRow(), 0);
    EXPECT_EQ(screen.cursorCol(), 0);
    feed("\x1B" "8");    // DECRC
    EXPECT_EQ(screen.cursorRow(), 4);
    EXPECT_EQ(screen.cursorCol(), 9);
}

// 15. Tab stops
TEST_F(ScreenTest, TabStops) {
    feed("A\tB");
    EXPECT_EQ(screen.cellAt(0, 0).codepoint, U'A');
    // After tab from col 1, should land at col 8
    EXPECT_EQ(screen.cellAt(0, 8).codepoint, U'B');
    EXPECT_EQ(screen.cursorCol(), 9);
}

// 16. getLineText
TEST_F(ScreenTest, GetLineText) {
    feed("Hello World");
    EXPECT_EQ(screen.getLineText(0), "Hello World");
    EXPECT_EQ(screen.getLineText(1), "");
}

// Additional: Backspace
TEST_F(ScreenTest, Backspace) {
    feed("AB\x08X");
    EXPECT_EQ(screen.cellAt(0, 0).codepoint, U'A');
    EXPECT_EQ(screen.cellAt(0, 1).codepoint, U'X');
}

// Additional: Cursor visibility
TEST_F(ScreenTest, CursorVisibility) {
    EXPECT_TRUE(screen.cursorVisible());
    feed("\x1B[?25l"); // hide
    EXPECT_FALSE(screen.cursorVisible());
    feed("\x1B[?25h"); // show
    EXPECT_TRUE(screen.cursorVisible());
}

// Additional: VPA and CHA
TEST_F(ScreenTest, AbsolutePositioning) {
    feed("\x1B[10d"); // VPA: row 9 (0-based)
    EXPECT_EQ(screen.cursorRow(), 9);

    feed("\x1B[20G"); // CHA: col 19 (0-based)
    EXPECT_EQ(screen.cursorCol(), 19);
}

// Additional: Scroll up/down commands
TEST_F(ScreenTest, ScrollUpCommand) {
    Screen s(3, 5);
    VtParser parser(s);
    std::string data = "AAA\r\nBBB\r\nCCC";
    parser.feed(data.data(), data.size());
    data = "\x1B[1S"; // scroll up 1
    parser.feed(data.data(), data.size());
    EXPECT_EQ(s.getLineText(0), "BBB");
    EXPECT_EQ(s.getLineText(1), "CCC");
    EXPECT_EQ(s.getLineText(2), "");
}

TEST_F(ScreenTest, ScrollDownCommand) {
    Screen s(3, 5);
    VtParser parser(s);
    std::string data = "AAA\r\nBBB\r\nCCC";
    parser.feed(data.data(), data.size());
    data = "\x1B[1T"; // scroll down 1
    parser.feed(data.data(), data.size());
    EXPECT_EQ(s.getLineText(0), "");
    EXPECT_EQ(s.getLineText(1), "AAA");
    EXPECT_EQ(s.getLineText(2), "BBB");
}

// Additional: Erase characters
TEST_F(ScreenTest, EraseCharacters) {
    feed("ABCDE");
    feed("\x1B[1;2H"); // col 1
    feed("\x1B[2X");    // erase 2 chars
    EXPECT_EQ(screen.cellAt(0, 0).codepoint, U'A');
    EXPECT_EQ(screen.cellAt(0, 1).codepoint, U' ');
    EXPECT_EQ(screen.cellAt(0, 2).codepoint, U' ');
    EXPECT_EQ(screen.cellAt(0, 3).codepoint, U'D');
}

// Additional: Insert characters
TEST_F(ScreenTest, InsertCharacters) {
    feed("ABCDE");
    feed("\x1B[1;3H"); // col 2
    feed("\x1B[2@");    // insert 2 chars
    EXPECT_EQ(screen.cellAt(0, 0).codepoint, U'A');
    EXPECT_EQ(screen.cellAt(0, 1).codepoint, U'B');
    EXPECT_EQ(screen.cellAt(0, 2).codepoint, U' ');
    EXPECT_EQ(screen.cellAt(0, 3).codepoint, U' ');
    EXPECT_EQ(screen.cellAt(0, 4).codepoint, U'C');
}

// Additional: Delete characters
TEST_F(ScreenTest, DeleteCharacters) {
    feed("ABCDE");
    feed("\x1B[1;2H"); // col 1
    feed("\x1B[2P");    // delete 2 chars
    EXPECT_EQ(screen.cellAt(0, 0).codepoint, U'A');
    EXPECT_EQ(screen.cellAt(0, 1).codepoint, U'D');
    EXPECT_EQ(screen.cellAt(0, 2).codepoint, U'E');
}

// Additional: ESC D (IND) and ESC M (RI)
TEST_F(ScreenTest, IndexAndReverseIndex) {
    Screen s(3, 5);
    VtParser parser(s);
    // Fill screen
    std::string data = "AAA\r\nBBB\r\nCCC";
    parser.feed(data.data(), data.size());
    // Cursor at row 2 (last row), ESC D should scroll
    data = "\x1B" "D";
    parser.feed(data.data(), data.size());
    EXPECT_EQ(s.getLineText(0), "BBB");
    EXPECT_EQ(s.getLineText(1), "CCC");
    EXPECT_EQ(s.getLineText(2), "");
}

// Additional: Bright foreground colors
TEST_F(ScreenTest, BrightFgColors) {
    feed("\x1B[91mX"); // bright red
    EXPECT_EQ(screen.cellAt(0, 0).fg_color, 0xFF5555u);
}

// Additional: Erase scrollback
TEST_F(ScreenTest, EraseScrollback) {
    Screen s(3, 10);
    VtParser parser(s);
    for (int i = 0; i < 5; ++i) {
        std::string line = "Line\r\n";
        parser.feed(line.data(), line.size());
    }
    EXPECT_GT(s.scrollbackSize(), 0u);
    std::string cmd = "\x1B[3J";
    parser.feed(cmd.data(), cmd.size());
    EXPECT_EQ(s.scrollbackSize(), 0u);
}

// Additional: Parser integration - full pipeline
TEST_F(ScreenTest, ParserIntegration) {
    VtParser parser(screen);
    std::string data = "\x1B[2J\x1B[H"; // clear + home
    parser.feed(data.data(), data.size());
    data = "Hello, Terminal!";
    parser.feed(data.data(), data.size());
    EXPECT_EQ(screen.getLineText(0), "Hello, Terminal!");
}

} // namespace
} // namespace termcore
