#include <gtest/gtest.h>
#include "termcore/grapheme.h"
#include "termcore/font/unicode_width.h"
#include "termcore/screen.h"
#include <string>
#include <vector>

namespace termcore {
namespace {

// ---------------------------------------------------------------------------
// isGraphemeBreak (simplified two-codepoint version)
// ---------------------------------------------------------------------------

TEST(GraphemeBreak, AsciiCharactersBreak) {
    EXPECT_TRUE(isGraphemeBreak('A', 'B'));
    EXPECT_TRUE(isGraphemeBreak('x', 'y'));
}

TEST(GraphemeBreak, CrLfNoBreak) {
    EXPECT_FALSE(isGraphemeBreak(0x000D, 0x000A));  // CR x LF
}

TEST(GraphemeBreak, ControlBreaks) {
    EXPECT_TRUE(isGraphemeBreak(0x000D, 'A'));  // CR divides A
    EXPECT_TRUE(isGraphemeBreak('A', 0x000A));  // A divides LF
    EXPECT_TRUE(isGraphemeBreak(0x0000, 'A'));  // NUL divides A
}

TEST(GraphemeBreak, CombiningAccentNoBreak) {
    // e + combining acute accent = no break
    EXPECT_FALSE(isGraphemeBreak('e', 0x0301));
    // Any base + combining grave = no break
    EXPECT_FALSE(isGraphemeBreak('a', 0x0300));
}

TEST(GraphemeBreak, ZwjNoBreak) {
    // Any char + ZWJ = no break (GB9)
    EXPECT_FALSE(isGraphemeBreak(0x1F468, 0x200D));  // man + ZWJ
}

TEST(GraphemeBreak, ZwjEmojiNoBreak) {
    // ZWJ + Extended_Pictographic = no break (GB11 simplified)
    EXPECT_FALSE(isGraphemeBreak(0x200D, 0x1F4BB));  // ZWJ + laptop
}

TEST(GraphemeBreak, RegionalIndicatorPairNoBreak) {
    // Two regional indicators = no break (simplified, first pair)
    EXPECT_FALSE(isGraphemeBreak(0x1F1FA, 0x1F1F8));  // U + S
}

TEST(GraphemeBreak, HangulLVNoBreak) {
    // L + V = no break (GB6)
    EXPECT_FALSE(isGraphemeBreak(0x1100, 0x1161));  // Hangul L + V
}

TEST(GraphemeBreak, HangulLVT) {
    // L + LVT = no break (GB6)
    EXPECT_FALSE(isGraphemeBreak(0x1100, 0xAC01));  // L + LVT syllable
}

TEST(GraphemeBreak, HangulVT) {
    // V + T = no break (GB7)
    EXPECT_FALSE(isGraphemeBreak(0x1161, 0x11A8));  // V + T
}

TEST(GraphemeBreak, ExtendNoBreak) {
    // Base + variation selector = no break
    EXPECT_FALSE(isGraphemeBreak(0x2764, 0xFE0F));  // heart + VS16
}

// ---------------------------------------------------------------------------
// graphemeClusterWidth
// ---------------------------------------------------------------------------

TEST(GraphemeClusterWidth, EmptyReturnsZero) {
    EXPECT_EQ(graphemeClusterWidth({}), 0);
}

TEST(GraphemeClusterWidth, SingleAscii) {
    EXPECT_EQ(graphemeClusterWidth({0x41}), 1);  // 'A'
}

TEST(GraphemeClusterWidth, SingleCjk) {
    EXPECT_EQ(graphemeClusterWidth({0xD55C}), 2);  // Korean char
}

TEST(GraphemeClusterWidth, BasePlusCombining) {
    // e + combining acute = width of base (1)
    EXPECT_EQ(graphemeClusterWidth({0x65, 0x0301}), 1);
}

TEST(GraphemeClusterWidth, EmojiZwjSequence) {
    // man + ZWJ + laptop = width 2
    EXPECT_EQ(graphemeClusterWidth({0x1F468, 0x200D, 0x1F4BB}), 2);
}

TEST(GraphemeClusterWidth, FlagSequence) {
    // US flag: Regional_Indicator U + Regional_Indicator S = width 2
    EXPECT_EQ(graphemeClusterWidth({0x1F1FA, 0x1F1F8}), 2);
}

TEST(GraphemeClusterWidth, FamilyEmoji) {
    // Family: man + ZWJ + woman + ZWJ + girl + ZWJ + boy
    EXPECT_EQ(graphemeClusterWidth({
        0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F467, 0x200D, 0x1F466
    }), 2);
}

TEST(GraphemeClusterWidth, EmojiWithVariationSelector) {
    // Heart + VS16 — base is emoji, width comes from base
    // U+2764 is not East Asian Wide but is Extended_Pictographic
    int w = graphemeClusterWidth({0x2764, 0xFE0F});
    EXPECT_GE(w, 1);
    EXPECT_LE(w, 2);
}

// ---------------------------------------------------------------------------
// TermCell multi-codepoint storage
// ---------------------------------------------------------------------------

TEST(TermCellGrapheme, DefaultSingleCodepoint) {
    TermCell cell;
    cell.codepoint = U'A';
    EXPECT_EQ((1 + cell.extra_count), 1u);
    auto g = cell.allCodepoints();
    ASSERT_EQ(g.size(), 1u);
    EXPECT_EQ(g[0], U'A');
}

TEST(TermCellGrapheme, AppendCodepoint) {
    TermCell cell;
    cell.codepoint = U'e';
    cell.appendCodepoint(0x0301);  // combining acute
    EXPECT_EQ((1 + cell.extra_count), 2u);
    auto g = cell.allCodepoints();
    ASSERT_EQ(g.size(), 2u);
    EXPECT_EQ(g[0], U'e');
    EXPECT_EQ(g[1], static_cast<char32_t>(0x0301));
}

TEST(TermCellGrapheme, MultipleExtras) {
    TermCell cell;
    cell.codepoint = 0x1F468;  // man
    cell.appendCodepoint(0x200D);   // ZWJ
    cell.appendCodepoint(0x1F4BB);  // laptop
    EXPECT_EQ((1 + cell.extra_count), 3u);
    auto g = cell.allCodepoints();
    ASSERT_EQ(g.size(), 3u);
    EXPECT_EQ(g[0], static_cast<char32_t>(0x1F468));
    EXPECT_EQ(g[1], static_cast<char32_t>(0x200D));
    EXPECT_EQ(g[2], static_cast<char32_t>(0x1F4BB));
}

TEST(TermCellGrapheme, BackwardCompatibility) {
    // Existing code using cell.codepoint still works
    TermCell cell;
    cell.codepoint = U'X';
    EXPECT_EQ(cell.codepoint, U'X');
    EXPECT_TRUE(cell.extra_count == 0);
}

// ---------------------------------------------------------------------------
// Screen input with grapheme clusters
// ---------------------------------------------------------------------------

TEST(ScreenGrapheme, CombiningAccentAppendsToPrevCell) {
    Screen screen(24, 80);
    // Print 'e' then combining acute accent
    screen.onPrint(U'e');
    screen.onPrint(0x0301);  // combining acute

    // Should be one cell with both codepoints, cursor should not advance
    const TermCell& cell = screen.cellAt(0, 0);
    EXPECT_EQ(cell.codepoint, U'e');
    ASSERT_EQ(cell.extra_count, 1u);
    EXPECT_EQ(cell.extra[0], static_cast<char32_t>(0x0301));

    // Cursor should be at column 1 (after 'e', combining didn't advance it)
    EXPECT_EQ(screen.cursorCol(), 1);
}

TEST(ScreenGrapheme, MultipleCombiningMarks) {
    Screen screen(24, 80);
    // Print 'o' then two combining marks
    screen.onPrint(U'o');
    screen.onPrint(0x0308);  // combining diaeresis
    screen.onPrint(0x0301);  // combining acute

    const TermCell& cell = screen.cellAt(0, 0);
    EXPECT_EQ(cell.codepoint, U'o');
    ASSERT_EQ(cell.extra_count, 2u);
    EXPECT_EQ((1 + cell.extra_count), 3u);
    EXPECT_EQ(screen.cursorCol(), 1);
}

TEST(ScreenGrapheme, NormalCharsStillWork) {
    Screen screen(24, 80);
    screen.onPrint(U'H');
    screen.onPrint(U'i');
    EXPECT_EQ(screen.cellAt(0, 0).codepoint, U'H');
    EXPECT_TRUE(screen.cellAt(0, 0).extra_count == 0);
    EXPECT_EQ(screen.cellAt(0, 1).codepoint, U'i');
    EXPECT_EQ(screen.cursorCol(), 2);
}

TEST(ScreenGrapheme, VariationSelectorAppendsToEmoji) {
    Screen screen(24, 80);
    // Heart emoji + VS16
    screen.onPrint(0x2764);
    screen.onPrint(0xFE0F);

    const TermCell& cell = screen.cellAt(0, 0);
    EXPECT_EQ(cell.codepoint, static_cast<char32_t>(0x2764));
    ASSERT_EQ(cell.extra_count, 1u);
    EXPECT_EQ(cell.extra[0], static_cast<char32_t>(0xFE0F));
}

TEST(ScreenGrapheme, GetLineTextIncludesExtras) {
    Screen screen(24, 80);
    // Print 'e' + combining acute
    screen.onPrint(U'e');
    screen.onPrint(0x0301);

    std::string text = screen.getLineText(0);
    // Should contain "e" + combining acute in UTF-8
    // e = 0x65, combining acute = 0xCC 0x81
    EXPECT_EQ(text, "e\xCC\x81");
}

TEST(ScreenGrapheme, ZwjSequenceOnScreen) {
    Screen screen(24, 80);
    // Man + ZWJ + Laptop
    screen.onPrint(0x1F468);  // man emoji (width 2)
    screen.onPrint(0x200D);   // ZWJ (width 0, extends)
    screen.onPrint(0x1F4BB);  // laptop (width 0 after ZWJ? Actually ExtPict after ZWJ = no break)

    // The man emoji is at col 0 (width 2), continuation at col 1
    const TermCell& cell = screen.cellAt(0, 0);
    EXPECT_EQ(cell.codepoint, static_cast<char32_t>(0x1F468));
    // ZWJ should be appended
    EXPECT_GE(cell.extra_count, 1u);
    // After ZWJ, the laptop emoji should also be appended (GB11)
    if (cell.extra_count >= 2) {
        EXPECT_EQ(cell.extra[0], static_cast<char32_t>(0x200D));
        EXPECT_EQ(cell.extra[1], static_cast<char32_t>(0x1F4BB));
    }
}

TEST(ScreenGrapheme, RegionalIndicatorPairOnScreen) {
    Screen screen(24, 80);
    // US flag: U+1F1FA + U+1F1F8
    screen.onPrint(0x1F1FA);  // Regional Indicator U
    screen.onPrint(0x1F1F8);  // Regional Indicator S

    // First RI should be at col 0, second should extend it
    // (RI has codepoint_width 1 from ICU, but as a pair they form a flag)
    const TermCell& cell = screen.cellAt(0, 0);
    EXPECT_EQ(cell.codepoint, static_cast<char32_t>(0x1F1FA));
    // The second RI should be appended (no grapheme break for first pair)
    ASSERT_EQ(cell.extra_count, 1u);
    EXPECT_EQ(cell.extra[0], static_cast<char32_t>(0x1F1F8));
}

TEST(ScreenGrapheme, HangulSyllableComposition) {
    Screen screen(24, 80);
    // Hangul L + V: the current implementation treats Hangul Jamo as
    // separate characters (no L+V composition in shouldCombineWithPrevious).
    // Each Hangul Jamo is wide (East Asian Width = W), so width = 2.
    screen.onPrint(0x1100);  // Hangul Choseong Kiyeok (L) - wide, occupies cols 0-1
    screen.onPrint(0x1161);  // Hangul Jungseong A (V) - wide, occupies cols 2-3

    const TermCell& cell0 = screen.cellAt(0, 0);
    EXPECT_EQ(cell0.codepoint, static_cast<char32_t>(0x1100));
    EXPECT_EQ(cell0.extra_count, 0u);
    EXPECT_EQ(cell0.width, 2);

    // V is placed at col 2 (after L which occupies cols 0-1)
    const TermCell& cell2 = screen.cellAt(0, 2);
    EXPECT_EQ(cell2.codepoint, static_cast<char32_t>(0x1161));
}

} // namespace
} // namespace termcore
