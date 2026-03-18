#include <gtest/gtest.h>
#include "CoreTextRasterizer.h"

namespace termcore {
namespace {

class CoreTextRasterizerTest : public ::testing::Test {
protected:
    void SetUp() override {
        rasterizer_ = createCoreTextRasterizer();
    }

    std::unique_ptr<IFontRasterizer> rasterizer_;

    // Menlo.ttc is a standard macOS system font
    static constexpr const char* kMenloPath = "/System/Library/Fonts/Menlo.ttc";
    static constexpr float kTestSize = 14.0f;
};

TEST_F(CoreTextRasterizerTest, CreateRasterizer) {
    ASSERT_NE(rasterizer_, nullptr);
}

TEST_F(CoreTextRasterizerTest, LoadSystemFont) {
    FontFaceId face = rasterizer_->loadFont(kMenloPath, 0, kTestSize);
    EXPECT_NE(face, kInvalidFontFace);
}

TEST_F(CoreTextRasterizerTest, LoadNonexistentFont) {
    FontFaceId face = rasterizer_->loadFont("/nonexistent/path/font.ttf", 0, kTestSize);
    EXPECT_EQ(face, kInvalidFontFace);
}

TEST_F(CoreTextRasterizerTest, GetMetricsReasonableValues) {
    FontFaceId face = rasterizer_->loadFont(kMenloPath, 0, kTestSize);
    ASSERT_NE(face, kInvalidFontFace);

    FontMetrics metrics = rasterizer_->getMetrics(face, kTestSize);
    EXPECT_GT(metrics.cell_width, 0.0f);
    EXPECT_GT(metrics.cell_height, 0.0f);
    EXPECT_GT(metrics.ascent, 0.0f);
    EXPECT_GT(metrics.descent, 0.0f);
    EXPECT_NE(metrics.underline_position, 0.0f);
    EXPECT_GT(metrics.underline_thickness, 0.0f);
    EXPECT_GT(metrics.strikethrough_position, 0.0f);
    EXPECT_GT(metrics.strikethrough_thickness, 0.0f);
}

TEST_F(CoreTextRasterizerTest, GetGlyphIndexForA) {
    FontFaceId face = rasterizer_->loadFont(kMenloPath, 0, kTestSize);
    ASSERT_NE(face, kInvalidFontFace);

    uint32_t glyphIndex = rasterizer_->getGlyphIndex(face, U'A');
    EXPECT_NE(glyphIndex, 0u);
}

TEST_F(CoreTextRasterizerTest, GetGlyphIndexInvalidCodepoint) {
    FontFaceId face = rasterizer_->loadFont(kMenloPath, 0, kTestSize);
    ASSERT_NE(face, kInvalidFontFace);

    // Private use area codepoint unlikely to be in Menlo
    uint32_t glyphIndex = rasterizer_->getGlyphIndex(face, U'\U000FFFFF');
    EXPECT_EQ(glyphIndex, 0u);
}

TEST_F(CoreTextRasterizerTest, RasterizeGlyphA) {
    FontFaceId face = rasterizer_->loadFont(kMenloPath, 0, kTestSize);
    ASSERT_NE(face, kInvalidFontFace);

    uint32_t glyphIndex = rasterizer_->getGlyphIndex(face, U'A');
    ASSERT_NE(glyphIndex, 0u);

    SubpixelOffset offset{0, 0};
    RasterizedGlyph glyph = rasterizer_->rasterize(face, glyphIndex, kTestSize, offset);

    EXPECT_GT(glyph.width, 0);
    EXPECT_GT(glyph.height, 0);
    EXPECT_FALSE(glyph.bitmap.empty());
}

TEST_F(CoreTextRasterizerTest, RasterizedBitmapHasNonZeroPixels) {
    FontFaceId face = rasterizer_->loadFont(kMenloPath, 0, kTestSize);
    ASSERT_NE(face, kInvalidFontFace);

    uint32_t glyphIndex = rasterizer_->getGlyphIndex(face, U'A');
    ASSERT_NE(glyphIndex, 0u);

    SubpixelOffset offset{0, 0};
    RasterizedGlyph glyph = rasterizer_->rasterize(face, glyphIndex, kTestSize, offset);

    bool hasNonZero = false;
    for (uint8_t byte : glyph.bitmap) {
        if (byte != 0) {
            hasNonZero = true;
            break;
        }
    }
    EXPECT_TRUE(hasNonZero) << "Rasterized bitmap should contain non-zero pixels";
}

TEST_F(CoreTextRasterizerTest, IsColorGlyphForRegularLetter) {
    FontFaceId face = rasterizer_->loadFont(kMenloPath, 0, kTestSize);
    ASSERT_NE(face, kInvalidFontFace);

    uint32_t glyphIndex = rasterizer_->getGlyphIndex(face, U'A');
    ASSERT_NE(glyphIndex, 0u);

    EXPECT_FALSE(rasterizer_->isColorGlyph(face, glyphIndex));
}

TEST_F(CoreTextRasterizerTest, LoadTTCFontFaceIndex0) {
    FontFaceId face = rasterizer_->loadFont(kMenloPath, 0, kTestSize);
    EXPECT_NE(face, kInvalidFontFace);

    // Verify we can get metrics from it
    FontMetrics metrics = rasterizer_->getMetrics(face, kTestSize);
    EXPECT_GT(metrics.cell_width, 0.0f);
}

} // namespace
} // namespace termcore
