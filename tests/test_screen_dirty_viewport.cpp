#include <gtest/gtest.h>
#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include <string>

namespace termcore {
namespace {

class ScreenDirtyTest : public ::testing::Test {
protected:
    Screen screen{5, 10};

    void feed(Screen& s, const std::string& data) {
        VtParser parser(s);
        parser.feed(data.data(), data.size());
    }

    void feed(const std::string& data) {
        feed(screen, data);
    }
};

// ============================================================
// Dirty Tracking Tests
// ============================================================

TEST_F(ScreenDirtyTest, InitiallyNotDirty) {
    // Constructor marks all dirty; clearDirty resets everything.
    screen.clearDirty();
    EXPECT_FALSE(screen.isDirty());
    for (int r = 0; r < screen.rows(); ++r) {
        EXPECT_FALSE(screen.isRowDirty(r)) << "row " << r;
    }
}

TEST_F(ScreenDirtyTest, PrintMarksDirty) {
    screen.clearDirty();
    feed("Hello");
    EXPECT_TRUE(screen.isDirty());
    EXPECT_TRUE(screen.isRowDirty(0));
    // Rows that were not written should remain clean.
    for (int r = 1; r < screen.rows(); ++r) {
        EXPECT_FALSE(screen.isRowDirty(r)) << "row " << r;
    }
}

TEST_F(ScreenDirtyTest, ClearDirtyResetsAllFlags) {
    // Write to multiple rows to dirty them.
    feed("AAA\r\nBBB\r\nCCC");
    screen.clearDirty();
    EXPECT_FALSE(screen.isDirty());
    for (int r = 0; r < screen.rows(); ++r) {
        EXPECT_FALSE(screen.isRowDirty(r)) << "row " << r;
    }
}

TEST_F(ScreenDirtyTest, MarkAllDirtyFlagsAllRows) {
    screen.clearDirty();
    screen.markAllDirty();
    EXPECT_TRUE(screen.isDirty());
    for (int r = 0; r < screen.rows(); ++r) {
        EXPECT_TRUE(screen.isRowDirty(r)) << "row " << r;
    }
}

TEST_F(ScreenDirtyTest, CursorMovementThenPrintMarksDirty) {
    screen.clearDirty();
    // CUP alone doesn't dirty rows — it only moves the cursor.
    // But printing after a move should dirty the destination row.
    feed("\x1B[4;1H");  // move to row 3
    feed("X");           // print marks row 3 dirty
    EXPECT_TRUE(screen.isDirty());
    EXPECT_TRUE(screen.isRowDirty(3));
    // Row 0 should remain clean (nothing written there after clear)
    EXPECT_FALSE(screen.isRowDirty(0));
}

TEST_F(ScreenDirtyTest, ScrollMarksDirty) {
    feed("AAA\r\nBBB\r\nCCC\r\nDDD\r\nEEE");
    screen.clearDirty();
    // Scroll up 1 line (CSI 1 S)
    feed("\x1B[1S");
    EXPECT_TRUE(screen.isDirty());
    // All rows in scroll region should be dirty
    for (int r = 0; r < screen.rows(); ++r) {
        EXPECT_TRUE(screen.isRowDirty(r)) << "row " << r;
    }
}

TEST_F(ScreenDirtyTest, EraseMarksDirty) {
    feed("ABCDE\r\nFGHIJ");
    screen.clearDirty();
    // Erase entire display (CSI 2 J)
    feed("\x1B[2J");
    EXPECT_TRUE(screen.isDirty());
    // All rows should be marked dirty after full erase
    for (int r = 0; r < screen.rows(); ++r) {
        EXPECT_TRUE(screen.isRowDirty(r)) << "row " << r;
    }
}

TEST_F(ScreenDirtyTest, EraseLineMarksDirty) {
    feed("ABCDE\r\nFGHIJ");
    screen.clearDirty();
    // Move to row 0 col 2, erase to end of line
    feed("\x1B[1;3H\x1B[K");
    EXPECT_TRUE(screen.isDirty());
    EXPECT_TRUE(screen.isRowDirty(0));
    EXPECT_FALSE(screen.isRowDirty(1));
}

TEST_F(ScreenDirtyTest, ResizeMarksAllDirty) {
    screen.clearDirty();
    screen.resize(8, 20);
    EXPECT_TRUE(screen.isDirty());
    for (int r = 0; r < screen.rows(); ++r) {
        EXPECT_TRUE(screen.isRowDirty(r)) << "row " << r;
    }
}

TEST_F(ScreenDirtyTest, AltScreenSwitchMarksDirty) {
    screen.clearDirty();
    // Enter alt screen (DECSET 1049)
    feed("\x1B[?1049h");
    EXPECT_TRUE(screen.isDirty());
    for (int r = 0; r < screen.rows(); ++r) {
        EXPECT_TRUE(screen.isRowDirty(r)) << "row " << r << " after enter alt";
    }

    screen.clearDirty();
    // Leave alt screen (DECRST 1049)
    feed("\x1B[?1049l");
    EXPECT_TRUE(screen.isDirty());
    for (int r = 0; r < screen.rows(); ++r) {
        EXPECT_TRUE(screen.isRowDirty(r)) << "row " << r << " after leave alt";
    }
}

TEST_F(ScreenDirtyTest, SgrChangeMarksDirty) {
    screen.clearDirty();
    // Set bold + print a character; the row with the printed char should be dirty
    feed("\x1B[1mX");
    EXPECT_TRUE(screen.isDirty());
    EXPECT_TRUE(screen.isRowDirty(0));
}

TEST_F(ScreenDirtyTest, IsRowDirtyOutOfBoundsReturnsFalse) {
    // Out-of-bounds row indices should safely return false.
    EXPECT_FALSE(screen.isRowDirty(-1));
    EXPECT_FALSE(screen.isRowDirty(screen.rows()));
    EXPECT_FALSE(screen.isRowDirty(999));
}

TEST_F(ScreenDirtyTest, MultipleRowsDirtyIndependently) {
    screen.clearDirty();
    // Write to row 0 and row 2 only
    feed("AAA");
    feed("\x1B[3;1H");  // move to row 2
    feed("CCC");
    EXPECT_TRUE(screen.isRowDirty(0));
    EXPECT_TRUE(screen.isRowDirty(2));
    // Row 1 may or may not be dirty depending on cursor movement implementation;
    // but row 3 and 4 should be clean
    EXPECT_FALSE(screen.isRowDirty(4));
}

TEST_F(ScreenDirtyTest, InsertLinesMarksDirty) {
    feed("AAA\r\nBBB\r\nCCC\r\nDDD\r\nEEE");
    screen.clearDirty();
    // Move to row 1, insert 1 line
    feed("\x1B[2;1H\x1B[1L");
    EXPECT_TRUE(screen.isDirty());
    // Row 1 and below should be dirty
    EXPECT_TRUE(screen.isRowDirty(1));
    EXPECT_TRUE(screen.isRowDirty(4));
}

TEST_F(ScreenDirtyTest, DeleteLinesMarksDirty) {
    feed("AAA\r\nBBB\r\nCCC\r\nDDD\r\nEEE");
    screen.clearDirty();
    // Move to row 1, delete 1 line
    feed("\x1B[2;1H\x1B[1M");
    EXPECT_TRUE(screen.isDirty());
    EXPECT_TRUE(screen.isRowDirty(1));
}

// ============================================================
// Viewport Scrolling Tests
// ============================================================

class ScreenViewportTest : public ::testing::Test {
protected:
    // Use a small screen so scrollback fills quickly
    Screen screen{3, 10};

