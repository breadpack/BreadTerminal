#include <gtest/gtest.h>
#include "termcore/font/font_shaper.h"
#include <fstream>
#include <string>

namespace termcore {
namespace {

// Try multiple font paths for cross-platform compatibility
static std::string findTestFont() {
    const char* candidates[] = {
        "/System/Library/Fonts/Menlo.ttc",            // macOS
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",  // Ubuntu/Debian
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",   // Arch
        "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf", // Fedora
    };
    for (auto* p : candidates) {
        std::ifstream f(p);
        if (f.good()) return p;
    }
    return "";
}

static const std::string kTestFontPath = findTestFont();
static const int kTestFaceIndex = 0;
static const float kTestFontSize = 14.0f;

class FontShaperTest : public ::testing::Test {
protected:
    FontShaper shaper;
    void SetUp() override {
        if (kTestFontPath.empty()) GTEST_SKIP() << "No test font found";
    }
};

TEST_F(FontShaperTest, LoadValidFont) {
    FontFaceId id = shaper.loadFont(kTestFontPath.c_str(), kTestFaceIndex, kTestFontSize);
    EXPECT_NE(id, kInvalidFontFace);
}

TEST_F(FontShaperTest, LoadNonexistentFont) {
    FontFaceId id = shaper.loadFont("/nonexistent/path/font.ttf", 0, kTestFontSize);
    EXPECT_EQ(id, kInvalidFontFace);
}

TEST_F(FontShaperTest, ShapeHelloProducesFiveGlyphs) {
    FontFaceId id = shaper.loadFont(kTestFontPath, kTestFaceIndex, kTestFontSize);
    ASSERT_NE(id, kInvalidFontFace);

    std::u32string text = U"Hello";
    auto glyphs = shaper.shape(id, text);

    EXPECT_EQ(glyphs.size(), 5u);
}

TEST_F(FontShaperTest, ShapeAsciiClustersMatchIndices) {
    FontFaceId id = shaper.loadFont(kTestFontPath, kTestFaceIndex, kTestFontSize);
    ASSERT_NE(id, kInvalidFontFace);

    std::u32string text = U"abcde";
    auto glyphs = shaper.shape(id, text);

    ASSERT_EQ(glyphs.size(), 5u);
    for (size_t i = 0; i < glyphs.size(); ++i) {
        EXPECT_EQ(glyphs[i].cluster, static_cast<uint32_t>(i))
            << "Cluster mismatch at glyph " << i;
    }
}

TEST_F(FontShaperTest, GetGlyphIndexForA) {
    FontFaceId id = shaper.loadFont(kTestFontPath, kTestFaceIndex, kTestFontSize);
    ASSERT_NE(id, kInvalidFontFace);

    uint32_t glyph_id = shaper.getGlyphIndex(id, U'A');
    EXPECT_NE(glyph_id, 0u);
}

TEST_F(FontShaperTest, HasGlyphForA) {
    FontFaceId id = shaper.loadFont(kTestFontPath, kTestFaceIndex, kTestFontSize);
    ASSERT_NE(id, kInvalidFontFace);

    EXPECT_TRUE(shaper.hasGlyph(id, U'A'));
}

TEST_F(FontShaperTest, HasGlyphObscureCodepoint) {
    FontFaceId id = shaper.loadFont(kTestFontPath, kTestFaceIndex, kTestFontSize);
    ASSERT_NE(id, kInvalidFontFace);

    // Private Use Area codepoint - unlikely to have a glyph
    // (This may or may not be false depending on font, but should not crash)
    shaper.hasGlyph(id, U'\U000F0000');
}

TEST_F(FontShaperTest, ShapeForGridHello) {
    FontFaceId id = shaper.loadFont(kTestFontPath, kTestFaceIndex, kTestFontSize);
    ASSERT_NE(id, kInvalidFontFace);

    std::u32string text = U"Hello";
    auto runs = shaper.shapeForGrid(id, text, 8.0f);

    // Count total cells covered
    int total_cells = 0;
    int total_glyphs = 0;
    for (const auto& run : runs) {
        total_cells += run.cell_count;
        total_glyphs += static_cast<int>(run.glyphs.size());
    }
    EXPECT_EQ(total_cells, 5);
    EXPECT_EQ(total_glyphs, 5);
}

TEST_F(FontShaperTest, DisableLigaturesNoCrash) {
    FontFaceId id = shaper.loadFont(kTestFontPath, kTestFaceIndex, kTestFontSize);
    ASSERT_NE(id, kInvalidFontFace);

    ShaperConfig config;
    config.enable_ligatures = false;
    config.enable_liga = false;

    std::u32string text = U"Hello";
    auto glyphs = shaper.shape(id, text, config);

    // For non-ligature text, should still produce 5 glyphs
    EXPECT_EQ(glyphs.size(), 5u);
}

TEST_F(FontShaperTest, ShapeEmptyString) {
    FontFaceId id = shaper.loadFont(kTestFontPath, kTestFaceIndex, kTestFontSize);
    ASSERT_NE(id, kInvalidFontFace);

    std::u32string text;
    auto glyphs = shaper.shape(id, text);
    EXPECT_TRUE(glyphs.empty());
}

TEST_F(FontShaperTest, ShapeForGridEmptyString) {
    FontFaceId id = shaper.loadFont(kTestFontPath, kTestFaceIndex, kTestFontSize);
    ASSERT_NE(id, kInvalidFontFace);

    std::u32string text;
    auto runs = shaper.shapeForGrid(id, text, 8.0f);
    EXPECT_TRUE(runs.empty());
}

TEST_F(FontShaperTest, SetFontSizeDoesNotCrash) {
    FontFaceId id = shaper.loadFont(kTestFontPath, kTestFaceIndex, kTestFontSize);
    ASSERT_NE(id, kInvalidFontFace);

    shaper.setFontSize(id, 20.0f);

    std::u32string text = U"Test";
    auto glyphs = shaper.shape(id, text);
    EXPECT_EQ(glyphs.size(), 4u);
}

TEST_F(FontShaperTest, GlyphsHaveNonZeroAdvance) {
    FontFaceId id = shaper.loadFont(kTestFontPath, kTestFaceIndex, kTestFontSize);
    ASSERT_NE(id, kInvalidFontFace);

    std::u32string text = U"A";
    auto glyphs = shaper.shape(id, text);

    ASSERT_EQ(glyphs.size(), 1u);
    EXPECT_GT(glyphs[0].x_advance, 0);
}

TEST_F(FontShaperTest, ShapeForGridCjkWideChars) {
    FontFaceId id = shaper.loadFont(kTestFontPath, kTestFaceIndex, kTestFontSize);
    ASSERT_NE(id, kInvalidFontFace);

    // CJK character that occupies 2 cells
    std::u32string text = U"\u4E00";  // '一'
    auto runs = shaper.shapeForGrid(id, text, 8.0f);

    int total_cells = 0;
    for (const auto& run : runs) {
        total_cells += run.cell_count;
    }
    EXPECT_EQ(total_cells, 2);
}

TEST_F(FontShaperTest, InvalidFaceIdReturnsEmpty) {
    std::u32string text = U"Hello";
    auto glyphs = shaper.shape(999, text);
    EXPECT_TRUE(glyphs.empty());

    EXPECT_EQ(shaper.getGlyphIndex(999, U'A'), 0u);
    EXPECT_FALSE(shaper.hasGlyph(999, U'A'));
}

} // namespace
} // namespace termcore
