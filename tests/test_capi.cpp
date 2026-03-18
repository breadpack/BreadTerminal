#include <gtest/gtest.h>
#include "termcore/termcore.h"
#include <cstring>
#include <string>

// --- 1. Create/destroy TermCore ---
TEST(CApi, CreateDestroy) {
    TermCore* core = tc_create();
    ASSERT_NE(core, nullptr);
    tc_destroy(core);
}

// --- 2. Create pane, check default rows/cols ---
TEST(CApi, PaneCreateDefaults) {
    TermCore* core = tc_create();
    TermPane* pane = tc_pane_create(core, 24, 80);
    ASSERT_NE(pane, nullptr);
    EXPECT_EQ(tc_pane_rows(pane), 24);
    EXPECT_EQ(tc_pane_cols(pane), 80);
    tc_pane_destroy(pane);
    tc_destroy(core);
}

// --- 3. Feed text data, query cells ---
TEST(CApi, FeedTextQueryCells) {
    TermCore* core = tc_create();
    TermPane* pane = tc_pane_create(core, 24, 80);

    const char* text = "Hello";
    tc_pane_feed(pane, text, strlen(text));

    TermCellData cell{};
    tc_pane_get_cell(pane, 0, 0, &cell);
    EXPECT_EQ(cell.codepoint, static_cast<uint32_t>('H'));

    tc_pane_get_cell(pane, 0, 1, &cell);
    EXPECT_EQ(cell.codepoint, static_cast<uint32_t>('e'));

    tc_pane_get_cell(pane, 0, 4, &cell);
    EXPECT_EQ(cell.codepoint, static_cast<uint32_t>('o'));

    tc_destroy(core);
}

// --- 4. Feed CSI SGR sequence, verify attributes ---
TEST(CApi, FeedSgrAttributes) {
    TermCore* core = tc_create();
    TermPane* pane = tc_pane_create(core, 24, 80);

    // ESC[1m = bold, then 'A'
    const char* seq = "\x1b[1mA";
    tc_pane_feed(pane, seq, strlen(seq));

    TermCellData cell{};
    tc_pane_get_cell(pane, 0, 0, &cell);
    EXPECT_EQ(cell.codepoint, static_cast<uint32_t>('A'));
    EXPECT_NE(cell.attributes & 1, 0u); // AttrBold = 1

    tc_destroy(core);
}

// --- 5. Feed text with cursor movement, verify cursor position ---
TEST(CApi, CursorPosition) {
    TermCore* core = tc_create();
    TermPane* pane = tc_pane_create(core, 24, 80);

    // Move cursor to row 3, col 5 (1-based: ESC[3;5H)
    const char* seq = "\x1b[3;5H";
    tc_pane_feed(pane, seq, strlen(seq));

    TermCursorData cursor{};
    tc_pane_get_cursor(pane, &cursor);
    EXPECT_EQ(cursor.row, 2); // 0-based
    EXPECT_EQ(cursor.col, 4); // 0-based
    EXPECT_EQ(cursor.visible, 1);

    tc_destroy(core);
}

// --- 6. Get line text ---
TEST(CApi, GetLineText) {
    TermCore* core = tc_create();
    TermPane* pane = tc_pane_create(core, 24, 80);

    const char* text = "World";
    tc_pane_feed(pane, text, strlen(text));

    const char* line = tc_pane_get_line_text(pane, 0);
    ASSERT_NE(line, nullptr);
    // Line text should start with "World" (may be padded with spaces)
    std::string lineStr(line);
    EXPECT_EQ(lineStr.substr(0, 5), "World");

    tc_destroy(core);
}

// --- 7. Spawn a shell process, verify it's alive ---
TEST(CApi, SpawnShell) {
    TermCore* core = tc_create();
    TermPane* pane = tc_pane_create(core, 24, 80);

    int ret = tc_pane_spawn(pane, nullptr);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(tc_pane_is_alive(pane), 1);

    tc_pane_destroy(pane);
    tc_destroy(core);
}

// --- 8. Pane resize ---
TEST(CApi, PaneResize) {
    TermCore* core = tc_create();
    TermPane* pane = tc_pane_create(core, 24, 80);

    tc_pane_resize(pane, 40, 120);
    EXPECT_EQ(tc_pane_rows(pane), 40);
    EXPECT_EQ(tc_pane_cols(pane), 120);

    tc_destroy(core);
}

// --- 9. Multiple panes on one core ---
TEST(CApi, MultiplePanes) {
    TermCore* core = tc_create();
    TermPane* pane1 = tc_pane_create(core, 24, 80);
    TermPane* pane2 = tc_pane_create(core, 30, 100);

    EXPECT_NE(pane1, pane2);
    EXPECT_EQ(tc_pane_rows(pane1), 24);
    EXPECT_EQ(tc_pane_cols(pane1), 80);
    EXPECT_EQ(tc_pane_rows(pane2), 30);
    EXPECT_EQ(tc_pane_cols(pane2), 100);

    // Feed different text to each
    tc_pane_feed(pane1, "AAA", 3);
    tc_pane_feed(pane2, "BBB", 3);

    TermCellData cell{};
    tc_pane_get_cell(pane1, 0, 0, &cell);
    EXPECT_EQ(cell.codepoint, static_cast<uint32_t>('A'));

    tc_pane_get_cell(pane2, 0, 0, &cell);
    EXPECT_EQ(cell.codepoint, static_cast<uint32_t>('B'));

    tc_pane_destroy(pane1);
    tc_pane_destroy(pane2);
    tc_destroy(core);
}

// --- Version ---
TEST(CApi, Version) {
    const char* ver = termcore_version();
    ASSERT_NE(ver, nullptr);
    EXPECT_STREQ(ver, "0.1.0");
}

// --- Null safety ---
TEST(CApi, NullSafety) {
    // These should not crash
    tc_destroy(nullptr);
    tc_pane_destroy(nullptr);
    tc_pane_resize(nullptr, 10, 10);
    tc_pane_feed(nullptr, "x", 1);
    EXPECT_EQ(tc_pane_rows(nullptr), 0);
    EXPECT_EQ(tc_pane_cols(nullptr), 0);
    EXPECT_EQ(tc_pane_is_alive(nullptr), 0);
    EXPECT_EQ(tc_pane_spawn(nullptr, nullptr), -1);
    EXPECT_EQ(tc_pane_read_pty(nullptr, nullptr, 0), -1);
    EXPECT_EQ(tc_pane_write_pty(nullptr, nullptr, 0), -1);

    TermCellData cell{};
    tc_pane_get_cell(nullptr, 0, 0, &cell);

    TermCursorData cursor{};
    tc_pane_get_cursor(nullptr, &cursor);

    tc_set_notify_callback(nullptr, nullptr, nullptr);
}
