#include <gtest/gtest.h>
#include "termcore/font/glyph_atlas.h"
#include <cstring>
#include <set>

namespace termcore {
namespace {

/// Helper: create a fake rasterized glyph with given dimensions and format.
RasterizedGlyph makeGlyph(int w, int h, PixelFormat fmt = PixelFormat::Grayscale,
                           int bearing_x = 0, int bearing_y = 0, uint8_t fill = 0xAA) {
    RasterizedGlyph g;
    g.width = w;
    g.height = h;
    g.bearing_x = bearing_x;
    g.bearing_y = bearing_y;
    g.format = fmt;

    int bpp = 1;
    if (fmt == PixelFormat::BGRA) bpp = 4;
    else if (fmt == PixelFormat::RGB) bpp = 3;

    g.bitmap.resize(static_cast<size_t>(w) * h * bpp, fill);
    return g;
}

// 1. Create atlas with default size -> pages exist after first pack
TEST(GlyphAtlasTest, CreateWithDefaultSize) {
    GlyphAtlas atlas(512, 4096);
    // No pages until first pack
    EXPECT_EQ(atlas.pageCount(), 0u);

    auto glyph = makeGlyph(10, 10);
    atlas.pack(glyph);

    // After packing a grayscale glyph, pages up to R8 index should exist
    EXPECT_GE(atlas.pageCount(), 1u);
    EXPECT_NE(atlas.getPage(AtlasFormat::R8), nullptr);
}

// 2. Pack a small glyph -> returns valid region
TEST(GlyphAtlasTest, PackSmallGlyph) {
    GlyphAtlas atlas(512, 4096);
    auto glyph = makeGlyph(10, 10);
    auto region = atlas.pack(glyph);

    ASSERT_TRUE(region.has_value());
    EXPECT_EQ(region->width, 10);
    EXPECT_EQ(region->height, 10);
}

// 3. Pack multiple glyphs -> all get unique non-overlapping regions
TEST(GlyphAtlasTest, PackMultipleNonOverlapping) {
    GlyphAtlas atlas(512, 4096);

    struct Rect { int x, y, w, h; };
    std::vector<Rect> rects;

    for (int i = 0; i < 20; ++i) {
        auto glyph = makeGlyph(8 + (i % 5), 12 + (i % 3));
        auto region = atlas.pack(glyph);
        ASSERT_TRUE(region.has_value()) << "Failed to pack glyph " << i;
        rects.push_back({region->x, region->y, region->width, region->height});
    }

    // Check no two rects overlap (brute force for small N)
    for (size_t i = 0; i < rects.size(); ++i) {
        for (size_t j = i + 1; j < rects.size(); ++j) {
            auto& a = rects[i];
            auto& b = rects[j];
            bool overlap = (a.x < b.x + b.w) && (a.x + a.w > b.x) &&
                           (a.y < b.y + b.h) && (a.y + a.h > b.y);
            EXPECT_FALSE(overlap) << "Rects " << i << " and " << j << " overlap";
        }
    }
}

// 4. Region has correct dimensions
TEST(GlyphAtlasTest, RegionCorrectDimensions) {
    GlyphAtlas atlas(512, 4096);
    auto glyph = makeGlyph(15, 20, PixelFormat::Grayscale, 3, 18);
    auto region = atlas.pack(glyph);

    ASSERT_TRUE(region.has_value());
    EXPECT_EQ(region->width, 15);
    EXPECT_EQ(region->height, 20);
    EXPECT_EQ(region->bearing_x, 3);
    EXPECT_EQ(region->bearing_y, 18);
}

// 5. Blit data -> verify pixels at region location
TEST(GlyphAtlasTest, BlitDataVerifyPixels) {
    GlyphAtlas atlas(512, 4096);
    uint8_t fill = 0xBB;
    auto glyph = makeGlyph(4, 4, PixelFormat::Grayscale, 0, 0, fill);
    auto region = atlas.pack(glyph);
    ASSERT_TRUE(region.has_value());

    auto* page = atlas.getPage(AtlasFormat::R8);
    ASSERT_NE(page, nullptr);

    const uint8_t* data = page->data();
    int stride = page->width();

    // Check that the glyph pixels are written correctly
    for (int row = 0; row < region->height; ++row) {
        for (int col = 0; col < region->width; ++col) {
            int offset = (region->y + row) * stride + (region->x + col);
            EXPECT_EQ(data[offset], fill)
                << "Mismatch at (" << col << ", " << row << ")";
        }
    }

    // Check that the border pixels (1px outside) are zero
    // Check pixel above the region (border row)
    if (region->y > 0) {
        int offset = (region->y - 1) * stride + region->x;
        EXPECT_EQ(data[offset], 0) << "Top border should be zero";
    }
}

// 6. Pack grayscale glyph -> goes to R8 atlas
TEST(GlyphAtlasTest, GrayscaleGoesToR8) {
    GlyphAtlas atlas(512, 4096);
    auto glyph = makeGlyph(10, 10, PixelFormat::Grayscale);
    auto region = atlas.pack(glyph);

    ASSERT_TRUE(region.has_value());
    EXPECT_EQ(region->atlas_index, static_cast<int>(AtlasFormat::R8));
}

// 7. Pack BGRA glyph -> goes to BGRA atlas
TEST(GlyphAtlasTest, BGRAGoesToBGRA) {
    GlyphAtlas atlas(512, 4096);
    auto glyph = makeGlyph(10, 10, PixelFormat::BGRA);
    auto region = atlas.pack(glyph);

    ASSERT_TRUE(region.has_value());
    EXPECT_EQ(region->atlas_index, static_cast<int>(AtlasFormat::BGRA));
}

// 8. 1px border: packed region has 1px margin
TEST(GlyphAtlasTest, OnePxBorder) {
    GlyphAtlas atlas(512, 4096);
    auto glyph = makeGlyph(10, 10);
    auto region = atlas.pack(glyph);

    ASSERT_TRUE(region.has_value());
    // First glyph should be placed at (1,1) — inside the 1px border
    EXPECT_GE(region->x, 1);
    EXPECT_GE(region->y, 1);
}

// 9. Atlas expansion: pack many glyphs until atlas must expand
TEST(GlyphAtlasTest, AtlasExpansion) {
    // Use a tiny initial size so expansion happens quickly
    GlyphAtlas atlas(32, 256);
    int packed = 0;
    // 32x32 atlas can fit only a few 10x10 glyphs (with 2px padding = 12x12 each)
    // 32/12 = 2 per row, 2 rows = ~4 glyphs before needing expansion
    for (int i = 0; i < 50; ++i) {
        auto glyph = makeGlyph(10, 10);
        auto region = atlas.pack(glyph);
        if (region) ++packed;
    }

    // We should have packed more than what fits in 32x32
    EXPECT_GT(packed, 4);

    // Atlas should have expanded
    auto* page = atlas.getPage(AtlasFormat::R8);
    ASSERT_NE(page, nullptr);
    EXPECT_GT(page->width(), 32);
}

// 10. Dirty flag: starts clean, becomes dirty after pack, clearable
TEST(GlyphAtlasTest, DirtyFlag) {
    GlyphAtlas atlas(512, 4096);

    // Before any pack, no pages, no dirty
    EXPECT_FALSE(atlas.anyDirty());

    auto glyph = makeGlyph(10, 10);
    atlas.pack(glyph);

    EXPECT_TRUE(atlas.anyDirty());

    atlas.clearAllDirty();
    EXPECT_FALSE(atlas.anyDirty());
}

// 11. Atlas format for pixel format mapping
TEST(GlyphAtlasTest, FormatMapping) {
    EXPECT_EQ(GlyphAtlas::formatForPixelFormat(PixelFormat::Grayscale), AtlasFormat::R8);
    EXPECT_EQ(GlyphAtlas::formatForPixelFormat(PixelFormat::BGRA), AtlasFormat::BGRA);
    EXPECT_EQ(GlyphAtlas::formatForPixelFormat(PixelFormat::RGB), AtlasFormat::RGB);
}

// 12. Pack empty glyph (0x0) -> handled gracefully
TEST(GlyphAtlasTest, PackEmptyGlyph) {
    GlyphAtlas atlas(512, 4096);
    auto glyph = makeGlyph(0, 0);
    auto region = atlas.pack(glyph);

    ASSERT_TRUE(region.has_value());
    EXPECT_EQ(region->width, 0);
    EXPECT_EQ(region->height, 0);
}

} // namespace
} // namespace termcore
