#include <gtest/gtest.h>
#include "termcore/font/color_font.h"

namespace {

using termcore::ColorFontType;

// Test detectColorTablesFromFace with null returns None
TEST(ColorFont, DetectColorTablesFromFaceNull) {
    auto result = termcore::detectColorTablesFromFace(nullptr);
    EXPECT_EQ(result, ColorFontType::None);
}

// Test colorFontTypeName for None
TEST(ColorFont, TypeNameNone) {
    EXPECT_EQ(termcore::colorFontTypeName(ColorFontType::None), "None");
}

// Test colorFontTypeName for each type
TEST(ColorFont, TypeNameCOLRv0) {
    EXPECT_EQ(termcore::colorFontTypeName(ColorFontType::COLR_v0), "COLR v0");
}

TEST(ColorFont, TypeNameCOLRv1) {
    EXPECT_EQ(termcore::colorFontTypeName(ColorFontType::COLR_v1), "COLR v1");
}

TEST(ColorFont, TypeNameSBIX) {
    EXPECT_EQ(termcore::colorFontTypeName(ColorFontType::SBIX), "SBIX");
}

TEST(ColorFont, TypeNameCBDT) {
    EXPECT_EQ(termcore::colorFontTypeName(ColorFontType::CBDT), "CBDT");
}

TEST(ColorFont, TypeNameSVG) {
    EXPECT_EQ(termcore::colorFontTypeName(ColorFontType::SVG), "SVG");
}

// Test hasFlag utility
TEST(ColorFont, HasFlagSingle) {
    EXPECT_TRUE(termcore::hasFlag(ColorFontType::COLR_v0, ColorFontType::COLR_v0));
    EXPECT_FALSE(termcore::hasFlag(ColorFontType::COLR_v0, ColorFontType::SBIX));
    EXPECT_FALSE(termcore::hasFlag(ColorFontType::None, ColorFontType::COLR_v0));
}

// Test operator| combining types
TEST(ColorFont, BitwiseOrCombine) {
    auto combined = ColorFontType::COLR_v0 | ColorFontType::SBIX;
    EXPECT_TRUE(termcore::hasFlag(combined, ColorFontType::COLR_v0));
    EXPECT_TRUE(termcore::hasFlag(combined, ColorFontType::SBIX));
    EXPECT_FALSE(termcore::hasFlag(combined, ColorFontType::SVG));
}

// Test operator& masking
TEST(ColorFont, BitwiseAndMask) {
    auto combined = ColorFontType::COLR_v0 | ColorFontType::SBIX | ColorFontType::SVG;
    auto masked = combined & ColorFontType::SBIX;
    EXPECT_EQ(masked, ColorFontType::SBIX);

    auto masked2 = combined & ColorFontType::CBDT;
    EXPECT_EQ(masked2, ColorFontType::None);
}

// Test colorFontTypeName for combined types
TEST(ColorFont, TypeNameCombined) {
    auto combined = ColorFontType::COLR_v0 | ColorFontType::SVG;
    std::string name = termcore::colorFontTypeName(combined);
    EXPECT_NE(name.find("COLR v0"), std::string::npos);
    EXPECT_NE(name.find("SVG"), std::string::npos);
}

// Test isColorGlyph with null returns false
TEST(ColorFont, IsColorGlyphNull) {
    EXPECT_FALSE(termcore::isColorGlyph(nullptr, 0));
    EXPECT_FALSE(termcore::isColorGlyph(nullptr, 42));
}

// Test detectColorTables with non-existent file returns None
TEST(ColorFont, DetectColorTablesInvalidPath) {
    auto result = termcore::detectColorTables("/nonexistent/path/to/font.ttf");
    EXPECT_EQ(result, ColorFontType::None);
}

} // namespace
