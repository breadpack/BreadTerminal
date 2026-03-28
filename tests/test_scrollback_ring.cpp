#include <gtest/gtest.h>
#include "termcore/scrollback_ring.h"
#include <vector>
#include <string>

namespace termcore {
namespace {

// Helper: create a row of TermCells with ASCII characters from a string.
std::vector<TermCell> makeRow(int cols, const std::string& text = "") {
    std::vector<TermCell> row(cols);
    for (int i = 0; i < cols; ++i) {
        row[i].codepoint = (i < static_cast<int>(text.size())) ? static_cast<char32_t>(text[i]) : U' ';
        row[i].width = 1;
    }
    return row;
}

// Helper: create a row where each cell has a specific codepoint based on a tag value.
// Useful for verifying data integrity after wrap-around.
std::vector<TermCell> makeTaggedRow(int cols, int tag) {
    std::vector<TermCell> row(cols);
    for (int i = 0; i < cols; ++i) {
        // Encode tag into the codepoint (use printable range offset)
        row[i].codepoint = static_cast<char32_t>('A' + (tag % 26));
        row[i].width = 1;
    }
    return row;
}

// ─── Basic Operations ───────────────────────────────────────────────────────

TEST(ScrollbackRing, EmptyRingHasZeroRows) {
    ScrollbackRing ring(80, 100);
    EXPECT_EQ(ring.size(), 0u);
    EXPECT_EQ(ring.maxRows(), 100u);
    EXPECT_EQ(ring.cols(), 80);
    EXPECT_EQ(ring.evictedCount(), 0);
}

TEST(ScrollbackRing, PushRowIncreasesCount) {
    ScrollbackRing ring(80, 100);
    ring.pushRow(makeRow(80, "Hello"));
    EXPECT_EQ(ring.size(), 1u);
    ring.pushRow(makeRow(80, "World"));
    EXPECT_EQ(ring.size(), 2u);
}

TEST(ScrollbackRing, CellAtReturnsCorrectData) {
    ScrollbackRing ring(10, 100);
    ring.pushRow(makeRow(10, "ABCDEFGHIJ"));

    // Row 0 (oldest = only row), check each cell
    for (int c = 0; c < 10; ++c) {
        TermCell tc = ring.cellAt(0, c);
        EXPECT_EQ(tc.codepoint, static_cast<char32_t>('A' + c))
            << "Mismatch at col " << c;
    }
}

TEST(ScrollbackRing, MaxRowsRespected) {
    const size_t maxRows = 50;
    ScrollbackRing ring(10, maxRows);

    for (int i = 0; i < 100; ++i) {
        ring.pushRow(makeRow(10, "Row"));
    }

    EXPECT_LE(ring.size(), maxRows);
}

// ─── Ring Buffer Behavior ───────────────────────────────────────────────────

TEST(ScrollbackRing, EvictionWhenFull) {
    ScrollbackRing ring(10, 5);

    // Push 5 rows to fill
    for (int i = 0; i < 5; ++i) {
        std::string s(1, static_cast<char>('A' + i));
        ring.pushRow(makeRow(10, s));
    }
    EXPECT_EQ(ring.size(), 5u);

    // Push one more; oldest should be evicted
    ring.pushRow(makeRow(10, "F"));
    EXPECT_EQ(ring.size(), 5u);

    // The oldest row should now be 'B' (row index 0 after eviction)
    TermCell tc = ring.cellAt(0, 0);
    EXPECT_EQ(tc.codepoint, static_cast<char32_t>('B'));

    // Newest row should be 'F'
    tc = ring.cellAt(4, 0);
    EXPECT_EQ(tc.codepoint, static_cast<char32_t>('F'));
}

TEST(ScrollbackRing, EvictedCountTracking) {
    ScrollbackRing ring(10, 5);

    // Push 5 rows - no eviction yet
    for (int i = 0; i < 5; ++i) {
        ring.pushRow(makeRow(10));
    }
    EXPECT_EQ(ring.evictedCount(), 0);

    // Push 10 more, each causes one eviction
    int64_t prev_evicted = ring.evictedCount();
    for (int i = 0; i < 10; ++i) {
        ring.pushRow(makeRow(10));
        EXPECT_GE(ring.evictedCount(), prev_evicted)
            << "evictedCount must be monotonically non-decreasing";
        prev_evicted = ring.evictedCount();
    }

    EXPECT_EQ(ring.evictedCount(), 10);
}

TEST(ScrollbackRing, WrapAroundCorrectness) {
    // Use a small maxRows to force multiple wrap-arounds
    const size_t maxRows = 20;
    ScrollbackRing ring(5, maxRows);

    // Push 100 rows with identifiable content
    for (int i = 0; i < 100; ++i) {
        ring.pushRow(makeTaggedRow(5, i));
    }

    EXPECT_EQ(ring.size(), maxRows);

    // The oldest visible row should correspond to tag (100 - 20) = 80
    // Newest should be tag 99
    for (size_t r = 0; r < ring.size(); ++r) {
        int expected_tag = 80 + static_cast<int>(r);
        char32_t expected_cp = static_cast<char32_t>('A' + (expected_tag % 26));
        TermCell tc = ring.cellAt(static_cast<int>(r), 0);
        EXPECT_EQ(tc.codepoint, expected_cp)
            << "Row " << r << " expected tag " << expected_tag;
    }
}

TEST(ScrollbackRing, MultipleSegmentAllocation) {
    // kRowsPerSegment = 2048, so pushing more than that forces a second segment
    const int rows_to_push = 2048 + 100;
    ScrollbackRing ring(10, rows_to_push + 1000);

    for (int i = 0; i < rows_to_push; ++i) {
        ring.pushRow(makeTaggedRow(10, i));
    }

    EXPECT_EQ(ring.size(), static_cast<size_t>(rows_to_push));

    // Verify first and last rows
    TermCell first = ring.cellAt(0, 0);
    EXPECT_EQ(first.codepoint, static_cast<char32_t>('A' + (0 % 26)));

    TermCell last = ring.cellAt(rows_to_push - 1, 0);
    int last_tag = rows_to_push - 1;
    EXPECT_EQ(last.codepoint, static_cast<char32_t>('A' + (last_tag % 26)));
}

// ─── Resize ─────────────────────────────────────────────────────────────────

TEST(ScrollbackRing, SetMaxRowsShrinks) {
    ScrollbackRing ring(10, 100);

    for (int i = 0; i < 50; ++i) {
        ring.pushRow(makeTaggedRow(10, i));
    }
    EXPECT_EQ(ring.size(), 50u);

    // Shrink to 20 - should evict 30 oldest rows
    ring.setMaxRows(20);
    EXPECT_EQ(ring.maxRows(), 20u);
    EXPECT_EQ(ring.size(), 20u);

    // Oldest visible should now be tag 30
    TermCell tc = ring.cellAt(0, 0);
    char32_t expected = static_cast<char32_t>('A' + (30 % 26));
    EXPECT_EQ(tc.codepoint, expected);
}

TEST(ScrollbackRing, SetMaxRowsGrows) {
    ScrollbackRing ring(10, 20);

    for (int i = 0; i < 20; ++i) {
        ring.pushRow(makeTaggedRow(10, i));
    }
    EXPECT_EQ(ring.size(), 20u);

    // Grow capacity - should not change existing data
    ring.setMaxRows(100);
    EXPECT_EQ(ring.maxRows(), 100u);
    EXPECT_EQ(ring.size(), 20u);

    // Verify data still intact
    for (int r = 0; r < 20; ++r) {
        char32_t expected = static_cast<char32_t>('A' + (r % 26));
        TermCell tc = ring.cellAt(r, 0);
        EXPECT_EQ(tc.codepoint, expected) << "Row " << r;
    }
}

TEST(ScrollbackRing, ClearResetsState) {
    ScrollbackRing ring(10, 100);

    for (int i = 0; i < 50; ++i) {
        ring.pushRow(makeRow(10, "data"));
    }
    int64_t evicted_before = ring.evictedCount();

    ring.clear();

    EXPECT_EQ(ring.size(), 0u);
    // evictedCount should increase by the number of rows that were in the ring
    EXPECT_EQ(ring.evictedCount(), evicted_before + 50);
    EXPECT_EQ(ring.maxRows(), 100u);
    EXPECT_EQ(ring.cols(), 10);
}

// ─── Cell Content ───────────────────────────────────────────────────────────

TEST(ScrollbackRing, AsciiCharacterStorage) {
    ScrollbackRing ring(26, 100);
    std::string alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    ring.pushRow(makeRow(26, alpha));

    for (int c = 0; c < 26; ++c) {
        TermCell tc = ring.cellAt(0, c);
        EXPECT_EQ(tc.codepoint, static_cast<char32_t>('A' + c));
        EXPECT_EQ(tc.width, 1);
    }

    // Also verify rowText
    std::string text = ring.rowText(0);
    EXPECT_EQ(text, alpha);
}

TEST(ScrollbackRing, WideCharacterStorage) {
    ScrollbackRing ring(10, 100);

    std::vector<TermCell> row(10);
    // Place a CJK character (width 2) at column 0
    row[0].codepoint = U'\u4E16'; // '世'
    row[0].width = 2;
    // Column 1 is the continuation cell
    row[1].codepoint = 0;
    row[1].width = 0;
    // Fill the rest with spaces
    for (int i = 2; i < 10; ++i) {
        row[i].codepoint = U' ';
        row[i].width = 1;
    }

    ring.pushRow(row);

    TermCell tc0 = ring.cellAt(0, 0);
    EXPECT_EQ(tc0.codepoint, U'\u4E16');
    EXPECT_EQ(tc0.width, 2);

    TermCell tc1 = ring.cellAt(0, 1);
    EXPECT_EQ(tc1.codepoint, static_cast<char32_t>(0));
    EXPECT_EQ(tc1.width, 0);
}

TEST(ScrollbackRing, EmptyRowStorage) {
    ScrollbackRing ring(10, 100);
    // Push a row of all default (space) cells
    ring.pushRow(makeRow(10));

    // rowText should return empty string (spaces are trimmed)
    std::string text = ring.rowText(0);
    EXPECT_EQ(text, "");

    // But cells should still be readable
    TermCell tc = ring.cellAt(0, 0);
    EXPECT_EQ(tc.codepoint, U' ');
}

TEST(ScrollbackRing, FullWidthRowStorage) {
    const int cols = 20;
    ScrollbackRing ring(cols, 100);
    std::string full = "12345678901234567890";
    ring.pushRow(makeRow(cols, full));

    for (int c = 0; c < cols; ++c) {
        TermCell tc = ring.cellAt(0, c);
        EXPECT_EQ(tc.codepoint, static_cast<char32_t>(full[c]))
            << "Mismatch at col " << c;
    }

    std::string text = ring.rowText(0);
    EXPECT_EQ(text, full);
}

// ─── Cell Attributes ────────────────────────────────────────────────────────

TEST(ScrollbackRing, CellAttributesPreserved) {
    ScrollbackRing ring(5, 100);

    std::vector<TermCell> row(5);
    row[0].codepoint = U'X';
    row[0].width = 1;
    row[0].fg_color = 0x00FF0000; // Red
    row[0].bg_color = 0x0000FF00; // Green
    row[0].attributes = AttrBold | AttrItalic;
    row[0].underline_style = UnderlineCurly;
    row[0].underline_color = 0x000000FF; // Blue
    for (int i = 1; i < 5; ++i) {
        row[i].codepoint = U' ';
        row[i].width = 1;
    }

    ring.pushRow(row);

    TermCell tc = ring.cellAt(0, 0);
    EXPECT_EQ(tc.codepoint, U'X');
    EXPECT_EQ(tc.fg_color, 0x00FF0000u);
    EXPECT_EQ(tc.bg_color, 0x0000FF00u);
    EXPECT_EQ(tc.attributes, AttrBold | AttrItalic);
    EXPECT_EQ(tc.underline_style, UnderlineCurly);
    EXPECT_EQ(tc.underline_color, 0x000000FFu);
}

// ─── Edge Cases ─────────────────────────────────────────────────────────────

TEST(ScrollbackRing, ZeroMaxRows) {
    ScrollbackRing ring(10, 0);
    EXPECT_EQ(ring.size(), 0u);
    EXPECT_EQ(ring.maxRows(), 0u);

    // With maxRows=0, pushRow should be a no-op — nothing stored.
    ring.pushRow(makeRow(10, "Hello"));
    EXPECT_EQ(ring.size(), 0u);

    // Multiple pushes still result in zero rows stored.
    ring.pushRow(makeRow(10, "World"));
    EXPECT_EQ(ring.size(), 0u);
    EXPECT_EQ(ring.evictedCount(), 0);

    // cellAt and rowText should return defaults for empty ring.
    TermCell tc = ring.cellAt(0, 0);
    EXPECT_EQ(tc.codepoint, U' ');
    EXPECT_EQ(ring.rowText(0), "");
}

TEST(ScrollbackRing, SingleRowCapacity) {
    ScrollbackRing ring(10, 1);

    ring.pushRow(makeRow(10, "First"));
    EXPECT_EQ(ring.size(), 1u);
    EXPECT_EQ(ring.cellAt(0, 0).codepoint, static_cast<char32_t>('F'));

    // Push second row - first should be evicted
    ring.pushRow(makeRow(10, "Second"));
    EXPECT_EQ(ring.size(), 1u);
    EXPECT_EQ(ring.cellAt(0, 0).codepoint, static_cast<char32_t>('S'));
    EXPECT_EQ(ring.evictedCount(), 1);
}

TEST(ScrollbackRing, PushAfterClear) {
    ScrollbackRing ring(10, 100);

    ring.pushRow(makeRow(10, "Before"));
    ring.clear();
    EXPECT_EQ(ring.size(), 0u);

    ring.pushRow(makeRow(10, "After"));
    EXPECT_EQ(ring.size(), 1u);

    TermCell tc = ring.cellAt(0, 0);
    EXPECT_EQ(tc.codepoint, static_cast<char32_t>('A'));

    std::string text = ring.rowText(0);
    EXPECT_EQ(text, "After");
}

TEST(ScrollbackRing, LargeRowCount) {
    const int total = 10000;
    const int maxRows = 5000;
    ScrollbackRing ring(10, maxRows);

    for (int i = 0; i < total; ++i) {
        ring.pushRow(makeTaggedRow(10, i));
    }

    EXPECT_EQ(ring.size(), static_cast<size_t>(maxRows));
    EXPECT_EQ(ring.evictedCount(), total - maxRows);

    // Verify oldest visible row: tag = total - maxRows = 5000
    int oldest_tag = total - maxRows;
    char32_t expected = static_cast<char32_t>('A' + (oldest_tag % 26));
    EXPECT_EQ(ring.cellAt(0, 0).codepoint, expected);

    // Verify newest row: tag = total - 1 = 9999
    int newest_tag = total - 1;
    expected = static_cast<char32_t>('A' + (newest_tag % 26));
    EXPECT_EQ(ring.cellAt(maxRows - 1, 0).codepoint, expected);

    // Spot-check a few rows in the middle
    for (int r = 0; r < maxRows; r += 1000) {
        int tag = oldest_tag + r;
        expected = static_cast<char32_t>('A' + (tag % 26));
        EXPECT_EQ(ring.cellAt(r, 0).codepoint, expected)
            << "Integrity check failed at row " << r << " (tag " << tag << ")";
    }
}

// ─── Out-of-bounds Access ───────────────────────────────────────────────────

TEST(ScrollbackRing, CellAtOutOfBoundsReturnsDefault) {
    ScrollbackRing ring(10, 100);
    ring.pushRow(makeRow(10, "Hello"));

    // Negative row index
    TermCell tc = ring.cellAt(-1, 0);
    EXPECT_EQ(tc.codepoint, U' '); // default TermCell has codepoint = ' '

    // Row index beyond size
    tc = ring.cellAt(1, 0);
    EXPECT_EQ(tc.codepoint, U' ');

    // Column out of bounds
    tc = ring.cellAt(0, 100);
    EXPECT_EQ(tc.codepoint, U' ');
}

TEST(ScrollbackRing, RowTextOutOfBoundsReturnsEmpty) {
    ScrollbackRing ring(10, 100);
    ring.pushRow(makeRow(10, "Hello"));

    EXPECT_EQ(ring.rowText(-1), "");
    EXPECT_EQ(ring.rowText(1), "");
}

// ─── Move Semantics ─────────────────────────────────────────────────────────

TEST(ScrollbackRing, MoveConstructor) {
    ScrollbackRing ring(10, 100);
    ring.pushRow(makeRow(10, "Hello"));
    ring.pushRow(makeRow(10, "World"));

    ScrollbackRing moved(std::move(ring));
    EXPECT_EQ(moved.size(), 2u);
    EXPECT_EQ(moved.rowText(0), "Hello");
    EXPECT_EQ(moved.rowText(1), "World");
}

TEST(ScrollbackRing, MoveAssignment) {
    ScrollbackRing ring(10, 100);
    ring.pushRow(makeRow(10, "Hello"));

    ScrollbackRing other(5, 50);
    other = std::move(ring);

    EXPECT_EQ(other.size(), 1u);
    EXPECT_EQ(other.cols(), 10);
    EXPECT_EQ(other.rowText(0), "Hello");
}

// ─── RowText ────────────────────────────────────────────────────────────────

TEST(ScrollbackRing, RowTextTrimsTrailingSpaces) {
    ScrollbackRing ring(20, 100);
    ring.pushRow(makeRow(20, "Hello"));
    // "Hello" followed by 15 spaces -> trimmed to "Hello"
    EXPECT_EQ(ring.rowText(0), "Hello");
}

TEST(ScrollbackRing, RowTextMultipleRows) {
    ScrollbackRing ring(10, 100);
    ring.pushRow(makeRow(10, "First"));
    ring.pushRow(makeRow(10, "Second"));
    ring.pushRow(makeRow(10, "Third"));

    EXPECT_EQ(ring.rowText(0), "First");
    EXPECT_EQ(ring.rowText(1), "Second");
    EXPECT_EQ(ring.rowText(2), "Third");
}

} // namespace
} // namespace termcore
