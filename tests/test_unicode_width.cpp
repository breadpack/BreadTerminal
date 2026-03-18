#include <gtest/gtest.h>
#include "termcore/font/unicode_width.h"
#include <string>

namespace termcore {
namespace {

// ---------------------------------------------------------------------------
// codepoint_width tests
// ---------------------------------------------------------------------------

TEST(CodepointWidth, AsciiPrintable) {
    EXPECT_EQ(codepoint_width(U'A'), 1);
    EXPECT_EQ(codepoint_width(U'z'), 1);
    EXPECT_EQ(codepoint_width(U' '), 1);
    EXPECT_EQ(codepoint_width(U'~'), 1);
}

TEST(CodepointWidth, ControlCharsZero) {
    EXPECT_EQ(codepoint_width(0x00), 0);  // NUL
    EXPECT_EQ(codepoint_width(0x1F), 0);  // US
    EXPECT_EQ(codepoint_width(0x7F), 0);  // DEL
    EXPECT_EQ(codepoint_width(0x9F), 0);  // APC
}

TEST(CodepointWidth, CjkIdeographsWide) {
    EXPECT_EQ(codepoint_width(U'\uD55C'), 2);  // 한
    EXPECT_EQ(codepoint_width(U'\u6F22'), 2);  // 漢
    EXPECT_EQ(codepoint_width(U'\u4E00'), 2);  // 一
    EXPECT_EQ(codepoint_width(U'\u9FFF'), 2);  // last CJK Unified
}

TEST(CodepointWidth, FullwidthFormsWide) {
    EXPECT_EQ(codepoint_width(U'\uFF21'), 2);  // Ａ fullwidth A
    EXPECT_EQ(codepoint_width(U'\uFF01'), 2);  // ！ fullwidth exclamation
}

TEST(CodepointWidth, HalfwidthKatakanaNarrow) {
    EXPECT_EQ(codepoint_width(U'\uFF66'), 1);  // ｦ halfwidth katakana
    EXPECT_EQ(codepoint_width(U'\uFF71'), 1);  // ｱ halfwidth katakana A
}

TEST(CodepointWidth, ZwjZeroWidth) {
    EXPECT_EQ(codepoint_width(0x200D), 0);  // ZWJ
    EXPECT_EQ(codepoint_width(0x200C), 0);  // ZWNJ
    EXPECT_EQ(codepoint_width(0x200B), 0);  // ZWSP
}

TEST(CodepointWidth, CombiningMarksZero) {
    EXPECT_EQ(codepoint_width(0x0301), 0);  // COMBINING ACUTE ACCENT
    EXPECT_EQ(codepoint_width(0x0300), 0);  // COMBINING GRAVE ACCENT
    EXPECT_EQ(codepoint_width(0x20E3), 0);  // COMBINING ENCLOSING KEYCAP
}

TEST(CodepointWidth, EmojiHeart) {
    // U+2764 HEAVY BLACK HEART — not East Asian Wide/Fullwidth
    // Typically width 1 per East Asian Width property (neutral)
    int w = codepoint_width(0x2764);
    EXPECT_GE(w, 1);
    EXPECT_LE(w, 2);
}

// ---------------------------------------------------------------------------
// is_zero_width tests
// ---------------------------------------------------------------------------

TEST(IsZeroWidth, ZeroWidthChars) {
    EXPECT_TRUE(is_zero_width(0x200B));   // ZWSP
    EXPECT_TRUE(is_zero_width(0x200C));   // ZWNJ
    EXPECT_TRUE(is_zero_width(0x200D));   // ZWJ
    EXPECT_TRUE(is_zero_width(0xFEFF));   // BOM
    EXPECT_TRUE(is_zero_width(0x0301));   // combining accent
    EXPECT_TRUE(is_zero_width(0x00AD));   // soft hyphen
}

TEST(IsZeroWidth, NormalCharsNotZeroWidth) {
    EXPECT_FALSE(is_zero_width(U'A'));
    EXPECT_FALSE(is_zero_width(U'\uD55C'));  // 한
    EXPECT_FALSE(is_zero_width(U' '));
}

// ---------------------------------------------------------------------------
// string_display_width tests
// ---------------------------------------------------------------------------

TEST(StringDisplayWidth, AsciiString) {
    EXPECT_EQ(string_display_width("Hello"), 5);
    EXPECT_EQ(string_display_width(""), 0);
    EXPECT_EQ(string_display_width("abc"), 3);
}

TEST(StringDisplayWidth, KoreanString) {
    // "한글" = 2 CJK chars, each width 2
    EXPECT_EQ(string_display_width("\xED\x95\x9C\xEA\xB8\x80"), 4);
}

TEST(StringDisplayWidth, MixedAsciiCjk) {
    // "A한B" = 1 + 2 + 1 = 4
    EXPECT_EQ(string_display_width("A\xED\x95\x9C""B"), 4);
}

TEST(StringDisplayWidth, EmptyString) {
    EXPECT_EQ(string_display_width(""), 0);
}

TEST(StringDisplayWidth, FullwidthForms) {
    // "Ａ" (U+FF21) is fullwidth, width 2
    EXPECT_EQ(string_display_width("\xEF\xBC\xA1"), 2);
}

// ---------------------------------------------------------------------------
// utf8_decode / utf8_encode roundtrip
// ---------------------------------------------------------------------------

TEST(Utf8Codec, AsciiRoundtrip) {
    std::string encoded;
    utf8_encode(U'A', encoded);
    EXPECT_EQ(encoded, "A");

    size_t pos = 0;
    char32_t decoded = utf8_decode(encoded.data(), encoded.size(), pos);
    EXPECT_EQ(decoded, U'A');
    EXPECT_EQ(pos, 1u);
}

TEST(Utf8Codec, TwoByteRoundtrip) {
    // U+00E9 (é) — 2-byte UTF-8
    std::string encoded;
    utf8_encode(0x00E9, encoded);
    EXPECT_EQ(encoded.size(), 2u);

    size_t pos = 0;
    char32_t decoded = utf8_decode(encoded.data(), encoded.size(), pos);
    EXPECT_EQ(decoded, static_cast<char32_t>(0x00E9));
}

TEST(Utf8Codec, ThreeByteRoundtrip) {
    // U+D55C (한) — 3-byte UTF-8
    std::string encoded;
    utf8_encode(0xD55C, encoded);
    EXPECT_EQ(encoded.size(), 3u);

    size_t pos = 0;
    char32_t decoded = utf8_decode(encoded.data(), encoded.size(), pos);
    EXPECT_EQ(decoded, static_cast<char32_t>(0xD55C));
}

TEST(Utf8Codec, FourByteRoundtrip) {
    // U+1F600 (grinning face) — 4-byte UTF-8
    std::string encoded;
    utf8_encode(0x1F600, encoded);
    EXPECT_EQ(encoded.size(), 4u);

    size_t pos = 0;
    char32_t decoded = utf8_decode(encoded.data(), encoded.size(), pos);
    EXPECT_EQ(decoded, static_cast<char32_t>(0x1F600));
}

TEST(Utf8Codec, InvalidSequenceReturnsReplacement) {
    // Invalid continuation byte
    const char bad[] = "\xFF\x80";
    size_t pos = 0;
    char32_t cp = utf8_decode(bad, 2, pos);
    EXPECT_EQ(cp, static_cast<char32_t>(0xFFFD));
}

TEST(Utf8Codec, MultipleCodepoints) {
    // Encode "Aé한" and decode all
    std::string encoded;
    utf8_encode(U'A', encoded);
    utf8_encode(0x00E9, encoded);
    utf8_encode(0xD55C, encoded);

    size_t pos = 0;
    EXPECT_EQ(utf8_decode(encoded.data(), encoded.size(), pos), U'A');
    EXPECT_EQ(utf8_decode(encoded.data(), encoded.size(), pos),
              static_cast<char32_t>(0x00E9));
    EXPECT_EQ(utf8_decode(encoded.data(), encoded.size(), pos),
              static_cast<char32_t>(0xD55C));
    EXPECT_EQ(pos, encoded.size());
}

// ---------------------------------------------------------------------------
// split_graphemes tests
// ---------------------------------------------------------------------------

TEST(SplitGraphemes, BasicAscii) {
    auto clusters = split_graphemes("abc");
    ASSERT_EQ(clusters.size(), 3u);
    EXPECT_EQ(clusters[0].codepoints, std::u32string{U'a'});
    EXPECT_EQ(clusters[0].display_width, 1);
    EXPECT_EQ(clusters[1].codepoints, std::u32string{U'b'});
    EXPECT_EQ(clusters[2].codepoints, std::u32string{U'c'});
}

TEST(SplitGraphemes, CombiningAccent) {
    // "e" + COMBINING ACUTE ACCENT = 1 cluster
    std::string input;
    input += 'e';
    // U+0301 combining acute accent: 0xCC 0x81
    input += "\xCC\x81";

    auto clusters = split_graphemes(input);
    ASSERT_EQ(clusters.size(), 1u);
    ASSERT_EQ(clusters[0].codepoints.size(), 2u);
    EXPECT_EQ(clusters[0].codepoints[0], U'e');
    EXPECT_EQ(clusters[0].codepoints[1], static_cast<char32_t>(0x0301));
    EXPECT_EQ(clusters[0].display_width, 1);
}

TEST(SplitGraphemes, CjkCharactersWidthTwo) {
    // "한글" = 2 clusters, each width 2
    auto clusters = split_graphemes("\xED\x95\x9C\xEA\xB8\x80");
    ASSERT_EQ(clusters.size(), 2u);
    EXPECT_EQ(clusters[0].display_width, 2);
    EXPECT_EQ(clusters[1].display_width, 2);
}

TEST(SplitGraphemes, VariationSelector) {
    // U+2764 + U+FE0F (heart + variation selector 16) = 1 cluster
    std::string input;
    // U+2764 = E2 9D A4
    input += "\xE2\x9D\xA4";
    // U+FE0F = EF B8 8F
    input += "\xEF\xB8\x8F";

    auto clusters = split_graphemes(input);
    ASSERT_EQ(clusters.size(), 1u);
    ASSERT_EQ(clusters[0].codepoints.size(), 2u);
    EXPECT_EQ(clusters[0].codepoints[0], static_cast<char32_t>(0x2764));
    EXPECT_EQ(clusters[0].codepoints[1], static_cast<char32_t>(0xFE0F));
}

TEST(SplitGraphemes, RegionalIndicatorPair) {
    // U+1F1FA U+1F1F8 (flag: US) = 1 cluster
    std::string input;
    // U+1F1FA = F0 9F 87 BA
    input += "\xF0\x9F\x87\xBA";
    // U+1F1F8 = F0 9F 87 B8
    input += "\xF0\x9F\x87\xB8";

    auto clusters = split_graphemes(input);
    ASSERT_EQ(clusters.size(), 1u);
    ASSERT_EQ(clusters[0].codepoints.size(), 2u);
    EXPECT_EQ(clusters[0].display_width, 2);
}

TEST(SplitGraphemes, EmptyString) {
    auto clusters = split_graphemes("");
    EXPECT_TRUE(clusters.empty());
}

TEST(SplitGraphemes, ZwjSequence) {
    // Simple ZWJ sequence: person + ZWJ + laptop
    // U+1F468 ZWJ U+1F4BB
    std::string input;
    input += "\xF0\x9F\x91\xA8";  // U+1F468 MAN
    input += "\xE2\x80\x8D";      // U+200D ZWJ
    input += "\xF0\x9F\x92\xBB";  // U+1F4BB LAPTOP

    auto clusters = split_graphemes(input);
    ASSERT_EQ(clusters.size(), 1u);
    ASSERT_EQ(clusters[0].codepoints.size(), 3u);
}

} // namespace
} // namespace termcore
