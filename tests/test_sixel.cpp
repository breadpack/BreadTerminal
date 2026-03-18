#include <gtest/gtest.h>
#include "termcore/sixel.h"

using namespace termcore;

// 1. Empty data produces empty image
TEST(SixelParserTest, EmptyData) {
    auto img = parseSixel("");
    EXPECT_TRUE(img.empty());
    EXPECT_EQ(img.width, 0);
    EXPECT_EQ(img.height, 0);
}

// 2. Single '?' (0x3F, bits=0) produces no visible pixels
TEST(SixelParserTest, SingleQuestionMark) {
    auto img = parseSixel("?");
    // '?' = 0x3F, bits = 0x3F - 0x3F = 0, no pixels set
    EXPECT_TRUE(img.empty());
}

// 3. Single '~' (0x7E, all 6 bits set) produces 1x6 column
TEST(SixelParserTest, SingleTilde) {
    auto img = parseSixel("~");
    ASSERT_FALSE(img.empty());
    EXPECT_EQ(img.width, 1);
    EXPECT_EQ(img.height, 6);
    // All 6 pixels should be non-zero (colored)
    for (int y = 0; y < 6; ++y) {
        EXPECT_NE(img.pixels[y], 0u) << "pixel at y=" << y << " should be set";
    }
}

// 4. Character 'A' (0x41) has specific bit pattern
TEST(SixelParserTest, CharacterA) {
    // 'A' = 0x41, bits = 0x41 - 0x3F = 0x02 = 0b000010
    // Only bit 1 is set, so pixel at y=1 should be colored
    auto img = parseSixel("A");
    ASSERT_FALSE(img.empty());
    EXPECT_EQ(img.width, 1);
    EXPECT_EQ(img.height, 2); // maxY = cursorY(0) + bit(1) + 1 = 2

    // y=0 should be transparent (0)
    EXPECT_EQ(img.pixels[0], 0u);
    // y=1 should be colored
    EXPECT_NE(img.pixels[1], 0u);
}

// 5. Repeat "!5~" produces 5 columns of full sixel
TEST(SixelParserTest, RepeatCommand) {
    auto img = parseSixel("!5~");
    ASSERT_FALSE(img.empty());
    EXPECT_EQ(img.width, 5);
    EXPECT_EQ(img.height, 6);
    for (int x = 0; x < 5; ++x) {
        for (int y = 0; y < 6; ++y) {
            EXPECT_NE(img.pixels[y * 5 + x], 0u)
                << "pixel at (" << x << "," << y << ") should be set";
        }
    }
}

// 6. Carriage return '$' resets x to 0
TEST(SixelParserTest, CarriageReturn) {
    // Draw two columns, then CR, then draw one column overwriting first
    // "~~$~" -> draw at x=0,1 then CR, draw at x=0 again
    auto img = parseSixel("~~$~");
    ASSERT_FALSE(img.empty());
    EXPECT_EQ(img.width, 2); // max x reached was 2 (columns 0 and 1)
    EXPECT_EQ(img.height, 6);
    // x=0 should be colored (overwritten by second draw)
    EXPECT_NE(img.pixels[0], 0u);
    // x=1 should be colored (from first draw)
    EXPECT_NE(img.pixels[1], 0u);
}

// 7. New line '-' advances y by 6
TEST(SixelParserTest, NewLine) {
    // Draw a full column, then newline, then another full column
    auto img = parseSixel("~-~");
    ASSERT_FALSE(img.empty());
    EXPECT_EQ(img.width, 1);
    EXPECT_EQ(img.height, 12); // two bands of 6
    // First band: y=0..5
    for (int y = 0; y < 6; ++y) {
        EXPECT_NE(img.pixels[y], 0u) << "first band y=" << y;
    }
    // Second band: y=6..11
    for (int y = 6; y < 12; ++y) {
        EXPECT_NE(img.pixels[y], 0u) << "second band y=" << y;
    }
}

// 8. Color definition "#0;2;100;0;0" defines red
TEST(SixelParserTest, ColorDefinitionRed) {
    std::string data = "#0;2;100;0;0#0~";
    auto img = parseSixel(data);
    ASSERT_FALSE(img.empty());
    // Red = R:255, G:0, B:0, A:255 -> 0xFF0000FF in RGBA
    uint32_t expectedRed = 0xFF0000FF;
    // Check first pixel (y=0, x=0) - bit 0 of '~' is set
    EXPECT_EQ(img.pixels[0], expectedRed);
}

// 9. Color select and draw with different colors
TEST(SixelParserTest, ColorSelectAndDraw) {
    // Define color 1 as green, select it, draw
    std::string data = "#1;2;0;100;0#1~";
    auto img = parseSixel(data);
    ASSERT_FALSE(img.empty());
    uint32_t expectedGreen = 0x00FF00FF;
    EXPECT_EQ(img.pixels[0], expectedGreen);
}

// 10. Multiple colors in a single image
TEST(SixelParserTest, MultipleColors) {
    // Define red and blue, draw with each
    // Red column at x=0, blue column at x=1
    std::string data = "#0;2;100;0;0#1;2;0;0;100#0~#1~";
    auto img = parseSixel(data);
    ASSERT_FALSE(img.empty());
    EXPECT_EQ(img.width, 2);

    uint32_t red = 0xFF0000FF;
    uint32_t blue = 0x0000FFFF;
    // x=0 should be red (y=0)
    EXPECT_EQ(img.pixels[0 * 2 + 0], red);
    // x=1 should be blue (y=0)
    EXPECT_EQ(img.pixels[0 * 2 + 1], blue);
}

// 11. Correct dimensions calculation
TEST(SixelParserTest, CorrectDimensions) {
    // 3 columns on first band, newline, 2 columns on second band
    std::string data = "~~~-~~";
    auto img = parseSixel(data);
    ASSERT_FALSE(img.empty());
    EXPECT_EQ(img.width, 3);  // max of 3 and 2
    EXPECT_EQ(img.height, 12); // two bands: 0-5, 6-11
}

// 12. RGB percentage conversion accuracy
TEST(SixelParserTest, RgbPercentageConversion) {
    // 50% red, 50% green, 50% blue
    std::string data = "#0;2;50;50;50#0~";
    auto img = parseSixel(data);
    ASSERT_FALSE(img.empty());

    uint32_t pixel = img.pixels[0];
    uint8_t r = (pixel >> 24) & 0xFF;
    uint8_t g = (pixel >> 16) & 0xFF;
    uint8_t b = (pixel >> 8) & 0xFF;
    uint8_t a = pixel & 0xFF;

    // 50% of 255 = 127
    EXPECT_EQ(r, 127);
    EXPECT_EQ(g, 127);
    EXPECT_EQ(b, 127);
    EXPECT_EQ(a, 255);
}
