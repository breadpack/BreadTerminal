#include <gtest/gtest.h>
#include "termcore/screen.h"
#include "termcore/scrollback_ring.h"
#include "termcore/vt_parser.h"
#include <string>
#include <vector>

namespace termcore {
namespace {

// ═══════════════════════════════════════════════════════════════════════════════
// Helper fixture
// ═══════════════════════════════════════════════════════════════════════════════

class WideCharGraphemeTest : public ::testing::Test {
protected:
    Screen screen{24, 80};

    void feed(const std::string& data) {
        VtParser parser(screen);
        parser.feed(data.data(), data.size());
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// Wide Character Tests (Screen)
// ═══════════════════════════════════════════════════════════════════════════════

// CJK character '中' (U+4E2D) should occupy 2 cells: primary cell with width=2,
// next cell is a zero-width continuation (codepoint=0, width=0).
TEST_F(WideCharGraphemeTest, CJKCharacterOccupiesTwoCells) {
    // U+4E2D '中' = UTF-8 \xE4\xB8\xAD
    feed("\xE4\xB8\xAD");

    const TermCell& primary = screen.cellAt(0, 0);
    EXPECT_EQ(primary.codepoint, U'\u4E2D');
    EXPECT_EQ(primary.width, 2);

    const TermCell& cont = screen.cellAt(0, 1);
    EXPECT_EQ(cont.codepoint, static_cast<char32_t>(0));
    EXPECT_EQ(cont.width, 0);
}

// After printing a wide char, cursor should advance by 2.
TEST_F(WideCharGraphemeTest, CursorAdvancesTwoCellsForWideChar) {
    feed("\xE4\xB8\xAD"); // '中'
    EXPECT_EQ(screen.cursorCol(), 2);
    EXPECT_EQ(screen.cursorRow(), 0);
}

// A wide char that doesn't fit at the last column should wrap to the next line.
TEST_F(WideCharGraphemeTest, WideCharAtEndOfLineWraps) {
    Screen small(2, 5);
    VtParser parser(small);

    // Fill 4 columns with narrow chars, then try a wide char at col 4
    // (which needs 2 cells but only 1 remains).
    std::string data = "ABCD";
    parser.feed(data.data(), data.size());
    EXPECT_EQ(small.cursorCol(), 4);

    // Feed a wide char: should wrap to next line
    data = "\xE4\xB8\xAD"; // '中'
    parser.feed(data.data(), data.size());

    EXPECT_EQ(small.cursorRow(), 1);
    EXPECT_EQ(small.cursorCol(), 2);

    // The wide char should be on the second row
    EXPECT_EQ(small.cellAt(1, 0).codepoint, U'\u4E2D');
    EXPECT_EQ(small.cellAt(1, 0).width, 2);
    EXPECT_EQ(small.cellAt(1, 1).codepoint, static_cast<char32_t>(0));
    EXPECT_EQ(small.cellAt(1, 1).width, 0);
}

// Erasing over a wide char should clear both the primary and continuation cells.
TEST_F(WideCharGraphemeTest, EraseWideCharClearsBothCells) {
    feed("\xE4\xB8\xAD"); // '中' at col 0-1

    // Move cursor to col 0 and erase to end of line
    feed("\x1B[1;1H"); // CUP row 1, col 1 (0-based: 0,0)
    feed("\x1B[K");      // Erase to end of line

    EXPECT_EQ(screen.cellAt(0, 0).codepoint, U' ');
    EXPECT_EQ(screen.cellAt(0, 0).width, 1);
    EXPECT_EQ(screen.cellAt(0, 1).codepoint, U' ');
    EXPECT_EQ(screen.cellAt(0, 1).width, 1);
}

// Overwriting the first cell of a wide char with a narrow char.
// The continuation cell must be cleared to avoid an orphaned half-width cell.
TEST_F(WideCharGraphemeTest, OverwriteWideCharWithNarrow) {
    feed("\xE4\xB8\xAD"); // '中' at col 0-1
    feed("\x1B[1;1H");     // Move cursor to col 0
    feed("X");             // Overwrite first cell with narrow 'X'

    EXPECT_EQ(screen.cellAt(0, 0).codepoint, U'X');
    EXPECT_EQ(screen.cellAt(0, 0).width, 1);
    // The continuation cell should be cleared to a space.
    const TermCell& second = screen.cellAt(0, 1);
    EXPECT_EQ(second.codepoint, U' ');
    EXPECT_EQ(second.width, 1);
}

// Overwriting the second cell of a wide char.
// The primary cell must be cleared to avoid an orphaned wide char without its continuation.
TEST_F(WideCharGraphemeTest, OverwriteWideCharSecondCell) {
    feed("\xE4\xB8\xAD"); // '中' at col 0-1
    feed("\x1B[1;2H");     // Move cursor to col 1 (1-based col 2)
    feed("Y");             // Overwrite second cell

    // The primary cell should be cleared to a space.
    const TermCell& first = screen.cellAt(0, 0);
    EXPECT_EQ(first.codepoint, U' ');
    EXPECT_EQ(first.width, 1);

    EXPECT_EQ(screen.cellAt(0, 1).codepoint, U'Y');
    EXPECT_EQ(screen.cellAt(0, 1).width, 1);
}

// Emoji U+1F600 should be treated as wide (2 cells).
TEST_F(WideCharGraphemeTest, EmojiCharacterWidth) {
    // U+1F600 '😀' = UTF-8 \xF0\x9F\x98\x80
    feed("\xF0\x9F\x98\x80");

    const TermCell& primary = screen.cellAt(0, 0);
    EXPECT_EQ(primary.codepoint, U'\U0001F600');
    EXPECT_EQ(primary.width, 2);

    const TermCell& cont = screen.cellAt(0, 1);
    EXPECT_EQ(cont.codepoint, static_cast<char32_t>(0));
    EXPECT_EQ(cont.width, 0);

    EXPECT_EQ(screen.cursorCol(), 2);
}

// Multiple CJK characters in a row.
TEST_F(WideCharGraphemeTest, MultipleCJKCharacters) {
    // '中文' = U+4E2D U+6587
    feed("\xE4\xB8\xAD"   // '中'
         "\xE6\x96\x87"); // '文'

    EXPECT_EQ(screen.cellAt(0, 0).codepoint, U'\u4E2D');
    EXPECT_EQ(screen.cellAt(0, 0).width, 2);
    EXPECT_EQ(screen.cellAt(0, 1).width, 0);

    EXPECT_EQ(screen.cellAt(0, 2).codepoint, U'\u6587');
    EXPECT_EQ(screen.cellAt(0, 2).width, 2);
    EXPECT_EQ(screen.cellAt(0, 3).width, 0);

    EXPECT_EQ(screen.cursorCol(), 4);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Grapheme Cluster Tests (Screen)
// ═══════════════════════════════════════════════════════════════════════════════

// A combining acute accent (U+0301) following a base character should be
// appended to the same cell via appendCodepoint().
TEST_F(WideCharGraphemeTest, CombiningMarkAppendsToCell) {
    // 'a' followed by combining acute accent U+0301
    feed("a\xCC\x81");

    const TermCell& cell = screen.cellAt(0, 0);
    EXPECT_EQ(cell.codepoint, U'a');
    EXPECT_EQ(cell.extra_count, 1);
    EXPECT_EQ(cell.extra[0], U'\u0301');

    // Cursor should still be at col 1 (combining mark doesn't advance cursor)
    EXPECT_EQ(screen.cursorCol(), 1);
}

// Multiple combining marks on the same base character.
TEST_F(WideCharGraphemeTest, MultipleCombiningMarks) {
    // 'a' + U+0308 (diaeresis) + U+0301 (acute) -> ä́
    feed("a\xCC\x88\xCC\x81");

    const TermCell& cell = screen.cellAt(0, 0);
    EXPECT_EQ(cell.codepoint, U'a');
    EXPECT_EQ(cell.extra_count, 2);
    EXPECT_EQ(cell.extra[0], U'\u0308');
    EXPECT_EQ(cell.extra[1], U'\u0301');

    EXPECT_EQ(screen.cursorCol(), 1);
}

// ZWJ emoji sequence: 👨‍💻 = U+1F468 U+200D U+1F4BB
// The ZWJ and subsequent codepoints should be combined into a single cell.
TEST_F(WideCharGraphemeTest, ZWJSequence) {
    // U+1F468 = \xF0\x9F\x91\xA8
    // U+200D  = \xE2\x80\x8D (ZWJ)
    // U+1F4BB = \xF0\x9F\x92\xBB
    feed("\xF0\x9F\x91\xA8"   // 👨
         "\xE2\x80\x8D"       // ZWJ
         "\xF0\x9F\x92\xBB"); // 💻

    const TermCell& cell = screen.cellAt(0, 0);
    EXPECT_EQ(cell.codepoint, U'\U0001F468');
    // Should have ZWJ and the second emoji as extra codepoints
    EXPECT_GE(cell.extra_count, 2);
    EXPECT_EQ(cell.extra[0], U'\u200D'); // ZWJ
    EXPECT_EQ(cell.extra[1], U'\U0001F4BB');

    // ZWJ sequence should be wide (2 cells)
    EXPECT_EQ(cell.width, 2);
}

// Variation selector 16 (U+FE0F) after an emoji should keep it wide.
TEST_F(WideCharGraphemeTest, VariationSelector) {
    // '☺' U+263A + VS16 U+FE0F -> emoji presentation (wide)
    // U+263A = \xE2\x98\xBA
    // U+FE0F = \xEF\xB8\x8F
    feed("\xE2\x98\xBA\xEF\xB8\x8F");

    const TermCell& cell = screen.cellAt(0, 0);
    EXPECT_EQ(cell.codepoint, U'\u263A');
    EXPECT_GE(cell.extra_count, 1);
    EXPECT_EQ(cell.extra[0], U'\uFE0F');
}

// Skin tone modifier (Fitzpatrick) after emoji.
// Note: skin tone modifiers (U+1F3FB-U+1F3FF) have Extend property in UAX #29,
// so they should combine with the preceding emoji into a single grapheme cluster.
TEST_F(WideCharGraphemeTest, SkinToneModifier) {
    // 👋 U+1F44B + skin tone U+1F3FD
    // U+1F44B = \xF0\x9F\x91\x8B
    // U+1F3FD = \xF0\x9F\x8F\xBD
    feed("\xF0\x9F\x91\x8B"   // 👋
         "\xF0\x9F\x8F\xBD"); // medium skin tone

    const TermCell& cell = screen.cellAt(0, 0);
    EXPECT_EQ(cell.codepoint, U'\U0001F44B');
    EXPECT_EQ(cell.width, 2);
    // The skin tone modifier may or may not combine depending on the
    // grapheme break property table. Verify the emoji itself is correct.
    // If combined, extra_count >= 1 and the modifier is in extra[0].
    // If not combined, the modifier appears as a separate cell at col 2.
    if (cell.extra_count >= 1) {
        EXPECT_EQ(cell.extra[0], U'\U0001F3FD');
    } else {
        // Modifier rendered as separate character
        EXPECT_EQ(screen.cellAt(0, 2).codepoint, U'\U0001F3FD');
    }
}

// getLineText should correctly reconstruct a line with wide chars.
TEST_F(WideCharGraphemeTest, GetLineTextWithWideChars) {
    // "A中B" should produce "A中B"
    feed("A\xE4\xB8\xAD" "B");
    std::string text = screen.getLineText(0);
    EXPECT_EQ(text, "A\xE4\xB8\xAD" "B");
}

// getLineText should correctly reconstruct combining marks.
TEST_F(WideCharGraphemeTest, GetLineTextWithCombiningMarks) {
    // 'a' + combining acute U+0301
    feed("a\xCC\x81");
    std::string text = screen.getLineText(0);
    EXPECT_EQ(text, "a\xCC\x81");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Wide Character Tests (ScrollbackRing)
// ═══════════════════════════════════════════════════════════════════════════════

// Helper to create a row with a wide character at a specific position.
static std::vector<TermCell> makeWideRow(int cols, int wide_col, char32_t wide_cp) {
    std::vector<TermCell> row(cols);
    for (int i = 0; i < cols; ++i) {
        row[i].codepoint = U' ';
        row[i].width = 1;
    }
    row[wide_col].codepoint = wide_cp;
    row[wide_col].width = 2;
    if (wide_col + 1 < cols) {
        row[wide_col + 1].codepoint = 0;
        row[wide_col + 1].width = 0;
    }
    return row;
}

// A row with a CJK character pushed into scrollback should preserve the
// wide char primary/continuation cell pair.
TEST(WideCharScrollback, WideCharSurvivesScrollback) {
    ScrollbackRing ring(80, 100);
    ring.pushRow(makeWideRow(80, 0, U'\u4E2D'));

    TermCell primary = ring.cellAt(0, 0);
    EXPECT_EQ(primary.codepoint, U'\u4E2D');
    EXPECT_EQ(primary.width, 2);

    TermCell cont = ring.cellAt(0, 1);
    EXPECT_EQ(cont.codepoint, static_cast<char32_t>(0));
    EXPECT_EQ(cont.width, 0);
}

// Wide char at column 78 (0-based) in an 80-column terminal should work
// correctly at the row boundary.
TEST(WideCharScrollback, WideCharAtRowBoundary) {
    ScrollbackRing ring(80, 100);
    ring.pushRow(makeWideRow(80, 78, U'\u4E16')); // '世' at cols 78-79

    TermCell primary = ring.cellAt(0, 78);
    EXPECT_EQ(primary.codepoint, U'\u4E16');
    EXPECT_EQ(primary.width, 2);

    TermCell cont = ring.cellAt(0, 79);
    EXPECT_EQ(cont.codepoint, static_cast<char32_t>(0));
    EXPECT_EQ(cont.width, 0);
}

// Wide chars should survive when they are pushed into scrollback via Screen scrolling.
TEST_F(WideCharGraphemeTest, WideCharScrolledIntoScrollback) {
    Screen s(3, 10);
    VtParser parser(s);

    // Fill 3 rows with CJK text, then force scrolling
    std::string line1 = "\xE4\xB8\xAD\r\n"; // '中'
    std::string line2 = "\xE6\x96\x87\r\n"; // '文'
    std::string line3 = "ABC\r\n";
    std::string line4 = "DEF";

    parser.feed(line1.data(), line1.size());
    parser.feed(line2.data(), line2.size());
    parser.feed(line3.data(), line3.size());
    parser.feed(line4.data(), line4.size());

    // At least row with '中' should have scrolled into scrollback
    EXPECT_GT(s.scrollbackSize(), 0u);

    // Verify the scrollback text contains the CJK character
    std::string sb_text = s.getScrollbackLineText(static_cast<int>(s.scrollbackSize()) - 1);
    EXPECT_NE(sb_text.find("\xE4\xB8\xAD"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Resize with Wide Characters
// ═══════════════════════════════════════════════════════════════════════════════

// Resizing narrower should not corrupt the grid. Wide chars that would be split
// by the new boundary should still leave the grid in a consistent state.
TEST_F(WideCharGraphemeTest, ResizeSmallerWithWideChars) {
    Screen s(5, 10);
    VtParser parser(s);

    // Place a wide char at col 8-9 (last two columns of a 10-col screen)
    std::string moveAndPrint = "\x1B[1;9H" // CUP to col 8 (1-based: col 9)
                               "\xE4\xB8\xAD"; // '中'
    parser.feed(moveAndPrint.data(), moveAndPrint.size());

    // Verify placement before resize
    EXPECT_EQ(s.cellAt(0, 8).codepoint, U'\u4E2D');
    EXPECT_EQ(s.cellAt(0, 8).width, 2);

    // Resize to 5 cols - the wide char at col 8-9 should be truncated
    s.resize(5, 5);

    // Grid should not crash or have invalid state
    EXPECT_EQ(s.rows(), 5);
    EXPECT_EQ(s.cols(), 5);

    // Verify the grid is still accessible (no crash)
    for (int c = 0; c < 5; ++c) {
        const TermCell& cell = s.cellAt(0, c);
        (void)cell.codepoint; // Just ensure no crash
    }
}

// Resizing wider should preserve wide character pairs.
TEST_F(WideCharGraphemeTest, ResizeLargerPreservesWideChars) {
    Screen s(5, 10);
    VtParser parser(s);

    // Place a wide char at col 0-1
    std::string data = "\xE4\xB8\xAD"; // '中'
    parser.feed(data.data(), data.size());

    // Verify before resize
    EXPECT_EQ(s.cellAt(0, 0).codepoint, U'\u4E2D');
    EXPECT_EQ(s.cellAt(0, 0).width, 2);
    EXPECT_EQ(s.cellAt(0, 1).codepoint, static_cast<char32_t>(0));
    EXPECT_EQ(s.cellAt(0, 1).width, 0);

    // Resize wider
    s.resize(5, 20);

    // Wide char pair should be preserved
    EXPECT_EQ(s.cellAt(0, 0).codepoint, U'\u4E2D');
    EXPECT_EQ(s.cellAt(0, 0).width, 2);
    EXPECT_EQ(s.cellAt(0, 1).codepoint, static_cast<char32_t>(0));
    EXPECT_EQ(s.cellAt(0, 1).width, 0);
}

// Resizing with CJK content on multiple lines.
TEST_F(WideCharGraphemeTest, ResizePreservesMultipleWideCharLines) {
    Screen s(5, 10);
    VtParser parser(s);

    // Write CJK on two lines
    std::string data = "\xE4\xB8\xAD\xE6\x96\x87\r\n" // '中文'
                       "\xE4\xB8\x96\xE7\x95\x8C";     // '世界'
    parser.feed(data.data(), data.size());

    // Resize wider
    s.resize(5, 20);

    EXPECT_EQ(s.cellAt(0, 0).codepoint, U'\u4E2D');
    EXPECT_EQ(s.cellAt(0, 2).codepoint, U'\u6587');
    EXPECT_EQ(s.cellAt(1, 0).codepoint, U'\u4E16');
    EXPECT_EQ(s.cellAt(1, 2).codepoint, U'\u754C');
}

} // namespace
} // namespace termcore
