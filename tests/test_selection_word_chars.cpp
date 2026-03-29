#include <gtest/gtest.h>
#include "termcore/selection_manager.h"
#include "termcore/screen.h"
#include "termcore/vt_parser.h"

using namespace termcore;

class SelectionWordCharsTest : public ::testing::Test {
protected:
    // Helper: simulate a double-click at pixel coordinates targeting a specific column.
    // Uses cellW=8, cellH=16, no offset, so col = px/8, row = py/16.
    void doubleClick(SelectionManager& sel, Screen& screen, int row, int col) {
        float cellW = 8.0f, cellH = 16.0f;
        int px = static_cast<int>(col * cellW + cellW / 2);
        int py = static_cast<int>(row * cellH + cellH / 2);
        sel.onDoubleClick(px, py, cellW, cellH, 0, 0, screen);
    }

    std::string getWord(SelectionManager& sel, Screen& screen) {
        return sel.getSelectedText(screen);
    }
};

// Default word chars include alphanumeric, underscore, and - . (from default config)
TEST_F(SelectionWordCharsTest, DefaultWordCharsSelectsAlphanumericUnderscore) {
    Screen screen(1, 40);
    VtParser parser(screen);
    std::string text = "hello world_test foo";
    parser.feed(text.data(), text.size());

    SelectionManager sel;
    // Default has no extra word chars — underscore is always default
    doubleClick(sel, screen, 0, 7);
    EXPECT_EQ(getWord(sel, screen), "world_test");
}

// With extra word chars, hyphenated words become a single selection
TEST_F(SelectionWordCharsTest, HyphenAsWordChar) {
    Screen screen(1, 40);
    VtParser parser(screen);
    std::string text = "foo-bar baz";
    parser.feed(text.data(), text.size());

    SelectionManager sel;
    // Without extra chars, hyphen is not a word char
    doubleClick(sel, screen, 0, 1);
    EXPECT_EQ(getWord(sel, screen), "foo");

    // With hyphen as word char
    sel.setWordChars("-");
    doubleClick(sel, screen, 0, 1);
    EXPECT_EQ(getWord(sel, screen), "foo-bar");
}

// With dot as word char, filenames are selected as whole words
TEST_F(SelectionWordCharsTest, DotAsWordChar) {
    Screen screen(1, 40);
    VtParser parser(screen);
    std::string text = "file.txt other";
    parser.feed(text.data(), text.size());

    SelectionManager sel;
    // Without dot
    doubleClick(sel, screen, 0, 1);
    EXPECT_EQ(getWord(sel, screen), "file");

    // With dot
    sel.setWordChars(".");
    doubleClick(sel, screen, 0, 1);
    EXPECT_EQ(getWord(sel, screen), "file.txt");
}

// Multiple extra word chars at once
TEST_F(SelectionWordCharsTest, MultipleExtraChars) {
    Screen screen(1, 60);
    VtParser parser(screen);
    std::string text = "my-file.name~v2 other";
    parser.feed(text.data(), text.size());

    SelectionManager sel;
    sel.setWordChars("-_.~");
    doubleClick(sel, screen, 0, 3);
    EXPECT_EQ(getWord(sel, screen), "my-file.name~v2");
}

// setWordChars replaces previous value
TEST_F(SelectionWordCharsTest, SetWordCharsReplaces) {
    Screen screen(1, 40);
    VtParser parser(screen);
    std::string text = "a-b.c rest";
    parser.feed(text.data(), text.size());

    SelectionManager sel;
    sel.setWordChars("-.");
    doubleClick(sel, screen, 0, 2);
    EXPECT_EQ(getWord(sel, screen), "a-b.c");

    // Now set only hyphen — dot should no longer be a word char
    sel.setWordChars("-");
    doubleClick(sel, screen, 0, 2);
    EXPECT_EQ(getWord(sel, screen), "a-b");
}

// Empty word chars string means only alphanumeric + underscore + non-ASCII
TEST_F(SelectionWordCharsTest, EmptyWordChars) {
    Screen screen(1, 40);
    VtParser parser(screen);
    std::string text = "foo-bar baz";
    parser.feed(text.data(), text.size());

    SelectionManager sel;
    sel.setWordChars("");
    doubleClick(sel, screen, 0, 1);
    EXPECT_EQ(getWord(sel, screen), "foo");
}
