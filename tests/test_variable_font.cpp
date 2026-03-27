#include <gtest/gtest.h>
#include "termcore/font/variable_font.h"
#include <cstdio>
#include <filesystem>

namespace {

// Helper: find a system variable font for testing
std::string findVariableFont() {
    const char* candidates[] = {
        "C:\\Windows\\Fonts\\bahnschrift.ttf",        // Windows (Bahnschrift)
        "C:\\Windows\\Fonts\\CascadiaCode.ttf",       // Windows (Cascadia Code)
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/SFNSText.ttf",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    };
    for (auto* path : candidates) {
        if (std::filesystem::exists(path)) {
            auto axes = termcore::queryAxes(path);
            if (!axes.empty()) return path;
        }
    }
    return {};
}

// Helper: find a system non-variable font for testing
std::string findNonVariableFont() {
    const char* candidates[] = {
        "C:\\Windows\\Fonts\\consola.ttf",            // Windows (Consolas)
        "C:\\Windows\\Fonts\\cour.ttf",               // Windows (Courier New)
        "/System/Library/Fonts/Courier.dfont",
        "/System/Library/Fonts/Supplemental/Courier New.ttf",
        "/System/Library/Fonts/Monaco.ttf",
        "/System/Library/Fonts/Menlo.ttc",
    };
    for (auto* path : candidates) {
        if (std::filesystem::exists(path)) {
            auto axes = termcore::queryAxes(path);
            if (axes.empty()) return path;
        }
    }
    return {};
}

// Test 1: parseAxisTag "wght" returns correct tag value
TEST(VariableFont, ParseAxisTagWght) {
    uint32_t tag = termcore::parseAxisTag("wght");
    EXPECT_EQ(tag, termcore::AxisTag::Weight);
    EXPECT_EQ(tag, 0x77676874u);
}

// Test 2: axisTagToString roundtrip
TEST(VariableFont, AxisTagRoundtrip) {
    uint32_t tag = termcore::parseAxisTag("wdth");
    std::string str = termcore::axisTagToString(tag);
    EXPECT_EQ(str, "wdth");

    uint32_t tag2 = termcore::parseAxisTag(str);
    EXPECT_EQ(tag, tag2);
}

// Test 3: parseVariations single "wght=700"
TEST(VariableFont, ParseVariationsSingle) {
    auto vars = termcore::parseVariations("wght=700");
    ASSERT_EQ(vars.size(), 1u);
    EXPECT_EQ(vars[0].tag, termcore::AxisTag::Weight);
    EXPECT_FLOAT_EQ(vars[0].value, 700.0f);
}

// Test 4: parseVariations multiple "wght=700,wdth=75"
TEST(VariableFont, ParseVariationsMultiple) {
    auto vars = termcore::parseVariations("wght=700,wdth=75");
    ASSERT_EQ(vars.size(), 2u);
    EXPECT_EQ(vars[0].tag, termcore::AxisTag::Weight);
    EXPECT_FLOAT_EQ(vars[0].value, 700.0f);
    EXPECT_EQ(vars[1].tag, termcore::AxisTag::Width);
    EXPECT_FLOAT_EQ(vars[1].value, 75.0f);
}

// Test 5: parseVariations empty string
TEST(VariableFont, ParseVariationsEmpty) {
    auto vars = termcore::parseVariations("");
    EXPECT_TRUE(vars.empty());
}

// Test 6: parseVariations invalid input
TEST(VariableFont, ParseVariationsInvalid) {
    auto vars = termcore::parseVariations("invalid");
    EXPECT_TRUE(vars.empty());

    vars = termcore::parseVariations("=100");
    EXPECT_TRUE(vars.empty());

    vars = termcore::parseVariations("ab=100");
    EXPECT_TRUE(vars.empty());
}

// Test 7: queryAxes on system variable font
TEST(VariableFont, QueryAxesVariableFont) {
    std::string path = findVariableFont();
    if (path.empty()) {
        GTEST_SKIP() << "No system variable font found";
    }
    auto axes = termcore::queryAxes(path);
    EXPECT_FALSE(axes.empty());
    // Variable fonts should have at least one axis with valid range
    for (const auto& axis : axes) {
        EXPECT_NE(axis.tag, 0u);
        EXPECT_LE(axis.min_value, axis.default_value);
        EXPECT_LE(axis.default_value, axis.max_value);
    }
}

// Test 8: queryAxes on non-variable font returns empty
TEST(VariableFont, QueryAxesNonVariableFont) {
    std::string path = findNonVariableFont();
    if (path.empty()) {
        GTEST_SKIP() << "No system non-variable font found";
    }
    auto axes = termcore::queryAxes(path);
    EXPECT_TRUE(axes.empty());
}

// Test 9: isVariableFont check
TEST(VariableFont, IsVariableFontCheck) {
    std::string var_path = findVariableFont();
    if (!var_path.empty()) {
        EXPECT_TRUE(termcore::isVariableFont(var_path));
    }

    std::string nonvar_path = findNonVariableFont();
    if (!nonvar_path.empty()) {
        EXPECT_FALSE(termcore::isVariableFont(nonvar_path));
    }

    if (var_path.empty() && nonvar_path.empty()) {
        GTEST_SKIP() << "No system fonts found for testing";
    }
}

// Test 10: applyVariations with null font returns false
TEST(VariableFont, ApplyVariationsNullFont) {
    std::vector<termcore::FontVariation> vars = {{termcore::AxisTag::Weight, 700.0f}};
    EXPECT_FALSE(termcore::applyVariations(nullptr, vars));
}

// Test 11: AxisTag constants have correct values
TEST(VariableFont, AxisTagConstants) {
    EXPECT_EQ(termcore::AxisTag::Weight, termcore::parseAxisTag("wght"));
    EXPECT_EQ(termcore::AxisTag::Width, termcore::parseAxisTag("wdth"));
    EXPECT_EQ(termcore::AxisTag::Slant, termcore::parseAxisTag("slnt"));
    EXPECT_EQ(termcore::AxisTag::OpticalSize, termcore::parseAxisTag("opsz"));
    EXPECT_EQ(termcore::AxisTag::Italic, termcore::parseAxisTag("ital"));
}

// Test 12: parseAxisTag with wrong length returns 0
TEST(VariableFont, ParseAxisTagWrongLength) {
    EXPECT_EQ(termcore::parseAxisTag(""), 0u);
    EXPECT_EQ(termcore::parseAxisTag("abc"), 0u);
    EXPECT_EQ(termcore::parseAxisTag("abcde"), 0u);
}

} // namespace
