#include <gtest/gtest.h>
#include "termcore/font/box_drawing.h"

namespace termcore {
namespace {

// ---------------------------------------------------------------------------
// is_box_drawing tests
// ---------------------------------------------------------------------------

TEST(BoxDrawing, IsBoxDrawingBoxLines) {
    EXPECT_TRUE(is_box_drawing(0x2500));  // ─
    EXPECT_TRUE(is_box_drawing(0x2502));  // │
    EXPECT_TRUE(is_box_drawing(0x250C));  // ┌
    EXPECT_TRUE(is_box_drawing(0x257F));  // last box drawing
}

TEST(BoxDrawing, IsBoxDrawingBlocks) {
    EXPECT_TRUE(is_box_drawing(0x2580));  // ▀
    EXPECT_TRUE(is_box_drawing(0x2588));  // █
    EXPECT_TRUE(is_box_drawing(0x259F));  // last block
}

TEST(BoxDrawing, IsBoxDrawingBraille) {
    EXPECT_TRUE(is_box_drawing(0x2800));  // empty braille
    EXPECT_TRUE(is_box_drawing(0x28FF));  // full braille
}

TEST(BoxDrawing, IsBoxDrawingPowerline) {
    EXPECT_TRUE(is_box_drawing(0xE0B0));
    EXPECT_TRUE(is_box_drawing(0xE0B3));
}

TEST(BoxDrawing, IsBoxDrawingFalseForAscii) {
    EXPECT_FALSE(is_box_drawing(U'A'));
    EXPECT_FALSE(is_box_drawing(0x0041));
    EXPECT_FALSE(is_box_drawing(U'z'));
    EXPECT_FALSE(is_box_drawing(U' '));
}

TEST(BoxDrawing, IsBoxDrawingFalseForOther) {
    EXPECT_FALSE(is_box_drawing(0x0000));
    EXPECT_FALSE(is_box_drawing(0x3000));  // CJK space
    EXPECT_FALSE(is_box_drawing(0xE0B4));  // just past powerline range
}

// ---------------------------------------------------------------------------
// Bitmap dimensions
// ---------------------------------------------------------------------------

TEST(BoxDrawing, BitmapDimensionsMatchCellSize) {
    auto bmp = render_box_glyph(0x2500, 8, 16);
    EXPECT_EQ(bmp.width, 8);
    EXPECT_EQ(bmp.height, 16);
    EXPECT_EQ(static_cast<int>(bmp.bitmap.size()), 8 * 16);
}

TEST(BoxDrawing, DifferentCellSizesProduceDifferentBitmaps) {
    auto bmp1 = render_box_glyph(0x2500, 8, 16);
    auto bmp2 = render_box_glyph(0x2500, 10, 20);
    EXPECT_EQ(bmp1.width, 8);
    EXPECT_EQ(bmp2.width, 10);
    EXPECT_EQ(bmp1.height, 16);
    EXPECT_EQ(bmp2.height, 20);
    EXPECT_NE(bmp1.bitmap.size(), bmp2.bitmap.size());
}

TEST(BoxDrawing, ZeroCellSizeReturnsEmpty) {
    auto bmp = render_box_glyph(0x2500, 0, 0);
    EXPECT_TRUE(bmp.bitmap.empty());
}

// ---------------------------------------------------------------------------
// Box Drawing Lines
// ---------------------------------------------------------------------------

TEST(BoxDrawing, HorizontalLightHasPixelsInCenterRow) {
    auto bmp = render_box_glyph(0x2500, 10, 20);  // ─
    int cy = 20 / 2;
    bool has_pixel = false;
    for (int x = 0; x < 10; ++x) {
        if (bmp.bitmap[cy * 10 + x] > 0) {
            has_pixel = true;
            break;
        }
    }
    EXPECT_TRUE(has_pixel);
}

TEST(BoxDrawing, VerticalLightHasPixelsInCenterColumn) {
    auto bmp = render_box_glyph(0x2502, 10, 20);  // │
    int cx = 10 / 2;
    bool has_pixel = false;
    for (int y = 0; y < 20; ++y) {
        if (bmp.bitmap[y * 10 + cx] > 0) {
            has_pixel = true;
            break;
        }
    }
    EXPECT_TRUE(has_pixel);
}

TEST(BoxDrawing, TopLeftCornerHasRightAndDown) {
    auto bmp = render_box_glyph(0x250C, 10, 20);  // ┌
    int cx = 10 / 2;
    int cy = 20 / 2;

    // Should have pixels going right from center
    bool has_right = false;
    for (int x = cx; x < 10; ++x) {
        if (bmp.bitmap[cy * 10 + x] > 0) {
            has_right = true;
            break;
        }
    }
    EXPECT_TRUE(has_right);

    // Should have pixels going down from center
    bool has_down = false;
    for (int y = cy; y < 20; ++y) {
        if (bmp.bitmap[y * 10 + cx] > 0) {
            has_down = true;
            break;
        }
    }
    EXPECT_TRUE(has_down);

    // Should NOT have pixels in top-left quadrant
    bool has_top_left = false;
    for (int y = 0; y < cy; ++y) {
        for (int x = 0; x < cx; ++x) {
            if (bmp.bitmap[y * 10 + x] > 0) {
                has_top_left = true;
                break;
            }
        }
    }
    EXPECT_FALSE(has_top_left);
}

TEST(BoxDrawing, HeavyHorizontalIsThicker) {
    auto light = render_box_glyph(0x2500, 10, 20);  // ─ light
    auto heavy = render_box_glyph(0x2501, 10, 20);  // ━ heavy

    int light_pixels = 0, heavy_pixels = 0;
    for (auto v : light.bitmap) if (v > 0) ++light_pixels;
    for (auto v : heavy.bitmap) if (v > 0) ++heavy_pixels;
    EXPECT_GT(heavy_pixels, light_pixels);
}

// ---------------------------------------------------------------------------
// Block Elements
// ---------------------------------------------------------------------------

TEST(BoxDrawing, FullBlockAllPixels255) {
    auto bmp = render_box_glyph(0x2588, 8, 16);  // █
    for (int i = 0; i < 8 * 16; ++i) {
        EXPECT_EQ(bmp.bitmap[i], 255);
    }
}

TEST(BoxDrawing, LowerHalfBlock) {
    auto bmp = render_box_glyph(0x2584, 8, 16);  // ▄
    // Top half should be empty
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 8; ++x)
            EXPECT_EQ(bmp.bitmap[y * 8 + x], 0) << "at y=" << y << " x=" << x;
    // Bottom half should be filled
    bool has_bottom = false;
    for (int y = 8; y < 16; ++y)
        for (int x = 0; x < 8; ++x)
            if (bmp.bitmap[y * 8 + x] == 255) has_bottom = true;
    EXPECT_TRUE(has_bottom);
}

TEST(BoxDrawing, UpperHalfBlock) {
    auto bmp = render_box_glyph(0x2580, 8, 16);  // ▀
    // Top half should be filled
    bool has_top = false;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            if (bmp.bitmap[y * 8 + x] == 255) has_top = true;
    EXPECT_TRUE(has_top);
    // Bottom half should be empty
    for (int y = 12; y < 16; ++y)
        for (int x = 0; x < 8; ++x)
            EXPECT_EQ(bmp.bitmap[y * 8 + x], 0) << "at y=" << y << " x=" << x;
}

TEST(BoxDrawing, LeftHalfBlock) {
    auto bmp = render_box_glyph(0x258C, 8, 16);  // ▌
    // Left half should have pixels
    bool has_left = false;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 4; ++x)
            if (bmp.bitmap[y * 8 + x] == 255) has_left = true;
    EXPECT_TRUE(has_left);
    // Right half should be empty
    for (int y = 0; y < 16; ++y)
        for (int x = 4; x < 8; ++x)
            EXPECT_EQ(bmp.bitmap[y * 8 + x], 0);
}

