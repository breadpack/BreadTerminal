#include <gtest/gtest.h>
#include "termcore/font/ligature.h"
#include "termcore/config.h"
#include <fstream>
#include <string>
#include <vector>

namespace termcore {
namespace {

// ============================================================
// LigatureDetector::detectLigatures tests (no font required)
// ============================================================

class LigatureDetectorTest : public ::testing::Test {
protected:
    LigatureDetector detector;
};

// --- Common programming ligature detection ---

TEST_F(LigatureDetectorTest, DetectsNotEqual) {
    std::vector<uint32_t> cps = { '!', '=' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].start_col, 0);
    EXPECT_EQ(spans[0].end_col, 2);
    EXPECT_EQ(spans[0].codepoints.size(), 2u);
}

TEST_F(LigatureDetectorTest, DetectsStrictNotEqual) {
    std::vector<uint32_t> cps = { '!', '=', '=' };
    auto spans = detector.detectLigatures(cps, 0);
    // Should match !== (3-char) rather than != + =
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].start_col, 0);
    EXPECT_EQ(spans[0].end_col, 3);
    EXPECT_EQ(spans[0].codepoints.size(), 3u);
}

TEST_F(LigatureDetectorTest, DetectsFatArrow) {
    std::vector<uint32_t> cps = { '=', '>' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].start_col, 0);
    EXPECT_EQ(spans[0].end_col, 2);
}

TEST_F(LigatureDetectorTest, DetectsThinArrow) {
    std::vector<uint32_t> cps = { '-', '>' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].start_col, 0);
    EXPECT_EQ(spans[0].end_col, 2);
}

TEST_F(LigatureDetectorTest, DetectsLongArrow) {
    std::vector<uint32_t> cps = { '-', '-', '>' };
    auto spans = detector.detectLigatures(cps, 0);
    // Should match --> (3-char) not -- + >
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].end_col, 3);
}

TEST_F(LigatureDetectorTest, DetectsLeftArrow) {
    std::vector<uint32_t> cps = { '<', '-' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].start_col, 0);
    EXPECT_EQ(spans[0].end_col, 2);
}

TEST_F(LigatureDetectorTest, DetectsLongLeftArrow) {
    std::vector<uint32_t> cps = { '<', '-', '-' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].end_col, 3);
}

TEST_F(LigatureDetectorTest, DetectsTripleEquals) {
    std::vector<uint32_t> cps = { '=', '=', '=' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].end_col, 3);
}

TEST_F(LigatureDetectorTest, DetectsDoubleEquals) {
    std::vector<uint32_t> cps = { '=', '=' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].end_col, 2);
}

TEST_F(LigatureDetectorTest, DetectsScope) {
    std::vector<uint32_t> cps = { ':', ':' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
}

TEST_F(LigatureDetectorTest, DetectsEllipsis) {
    std::vector<uint32_t> cps = { '.', '.', '.' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].end_col, 3);
}

TEST_F(LigatureDetectorTest, DetectsLogicalAnd) {
    std::vector<uint32_t> cps = { '&', '&' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
}

TEST_F(LigatureDetectorTest, DetectsLogicalOr) {
    std::vector<uint32_t> cps = { '|', '|' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
}

TEST_F(LigatureDetectorTest, DetectsPipeForward) {
    std::vector<uint32_t> cps = { '|', '>' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
}

TEST_F(LigatureDetectorTest, DetectsPipeBackward) {
    std::vector<uint32_t> cps = { '<', '|' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
}

TEST_F(LigatureDetectorTest, DetectsShiftOperators) {
    std::vector<uint32_t> cps = { '>', '>' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);

    cps = { '<', '<' };
    spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
}

TEST_F(LigatureDetectorTest, DetectsIncrementDecrement) {
    std::vector<uint32_t> cps = { '+', '+' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);

    cps = { '-', '-' };
    spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
}

TEST_F(LigatureDetectorTest, DetectsCommentDelimiters) {
    std::vector<uint32_t> cps = { '/', '*' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);

    cps = { '*', '/' };
    spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);

    cps = { '/', '/' };
    spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
}

TEST_F(LigatureDetectorTest, DetectsDocComment) {
    std::vector<uint32_t> cps = { '/', '*', '*' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].end_col, 3);
}

TEST_F(LigatureDetectorTest, DetectsJsxClose) {
    std::vector<uint32_t> cps = { '<', '/', '>' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].end_col, 3);
}

TEST_F(LigatureDetectorTest, DetectsTildeSequences) {
    std::vector<uint32_t> cps = { '~', '~' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);

    cps = { '~', '>' };
    spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
}

TEST_F(LigatureDetectorTest, DetectsWww) {
    std::vector<uint32_t> cps = { 'w', 'w', 'w' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].end_col, 3);
}

