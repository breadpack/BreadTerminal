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

// --- Extended C API: Title ---
TEST(CApi, GetTitleAfterOsc0) {
    TermCore* core = tc_create();
    TermPane* pane = tc_pane_create(core, 24, 80);

    // OSC 0 ; title ST
    const char* seq = "\x1b]0;My Terminal\x07";
    tc_pane_feed(pane, seq, strlen(seq));

    EXPECT_STREQ(tc_pane_get_title(pane), "My Terminal");

    tc_destroy(core);
}

// --- Extended C API: Working directory ---
TEST(CApi, GetWorkingDirAfterOsc7) {
    TermCore* core = tc_create();
    TermPane* pane = tc_pane_create(core, 24, 80);

    const char* seq = "\x1b]7;file:///Users/test/project\x07";
    tc_pane_feed(pane, seq, strlen(seq));

    std::string cwd = tc_pane_get_working_dir(pane);
    EXPECT_FALSE(cwd.empty());

    tc_destroy(core);
}

// --- Extended C API: Cursor style default ---
TEST(CApi, GetCursorStyleDefault) {
    TermCore* core = tc_create();
    TermPane* pane = tc_pane_create(core, 24, 80);

    EXPECT_EQ(tc_pane_get_cursor_style(pane), 0); // Block

    tc_destroy(core);
}

// --- Extended C API: Cursor style after DECSCUSR ---
TEST(CApi, GetCursorStyleAfterDecscusr) {
    TermCore* core = tc_create();
    TermPane* pane = tc_pane_create(core, 24, 80);

    // CSI 5 SP q = blinking bar (Bar = 2)
    const char* seq = "\x1b[5 q";
    tc_pane_feed(pane, seq, strlen(seq));

    EXPECT_EQ(tc_pane_get_cursor_style(pane), 2); // Bar

    tc_destroy(core);
}

// --- Extended C API: Cursor blink ---
TEST(CApi, GetCursorBlink) {
    TermCore* core = tc_create();
    TermPane* pane = tc_pane_create(core, 24, 80);

    // Default cursor blinks
    EXPECT_EQ(tc_pane_get_cursor_blink(pane), 1);

    // CSI 2 SP q = steady block (no blink)
    const char* seq = "\x1b[2 q";
    tc_pane_feed(pane, seq, strlen(seq));
    EXPECT_EQ(tc_pane_get_cursor_blink(pane), 0);

    tc_destroy(core);
}

// --- Extended C API: Scrollback size initially zero ---
TEST(CApi, ScrollbackSizeInitial) {
    TermCore* core = tc_create();
    TermPane* pane = tc_pane_create(core, 24, 80);

    EXPECT_EQ(tc_pane_scrollback_size(pane), 0);

    tc_destroy(core);
}

// --- Extended C API: Scrollback size after scrolling ---
TEST(CApi, ScrollbackSizeAfterScroll) {
    TermCore* core = tc_create();
    TermPane* pane = tc_pane_create(core, 4, 80);

    // Fill screen and force scrollback
    const char* text = "Line1\nLine2\nLine3\nLine4\nLine5\nLine6\n";
    tc_pane_feed(pane, text, strlen(text));

    EXPECT_GT(tc_pane_scrollback_size(pane), 0);

    tc_destroy(core);
}

// --- Extended C API: Get scrollback line text ---
TEST(CApi, GetScrollbackLine) {
    TermCore* core = tc_create();
    TermPane* pane = tc_pane_create(core, 4, 80);

    // Write enough lines to push some into scrollback
    const char* text = "AAA\nBBB\nCCC\nDDD\nEEE\nFFF\n";
    tc_pane_feed(pane, text, strlen(text));

    int sb_size = tc_pane_scrollback_size(pane);
    ASSERT_GT(sb_size, 0);

    // Line 0 is most recent scrollback line
    const char* line = tc_pane_get_scrollback_line(pane, 0);
    ASSERT_NE(line, nullptr);
    EXPECT_GT(strlen(line), 0u);

    tc_destroy(core);
}

// --- Extended C API: Alt screen default ---
TEST(CApi, AltScreenDefault) {
    TermCore* core = tc_create();
    TermPane* pane = tc_pane_create(core, 24, 80);

    EXPECT_EQ(tc_pane_alt_screen_active(pane), 0);

    tc_destroy(core);
}

// --- Extended C API: Alt screen after DECSET 1049 ---
TEST(CApi, AltScreenAfterDecset) {
    TermCore* core = tc_create();
    TermPane* pane = tc_pane_create(core, 24, 80);

    const char* seq = "\x1b[?1049h";
    tc_pane_feed(pane, seq, strlen(seq));

    EXPECT_EQ(tc_pane_alt_screen_active(pane), 1);

    tc_destroy(core);
}

// --- Extended C API: Bracketed paste default ---
TEST(CApi, BracketedPasteDefault) {
    TermCore* core = tc_create();
    TermPane* pane = tc_pane_create(core, 24, 80);

    EXPECT_EQ(tc_pane_bracketed_paste(pane), 0);

    tc_destroy(core);
}

// --- Extended C API: App cursor keys default ---
TEST(CApi, AppCursorKeysDefault) {
    TermCore* core = tc_create();
    TermPane* pane = tc_pane_create(core, 24, 80);

    EXPECT_EQ(tc_pane_app_cursor_keys(pane), 0);

    tc_destroy(core);
}

// --- Extended C API: Null safety for new functions ---
TEST(CApi, ExtendedNullSafety) {
    EXPECT_STREQ(tc_pane_get_title(nullptr), "");
    EXPECT_STREQ(tc_pane_get_working_dir(nullptr), "");
    EXPECT_EQ(tc_pane_get_cursor_style(nullptr), 0);
    EXPECT_EQ(tc_pane_get_cursor_blink(nullptr), 0);
    EXPECT_EQ(tc_pane_scrollback_size(nullptr), 0);
    EXPECT_STREQ(tc_pane_get_scrollback_line(nullptr, 0), "");
    EXPECT_EQ(tc_pane_alt_screen_active(nullptr), 0);
    EXPECT_EQ(tc_pane_bracketed_paste(nullptr), 0);
    EXPECT_EQ(tc_pane_app_cursor_keys(nullptr), 0);
}