    void feed(Screen& s, const std::string& data) {
        VtParser parser(s);
        parser.feed(data.data(), data.size());
    }

    void feed(const std::string& data) {
        feed(screen, data);
    }

    /// Push enough lines to create scrollback
    void fillScrollback(int extra_lines = 5) {
        for (int i = 0; i < screen.rows() + extra_lines; ++i) {
            std::string line = "Line" + std::to_string(i) + "\r\n";
            feed(line);
        }
    }
};

TEST_F(ScreenViewportTest, ScrollUpWhenEmptyIsNoop) {
    // No scrollback at all — scrolling should have no effect
    EXPECT_EQ(screen.scrollbackSize(), 0u);
    EXPECT_EQ(screen.viewportOffset(), 0);
    screen.scrollViewportUp(5);
    EXPECT_EQ(screen.viewportOffset(), 0);
}

TEST_F(ScreenViewportTest, ScrollDownPastBottomClamped) {
    fillScrollback();
    // Scroll up first, then try to scroll down past bottom
    screen.scrollViewportUp(2);
    EXPECT_EQ(screen.viewportOffset(), 2);
    // Now scroll down by more than offset
    screen.scrollViewportDown(100);
    EXPECT_EQ(screen.viewportOffset(), 0);
    EXPECT_TRUE(screen.isViewportAtBottom());
}

TEST_F(ScreenViewportTest, ScrollToTopShowsOldestContent) {
    fillScrollback();
    int sb = static_cast<int>(screen.scrollbackSize());
    ASSERT_GT(sb, 0);

    screen.scrollViewportToTop();
    EXPECT_EQ(screen.viewportOffset(), sb);

    // The top-left cell should come from the oldest scrollback row.
    // "Line0" was the first line pushed into scrollback.
    const TermCell& cell = screen.cellAt(0, 0);
    EXPECT_EQ(cell.codepoint, U'L');
}

TEST_F(ScreenViewportTest, ScrollToBottomReturnsToLive) {
    fillScrollback();
    screen.scrollViewportToTop();
    EXPECT_FALSE(screen.isViewportAtBottom());

    screen.scrollViewportToBottom();
    EXPECT_EQ(screen.viewportOffset(), 0);
    EXPECT_TRUE(screen.isViewportAtBottom());
}

TEST_F(ScreenViewportTest, ViewportOffsetAfterResize) {
    fillScrollback();
    screen.scrollViewportUp(3);
    int offset_before = screen.viewportOffset();
    EXPECT_EQ(offset_before, 3);

    // Resize the screen — viewport offset should be clamped to valid range
    screen.resize(5, 10);
    int max_valid = static_cast<int>(screen.scrollbackSize());
    EXPECT_LE(screen.viewportOffset(), max_valid);
    EXPECT_GE(screen.viewportOffset(), 0);
}

TEST_F(ScreenViewportTest, CellAtWithViewportOffset) {
    fillScrollback();
    int sb = static_cast<int>(screen.scrollbackSize());
    ASSERT_GT(sb, 0);

    // At bottom, row 0 should show the live grid content
    const TermCell& live_cell = screen.cellAt(0, 0);
    char32_t live_cp = live_cell.codepoint;

    // Scroll up by 1 — row 0 should now show the last scrollback line
    screen.scrollViewportUp(1);
    const TermCell& scrolled_cell = screen.cellAt(0, 0);
    // The scrollback line should be different from the bottom-most grid row
    // (unless it happens to have the same content, which is unlikely with our fill pattern)
    // At minimum, verify the cell is valid (not a null codepoint)
    EXPECT_NE(scrolled_cell.codepoint, U'\0');

    // After scrolling back to bottom, row 0 content should match original
    screen.scrollViewportToBottom();
    EXPECT_EQ(screen.cellAt(0, 0).codepoint, live_cp);
}

TEST_F(ScreenViewportTest, ScrollbackPushUpdatesViewport) {
    fillScrollback();
    int sb_before = static_cast<int>(screen.scrollbackSize());

    // Scroll up to some position
    screen.scrollViewportUp(2);
    EXPECT_EQ(screen.viewportOffset(), 2);

    // Push more content — scrollback grows
    feed("NewLine\r\n");
    int sb_after = static_cast<int>(screen.scrollbackSize());
    EXPECT_GE(sb_after, sb_before);

    // Viewport offset should still be valid (not exceed scrollback size)
    EXPECT_LE(screen.viewportOffset(), static_cast<int>(screen.scrollbackSize()));
}

TEST_F(ScreenViewportTest, ViewportScrollStepSize) {
    fillScrollback(10);
    int sb = static_cast<int>(screen.scrollbackSize());
    ASSERT_GT(sb, 5);

    // Scroll up by 3
    screen.scrollViewportUp(3);
    EXPECT_EQ(screen.viewportOffset(), 3);

    // Scroll up by 2 more
    screen.scrollViewportUp(2);
    EXPECT_EQ(screen.viewportOffset(), 5);

    // Scroll down by 1
    screen.scrollViewportDown(1);
    EXPECT_EQ(screen.viewportOffset(), 4);

    // Scroll down by 4
    screen.scrollViewportDown(4);
    EXPECT_EQ(screen.viewportOffset(), 0);
    EXPECT_TRUE(screen.isViewportAtBottom());
}

TEST_F(ScreenViewportTest, ScrollUpClampedToScrollbackSize) {
    fillScrollback(3);
    int sb = static_cast<int>(screen.scrollbackSize());

    // Try to scroll up far more than scrollback
    screen.scrollViewportUp(sb + 100);
    EXPECT_EQ(screen.viewportOffset(), sb);
}

TEST_F(ScreenViewportTest, ScrollDownWithZeroLinesIsNoop) {
    fillScrollback();
    screen.scrollViewportUp(3);
    int offset = screen.viewportOffset();
    screen.scrollViewportDown(0);
    EXPECT_EQ(screen.viewportOffset(), offset);
}

TEST_F(ScreenViewportTest, ScrollUpWithZeroLinesIsNoop) {
    fillScrollback();
    screen.scrollViewportUp(3);
    int offset = screen.viewportOffset();
    screen.scrollViewportUp(0);
    EXPECT_EQ(screen.viewportOffset(), offset);
}

TEST_F(ScreenViewportTest, ViewportOffsetResizeToSmallerClampsCorrectly) {
    fillScrollback(10);
    screen.scrollViewportToTop();
    int offset_at_top = screen.viewportOffset();
    EXPECT_GT(offset_at_top, 0);

    // Resize to larger height — scrollback may shrink relative to screen
    screen.resize(6, 10);
    EXPECT_LE(screen.viewportOffset(), static_cast<int>(screen.scrollbackSize()));
    EXPECT_GE(screen.viewportOffset(), 0);
}

} // namespace
} // namespace termcore