TEST_F(LigatureDetectorTest, DetectsGreaterEqual) {
    std::vector<uint32_t> cps = { '>', '=' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
}

TEST_F(LigatureDetectorTest, DetectsLessEqual) {
    std::vector<uint32_t> cps = { '<', '=' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
}

// --- Non-ligature text produces no spans ---

TEST_F(LigatureDetectorTest, NoSpansForPlainText) {
    std::vector<uint32_t> cps = { 'H', 'e', 'l', 'l', 'o' };
    auto spans = detector.detectLigatures(cps, 0);
    EXPECT_TRUE(spans.empty());
}

TEST_F(LigatureDetectorTest, NoSpansForSingleChars) {
    std::vector<uint32_t> cps = { '!' };
    auto spans = detector.detectLigatures(cps, 0);
    EXPECT_TRUE(spans.empty());
}

TEST_F(LigatureDetectorTest, NoSpansForEmptyInput) {
    std::vector<uint32_t> cps;
    auto spans = detector.detectLigatures(cps, 0);
    EXPECT_TRUE(spans.empty());
}

TEST_F(LigatureDetectorTest, NoSpansForDigits) {
    std::vector<uint32_t> cps = { '1', '2', '3', '4', '5' };
    auto spans = detector.detectLigatures(cps, 0);
    EXPECT_TRUE(spans.empty());
}

TEST_F(LigatureDetectorTest, NoSpansForMixedNonLigature) {
    // Characters that individually appear in ligatures but don't form one together
    std::vector<uint32_t> cps = { '!', '+', '<', '.' };
    auto spans = detector.detectLigatures(cps, 0);
    EXPECT_TRUE(spans.empty());
}

// --- Row offset handling ---

TEST_F(LigatureDetectorTest, RowStartColOffset) {
    std::vector<uint32_t> cps = { '=', '>' };
    auto spans = detector.detectLigatures(cps, 10);
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].start_col, 10);
    EXPECT_EQ(spans[0].end_col, 12);
}

// --- Multiple ligatures in one row ---

TEST_F(LigatureDetectorTest, MultipleLigaturesInRow) {
    // "if (x != y && z == w)"
    // Contains: !=, &&, ==
    std::vector<uint32_t> cps = {
        'i', 'f', ' ', '(', 'x', ' ', '!', '=', ' ',
        'y', ' ', '&', '&', ' ', 'z', ' ', '=', '=', ' ', 'w', ')'
    };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 3u);

    EXPECT_EQ(spans[0].start_col, 6);   // !=
    EXPECT_EQ(spans[0].end_col, 8);

    EXPECT_EQ(spans[1].start_col, 11);  // &&
    EXPECT_EQ(spans[1].end_col, 13);

    EXPECT_EQ(spans[2].start_col, 16);  // ==
    EXPECT_EQ(spans[2].end_col, 18);
}

TEST_F(LigatureDetectorTest, AdjacentLigatures) {
    // "=>->" — two ligatures back to back
    std::vector<uint32_t> cps = { '=', '>', '-', '>' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 2u);
    EXPECT_EQ(spans[0].start_col, 0);  // =>
    EXPECT_EQ(spans[0].end_col, 2);
    EXPECT_EQ(spans[1].start_col, 2);  // ->
    EXPECT_EQ(spans[1].end_col, 4);
}

