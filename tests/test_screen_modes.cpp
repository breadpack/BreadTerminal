#include <gtest/gtest.h>
#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include <string>

namespace termcore {
namespace {

class ScreenModesTest : public ::testing::Test {
protected:
    Screen screen{24, 80};

    void feed(const std::string& data) {
        VtParser parser(screen);
        parser.feed(data.data(), data.size());
    }
};

// ===== Alt Screen Buffer Tests =====

// Alt screen enter/exit: text on primary survives roundtrip
TEST_F(ScreenModesTest, AltScreenPrimarySurvivesRoundtrip) {
    feed("Hello");
    EXPECT_EQ(screen.getLineText(0), "Hello");
    EXPECT_FALSE(screen.altScreenActive());

    feed("\x1B[?1049h"); // enter alt screen
    EXPECT_TRUE(screen.altScreenActive());
    EXPECT_EQ(screen.getLineText(0), "");

    feed("\x1B[?1049l"); // leave alt screen
    EXPECT_FALSE(screen.altScreenActive());
    EXPECT_EQ(screen.getLineText(0), "Hello");
}

// Writing on alt doesn't affect primary
TEST_F(ScreenModesTest, AltScreenWriteDoesNotAffectPrimary) {
    feed("Primary");
    feed("\x1B[?1049h");
    feed("AltText");
    EXPECT_EQ(screen.getLineText(0), "AltText");

    feed("\x1B[?1049l");
    EXPECT_EQ(screen.getLineText(0), "Primary");
}

// Cursor position saved/restored with ?1049
TEST_F(ScreenModesTest, AltScreen1049SavesRestoresCursor) {
    feed("\x1B[5;10H"); // row 4, col 9
    feed("\x1B[?1049h");
    EXPECT_EQ(screen.cursorRow(), 0);
    EXPECT_EQ(screen.cursorCol(), 0);

    feed("\x1B[10;20H"); // move on alt
    feed("\x1B[?1049l");
    EXPECT_EQ(screen.cursorRow(), 4);
    EXPECT_EQ(screen.cursorCol(), 9);
}

// ?47 doesn't save/restore cursor
TEST_F(ScreenModesTest, AltScreen47NoSaveRestoreCursor) {
    feed("\x1B[5;10H"); // row 4, col 9
    feed("\x1B[?47h");
    EXPECT_EQ(screen.cursorRow(), 0);
    EXPECT_EQ(screen.cursorCol(), 0);

    feed("\x1B[10;20H"); // move on alt
    feed("\x1B[?47l");
    EXPECT_EQ(screen.cursorRow(), 4);
    EXPECT_EQ(screen.cursorCol(), 9);
}

// Scrollback disabled on alt screen
TEST_F(ScreenModesTest, AltScreenNoScrollback) {
    Screen s(3, 10);
    VtParser parser(s);
    std::string cmd;

    cmd = "\x1B[?1049h";
    parser.feed(cmd.data(), cmd.size());

    for (int i = 0; i < 5; ++i) {
        cmd = "Line" + std::to_string(i) + "\r\n";
        parser.feed(cmd.data(), cmd.size());
    }
    EXPECT_EQ(s.scrollbackSize(), 0u);

    cmd = "\x1B[?1049l";
    parser.feed(cmd.data(), cmd.size());
}

// DECCKM ?1: app_cursor_keys toggled
TEST_F(ScreenModesTest, DecCKM) {
    EXPECT_FALSE(screen.appCursorKeys());
    feed("\x1B[?1h");
    EXPECT_TRUE(screen.appCursorKeys());
    feed("\x1B[?1l");
    EXPECT_FALSE(screen.appCursorKeys());
}

// DECAWM ?7: autowrap toggled
TEST_F(ScreenModesTest, DecAWM) {
    Screen s(2, 5);
    VtParser parser(s);

    std::string cmd = "\x1B[?7l";
    parser.feed(cmd.data(), cmd.size());

    cmd = "ABCDEFGH";
    parser.feed(cmd.data(), cmd.size());
    EXPECT_EQ(s.cellAt(0, 4).codepoint, U'H');
    EXPECT_EQ(s.getLineText(1), "");

    cmd = "\x1B[?7h";
    parser.feed(cmd.data(), cmd.size());
}

// Bracketed paste ?2004
TEST_F(ScreenModesTest, BracketedPaste) {
    EXPECT_FALSE(screen.bracketedPaste());
    feed("\x1B[?2004h");
    EXPECT_TRUE(screen.bracketedPaste());
    feed("\x1B[?2004l");
    EXPECT_FALSE(screen.bracketedPaste());
}

// Mouse mode ?1000
TEST_F(ScreenModesTest, MouseModeX10) {
    EXPECT_EQ(screen.mouseMode(), MouseMode::None);
    feed("\x1B[?1000h");
    EXPECT_EQ(screen.mouseMode(), MouseMode::X10);
    feed("\x1B[?1000l");
    EXPECT_EQ(screen.mouseMode(), MouseMode::None);
}

// Mouse SGR ?1006
TEST_F(ScreenModesTest, MouseEncodingSGR) {
    EXPECT_EQ(screen.mouseEncoding(), MouseEncoding::Default);
    feed("\x1B[?1006h");
    EXPECT_EQ(screen.mouseEncoding(), MouseEncoding::SGR);
    feed("\x1B[?1006l");
    EXPECT_EQ(screen.mouseEncoding(), MouseEncoding::Default);
}

// Multiple mode set/reset in one sequence
TEST_F(ScreenModesTest, MultipleModesInOneSequence) {
    feed("\x1B[?1;2004;25h");
    EXPECT_TRUE(screen.appCursorKeys());
    EXPECT_TRUE(screen.bracketedPaste());
    EXPECT_TRUE(screen.cursorVisible());

    feed("\x1B[?1;2004;25l");
    EXPECT_FALSE(screen.appCursorKeys());
    EXPECT_FALSE(screen.bracketedPaste());
    EXPECT_FALSE(screen.cursorVisible());
}

// Alt screen + resize
TEST_F(ScreenModesTest, AltScreenResize) {
    feed("Hello");
    feed("\x1B[?1049h");
    feed("Alt");

    screen.resize(10, 40);
    EXPECT_EQ(screen.rows(), 10);
    EXPECT_EQ(screen.cols(), 40);
    EXPECT_TRUE(screen.altScreenActive());

    feed("\x1B[?1049l");
    EXPECT_FALSE(screen.altScreenActive());
}

// Alt screen: erase display works on alt grid
TEST_F(ScreenModesTest, AltScreenEraseDisplay) {
    feed("Primary");
    feed("\x1B[?1049h");
    feed("AltContent");
    feed("\x1B[2J"); // erase all on alt
    EXPECT_EQ(screen.getLineText(0), "");

    feed("\x1B[?1049l");
    EXPECT_EQ(screen.getLineText(0), "Primary");
}

// Mouse button event tracking ?1002
TEST_F(ScreenModesTest, MouseModeButtonEvent) {
    feed("\x1B[?1002h");
    EXPECT_EQ(screen.mouseMode(), MouseMode::ButtonEvent);
    feed("\x1B[?1002l");
    EXPECT_EQ(screen.mouseMode(), MouseMode::None);
}

// Mouse any event tracking ?1003
TEST_F(ScreenModesTest, MouseModeAnyEvent) {
    feed("\x1B[?1003h");
    EXPECT_EQ(screen.mouseMode(), MouseMode::AnyEvent);
    feed("\x1B[?1003l");
    EXPECT_EQ(screen.mouseMode(), MouseMode::None);
}

} // namespace
} // namespace termcore