// ---------------------------------------------------------------------------
// Braille
// ---------------------------------------------------------------------------

TEST(BoxDrawing, EmptyBrailleAllZero) {
    auto bmp = render_box_glyph(0x2800, 10, 20);  // empty braille
    for (auto v : bmp.bitmap) {
        EXPECT_EQ(v, 0);
    }
}

TEST(BoxDrawing, FullBrailleHasAllDots) {
    auto bmp = render_box_glyph(0x28FF, 10, 20);  // all dots braille
    int non_zero = 0;
    for (auto v : bmp.bitmap) if (v > 0) ++non_zero;
    EXPECT_GT(non_zero, 0);
    // Should have dots in multiple quadrants
    // Check upper-left area
    bool upper_left = false;
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 5; ++x)
            if (bmp.bitmap[y * 10 + x] > 0) upper_left = true;
    EXPECT_TRUE(upper_left);
    // Check lower-right area
    bool lower_right = false;
    for (int y = 10; y < 20; ++y)
        for (int x = 5; x < 10; ++x)
            if (bmp.bitmap[y * 10 + x] > 0) lower_right = true;
    EXPECT_TRUE(lower_right);
}

// ---------------------------------------------------------------------------
// Powerline
// ---------------------------------------------------------------------------

TEST(BoxDrawing, PowerlineRightTriangleNonEmpty) {
    auto bmp = render_box_glyph(0xE0B0, 10, 20);
    int non_zero = 0;
    for (auto v : bmp.bitmap) if (v > 0) ++non_zero;
    EXPECT_GT(non_zero, 0);
}

TEST(BoxDrawing, PowerlineLeftTriangleNonEmpty) {
    auto bmp = render_box_glyph(0xE0B2, 10, 20);
    int non_zero = 0;
    for (auto v : bmp.bitmap) if (v > 0) ++non_zero;
    EXPECT_GT(non_zero, 0);
}

TEST(BoxDrawing, UnknownCodepointReturnsEmpty) {
    auto bmp = render_box_glyph(U'A', 10, 20);
    EXPECT_TRUE(bmp.bitmap.empty());
    EXPECT_EQ(bmp.width, 0);
    EXPECT_EQ(bmp.height, 0);
}

// ---------------------------------------------------------------------------
// Rounded corners and diagonals
// ---------------------------------------------------------------------------

TEST(BoxDrawing, RoundedCornerNonEmpty) {
    auto bmp = render_box_glyph(0x256D, 10, 20);  // ╭
    int non_zero = 0;
    for (auto v : bmp.bitmap) if (v > 0) ++non_zero;
    EXPECT_GT(non_zero, 0);
}

TEST(BoxDrawing, DiagonalNonEmpty) {
    auto bmp = render_box_glyph(0x2571, 10, 20);  // ╱
    int non_zero = 0;
    for (auto v : bmp.bitmap) if (v > 0) ++non_zero;
    EXPECT_GT(non_zero, 0);
}

TEST(BoxDrawing, DoubleHorizontalNonEmpty) {
    auto bmp = render_box_glyph(0x2550, 10, 20);  // ═
    int non_zero = 0;
    for (auto v : bmp.bitmap) if (v > 0) ++non_zero;
    EXPECT_GT(non_zero, 0);
}

} // namespace
} // namespace termcore
