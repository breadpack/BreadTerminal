#include <gtest/gtest.h>
#include "termcore/iterm_image.h"
#include "termcore/screen.h"

using namespace termcore;

// --- parseITermImageOsc tests ---

TEST(ITermImageTest, ParseBasicParams) {
    ITermImageParams params;
    std::string payload;
    bool ok = parseITermImageOsc("inline=1:AAAA", params, payload);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(params.inline_display);
    EXPECT_EQ(payload, "AAAA");
}

TEST(ITermImageTest, ParseAllParams) {
    ITermImageParams params;
    std::string payload;
    // name=dGVzdA== is base64 for "test"
    bool ok = parseITermImageOsc(
        "name=dGVzdA==;size=1024;width=80;height=24;preserveAspectRatio=0;inline=1:AAAA",
        params, payload);
    EXPECT_TRUE(ok);
    EXPECT_EQ(params.name, "test");
    EXPECT_EQ(params.size, 1024);
    EXPECT_EQ(params.width.unit, ITermDimension::Unit::Cells);
    EXPECT_EQ(params.width.value, 80);
    EXPECT_EQ(params.height.unit, ITermDimension::Unit::Cells);
    EXPECT_EQ(params.height.value, 24);
    EXPECT_FALSE(params.preserve_aspect_ratio);
    EXPECT_TRUE(params.inline_display);
    EXPECT_EQ(payload, "AAAA");
}

TEST(ITermImageTest, ParsePixelDimensions) {
    ITermImageParams params;
    std::string payload;
    bool ok = parseITermImageOsc("width=100px;height=200px;inline=1:AA", params, payload);
    EXPECT_TRUE(ok);
    EXPECT_EQ(params.width.unit, ITermDimension::Unit::Pixels);
    EXPECT_EQ(params.width.value, 100);
    EXPECT_EQ(params.height.unit, ITermDimension::Unit::Pixels);
    EXPECT_EQ(params.height.value, 200);
}

TEST(ITermImageTest, ParsePercentDimensions) {
    ITermImageParams params;
    std::string payload;
    bool ok = parseITermImageOsc("width=50%;height=75%;inline=1:AA", params, payload);
    EXPECT_TRUE(ok);
    EXPECT_EQ(params.width.unit, ITermDimension::Unit::Percent);
    EXPECT_EQ(params.width.value, 50);
    EXPECT_EQ(params.height.unit, ITermDimension::Unit::Percent);
    EXPECT_EQ(params.height.value, 75);
}

TEST(ITermImageTest, ParseAutoDimensions) {
    ITermImageParams params;
    std::string payload;
    bool ok = parseITermImageOsc("width=auto;height=auto;inline=1:AA", params, payload);
    EXPECT_TRUE(ok);
    EXPECT_EQ(params.width.unit, ITermDimension::Unit::Auto);
    EXPECT_EQ(params.height.unit, ITermDimension::Unit::Auto);
}

TEST(ITermImageTest, ParseDefaultsToAutoAndPreserveAspect) {
    ITermImageParams params;
    std::string payload;
    bool ok = parseITermImageOsc("inline=1:AA", params, payload);
    EXPECT_TRUE(ok);
    EXPECT_EQ(params.width.unit, ITermDimension::Unit::Auto);
    EXPECT_EQ(params.height.unit, ITermDimension::Unit::Auto);
    EXPECT_TRUE(params.preserve_aspect_ratio);
}

TEST(ITermImageTest, RejectsNoPayload) {
    ITermImageParams params;
    std::string payload;
    // No colon = no payload
    EXPECT_FALSE(parseITermImageOsc("inline=1", params, payload));
}

TEST(ITermImageTest, RejectsEmptyPayload) {
    ITermImageParams params;
    std::string payload;
    // Colon but empty data
    EXPECT_FALSE(parseITermImageOsc("inline=1:", params, payload));
}

// --- Base64 decode tests ---

TEST(ITermImageTest, Base64DecodeBasic) {
    auto result = iTermBase64Decode("SGVsbG8=");
    std::string str(result.begin(), result.end());
    EXPECT_EQ(str, "Hello");
}

TEST(ITermImageTest, Base64DecodeEmpty) {
    auto result = iTermBase64Decode("");
    EXPECT_TRUE(result.empty());
}

// --- calculateDisplayCells tests ---

TEST(ITermImageTest, CalculateAutoSizeUsesNaturalDimensions) {
    ITermImageParams params;
    params.width.unit = ITermDimension::Unit::Auto;
    params.height.unit = ITermDimension::Unit::Auto;

    int cols = 0, rows = 0;
    calculateDisplayCells(params, 8, 16, 80, 24, 160, 96, cols, rows);
    // 160px / 8px = 20 cols, 96px / 16px = 6 rows
    EXPECT_EQ(cols, 20);
    EXPECT_EQ(rows, 6);
}

TEST(ITermImageTest, CalculateCellDimensions) {
    ITermImageParams params;
    params.width.unit = ITermDimension::Unit::Cells;
    params.width.value = 10;
    params.height.unit = ITermDimension::Unit::Cells;
    params.height.value = 5;
    params.preserve_aspect_ratio = false;

    int cols = 0, rows = 0;
    calculateDisplayCells(params, 8, 16, 80, 24, 100, 100, cols, rows);
    EXPECT_EQ(cols, 10);
    EXPECT_EQ(rows, 5);
}

TEST(ITermImageTest, CalculateClampToTerminalSize) {
    ITermImageParams params;
    params.width.unit = ITermDimension::Unit::Cells;
    params.width.value = 200; // Larger than terminal
    params.height.unit = ITermDimension::Unit::Cells;
    params.height.value = 100;
    params.preserve_aspect_ratio = false;

    int cols = 0, rows = 0;
    calculateDisplayCells(params, 8, 16, 80, 24, 100, 100, cols, rows);
    EXPECT_EQ(cols, 80);
    EXPECT_EQ(rows, 24);
}

// --- Screen integration via OSC 1337 ---

TEST(ITermImageTest, OscIgnoresNonInlineImages) {
    Screen screen(24, 80);
    VtParser parser(screen);

    // Send OSC 1337 with inline=0 (should be ignored)
    std::string osc = "\033]1337;File=inline=0:AAAA\033\\";
    parser.feed(osc.c_str(), osc.size());

    // No placements should be created
    EXPECT_TRUE(screen.kittyGraphics().placements().empty());
}

TEST(ITermImageTest, OscIgnoresNonFilePrefix) {
    Screen screen(24, 80);
    VtParser parser(screen);

    // Send OSC 1337 without File= prefix
    std::string osc = "\033]1337;NotFile=something\033\\";
    parser.feed(osc.c_str(), osc.size());

    EXPECT_TRUE(screen.kittyGraphics().placements().empty());
}