// --- Greedy longest-match ---

TEST_F(LigatureDetectorTest, LongestMatchPrefersThreeChar) {
    // "===" should be one 3-char span, not "==" + "="
    std::vector<uint32_t> cps = { '=', '=', '=' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].codepoints.size(), 3u);
}

TEST_F(LigatureDetectorTest, LongestMatchForNotEqualStrict) {
    // "!==" should be one 3-char span
    std::vector<uint32_t> cps = { '!', '=', '=' };
    auto spans = detector.detectLigatures(cps, 0);
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].codepoints.size(), 3u);
}

// --- Config enable/disable ---

TEST_F(LigatureDetectorTest, ConfigEnableLigatures) {
    Config config;
    EXPECT_TRUE(config.font_ligatures);  // Default is true

    config.font_ligatures = false;
    EXPECT_FALSE(config.font_ligatures);
}

// ============================================================
// Font-dependent tests (skipped when no test font is available)
// ============================================================

static std::string findTestFont() {
    const char* candidates[] = {
        "/System/Library/Fonts/Menlo.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf",
        "C:/Windows/Fonts/consola.ttf",
    };
    for (auto* p : candidates) {
        std::ifstream f(p);
        if (f.good()) return p;
    }
    return "";
}

class LigatureShaperTest : public ::testing::Test {
protected:
    FontShaper shaper;
    FontFaceId face_id = kInvalidFontFace;
    LigatureDetector detector;

    void SetUp() override {
        std::string font_path = findTestFont();
        if (font_path.empty()) {
            GTEST_SKIP() << "No test font found";
        }
        face_id = shaper.loadFont(font_path, 0, 14.0f);
        if (face_id == kInvalidFontFace) {
            GTEST_SKIP() << "Failed to load test font";
        }
    }
};

TEST_F(LigatureShaperTest, ShapeLigatureProducesGlyphs) {
    LigatureSpan span;
    span.start_col = 0;
    span.end_col = 2;
    span.codepoints = { '=', '>' };

    auto result = LigatureDetector::shapeLigature(shaper, face_id, span);
    EXPECT_FALSE(result.glyphs.empty());
    EXPECT_EQ(result.cell_count, 2);
}

TEST_F(LigatureShaperTest, ShapeLigatureEmptySpan) {
    LigatureSpan span;
    span.start_col = 0;
    span.end_col = 0;

    auto result = LigatureDetector::shapeLigature(shaper, face_id, span);
    EXPECT_TRUE(result.glyphs.empty());
    EXPECT_EQ(result.cell_count, 0);
}

TEST_F(LigatureShaperTest, ShapeLigatureWithDisabledFeatures) {
    LigatureSpan span;
    span.start_col = 0;
    span.end_col = 2;
    span.codepoints = { '!', '=' };

    ShaperConfig config;
    config.enable_ligatures = false;
    config.enable_liga = false;

    auto result = LigatureDetector::shapeLigature(shaper, face_id, span, config);
    // Should still produce glyphs (just not ligated)
    EXPECT_FALSE(result.glyphs.empty());
    EXPECT_EQ(result.cell_count, 2);
}

TEST_F(LigatureShaperTest, HasLigatureSupportDoesNotCrash) {
    // This test just verifies it doesn't crash. Whether the font actually
    // has ligatures depends on the test font available.
    bool has_lig = LigatureDetector::hasLigatureSupport(shaper, face_id);
    (void)has_lig;  // Result depends on font
}

TEST_F(LigatureShaperTest, ShapeThreeCharLigature) {
    LigatureSpan span;
    span.start_col = 0;
    span.end_col = 3;
    span.codepoints = { '=', '=', '=' };

    auto result = LigatureDetector::shapeLigature(shaper, face_id, span);
    EXPECT_FALSE(result.glyphs.empty());
    EXPECT_EQ(result.cell_count, 3);
}

} // namespace
} // namespace termcore
