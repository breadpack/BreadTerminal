#include <gtest/gtest.h>
#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include "termcore/font/unicode_width.h"

using namespace termcore;

// Helper: feed a UTF-8 string through VtParser into a Screen
static void feedUtf8(VtParser& parser, const std::string& str) {
    parser.feed(str.data(), str.size());
}

// Helper: create a UTF-8 string from a single codepoint
static std::string toUtf8(char32_t cp) {
    std::string s;
    utf8_encode(cp, s);
    return s;
}

// Helper: create a UTF-8 string from multiple codepoints
static std::string toUtf8(std::initializer_list<char32_t> cps) {
    std::string s;
    for (char32_t cp : cps)
        utf8_encode(cp, s);
    return s;
}

TEST(GraphemeCluster, CombiningMarkAttachesToBase) {
    Screen screen(24, 80);
    VtParser parser(screen);

    // 'e' followed by combining acute accent U+0301
    feedUtf8(parser, toUtf8({'e', 0x0301}));

    const TermCell& cell = screen.cellAt(0, 0);
    EXPECT_EQ(cell.codepoint, U'e');
    EXPECT_EQ(cell.extra_count, 1);
    EXPECT_EQ(cell.extra[0], char32_t(0x0301));
    EXPECT_EQ(cell.width, 1);

    // Cursor should be at col 1 (not col 2)
    EXPECT_EQ(screen.cursorCol(), 1);
}

TEST(GraphemeCluster, VariationSelector16MakesEmojiWide) {
    Screen screen(24, 80);
    VtParser parser(screen);

    // Heart U+2764 + VS16 U+FE0F -> emoji presentation, width 2
    feedUtf8(parser, toUtf8({0x2764, 0xFE0F}));

    const TermCell& cell = screen.cellAt(0, 0);
    EXPECT_EQ(cell.codepoint, char32_t(0x2764));
    EXPECT_EQ(cell.extra_count, 1);
    EXPECT_EQ(cell.extra[0], char32_t(0xFE0F));
    EXPECT_EQ(cell.width, 2);

    // Continuation cell
    const TermCell& cont = screen.cellAt(0, 1);
    EXPECT_EQ(cont.codepoint, char32_t(0));
    EXPECT_EQ(cont.width, 0);
}

TEST(GraphemeCluster, SkinToneModifier) {
    Screen screen(24, 80);

    // Waving hand U+1F44B + skin tone modifier U+1F3FD
    // Use direct onPrint to avoid VtParser encoding issues
    screen.onPrint(0x1F44B);
    screen.onPrint(0x1F3FD);

    const TermCell& cell = screen.cellAt(0, 0);
    EXPECT_EQ(cell.codepoint, char32_t(0x1F44B));
    EXPECT_EQ(cell.extra_count, 1);
    EXPECT_EQ(cell.extra[0], char32_t(0x1F3FD));
    EXPECT_EQ(cell.width, 2);
}

TEST(GraphemeCluster, ZWJSequence) {
    Screen screen(24, 80);
    VtParser parser(screen);

    // Woman + ZWJ + Laptop: U+1F469 U+200D U+1F4BB
    feedUtf8(parser, toUtf8({0x1F469, 0x200D, 0x1F4BB}));

    const TermCell& cell = screen.cellAt(0, 0);
    EXPECT_EQ(cell.codepoint, char32_t(0x1F469));
    EXPECT_EQ(cell.extra_count, 2);
    EXPECT_EQ(cell.extra[0], char32_t(0x200D));
    EXPECT_EQ(cell.extra[1], char32_t(0x1F4BB));
    EXPECT_EQ(cell.width, 2);
}

TEST(GraphemeCluster, FlagSequence) {
    Screen screen(24, 80);
    VtParser parser(screen);

    // US flag: U+1F1FA U+1F1F8
    feedUtf8(parser, toUtf8({0x1F1FA, 0x1F1F8}));

    const TermCell& cell = screen.cellAt(0, 0);
    EXPECT_EQ(cell.codepoint, char32_t(0x1F1FA));
    EXPECT_EQ(cell.extra_count, 1);
    EXPECT_EQ(cell.extra[0], char32_t(0x1F1F8));
    EXPECT_EQ(cell.width, 2);
}

TEST(GraphemeCluster, GetLineTextIncludesExtras) {
    Screen screen(24, 80);
    VtParser parser(screen);

    // 'e' + combining acute
    std::string input = toUtf8({'e', 0x0301});
    feedUtf8(parser, input);

    std::string text = screen.getLineText(0);
    EXPECT_EQ(text, input);
}

TEST(GraphemeCluster, AllCodepointsMethod) {
    TermCell cell;
    cell.codepoint = U'A';
    cell.extra_count = 0;

    auto cps = cell.allCodepoints();
    ASSERT_EQ(cps.size(), 1u);
    EXPECT_EQ(cps[0], U'A');

    cell.appendCodepoint(0x0301);
    cps = cell.allCodepoints();
    ASSERT_EQ(cps.size(), 2u);
    EXPECT_EQ(cps[0], U'A');
    EXPECT_EQ(cps[1], char32_t(0x0301));
}

TEST(GraphemeCluster, AppendCodepointRejectsFull) {
    TermCell cell;
    cell.codepoint = U'A';
    for (int i = 0; i < kMaxExtraCodepoints; ++i) {
        EXPECT_TRUE(cell.appendCodepoint(0x0300 + i));
    }
    // Should be full now
    EXPECT_FALSE(cell.appendCodepoint(0x0399));
    EXPECT_EQ(cell.extra_count, kMaxExtraCodepoints);
}

TEST(GraphemeCluster, SplitGraphemesConsistency) {
    // Verify split_graphemes output matches our onPrint combining logic
    // Woman technologist: U+1F469 U+200D U+1F4BB in UTF-8
    std::string input = toUtf8({0x1F469, 0x200D, 0x1F4BB});
    auto clusters = split_graphemes(input);
    EXPECT_EQ(clusters.size(), 1u);
}

TEST(GraphemeCluster, MultipleGraphemesInSequence) {
    Screen screen(24, 80);
    VtParser parser(screen);

    // Two combining mark clusters: e+acute then a+tilde
    feedUtf8(parser, toUtf8({'e', 0x0301, 'a', 0x0303}));

    const TermCell& cell0 = screen.cellAt(0, 0);
    EXPECT_EQ(cell0.codepoint, U'e');
    EXPECT_EQ(cell0.extra_count, 1);
    EXPECT_EQ(cell0.extra[0], char32_t(0x0301));

    const TermCell& cell1 = screen.cellAt(0, 1);
    EXPECT_EQ(cell1.codepoint, U'a');
    EXPECT_EQ(cell1.extra_count, 1);
    EXPECT_EQ(cell1.extra[0], char32_t(0x0303));
}

TEST(GraphemeCluster, EraseCellClearsExtras) {
    Screen screen(24, 80);
    VtParser parser(screen);

    feedUtf8(parser, toUtf8({'e', 0x0301}));

    // Erase the line
    feedUtf8(parser, "\x1b[2K");

    const TermCell& cell = screen.cellAt(0, 0);
    EXPECT_EQ(cell.codepoint, U' ');
    EXPECT_EQ(cell.extra_count, 0);
}
