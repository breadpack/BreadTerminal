#include <gtest/gtest.h>
#include "termcore/font/glyph_cache.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/i_font_rasterizer.h"
#include <cstring>

using namespace termcore;

// Mock rasterizer for testing: returns simple 8x8 grayscale glyphs
class MockRasterizer : public IFontRasterizer {
public:
    int rasterize_call_count = 0;

    FontFaceId loadFont(const std::string& /*path*/, int /*face_index*/,
                        float /*size*/) override {
        return 1;
    }

    RasterizedGlyph rasterize(FontFaceId /*face*/, uint32_t /*glyph_index*/,
                               float /*size*/, SubpixelOffset /*offset*/) override {
        ++rasterize_call_count;
        RasterizedGlyph glyph;
        glyph.width = 8;
        glyph.height = 8;
        glyph.bearing_x = 0;
        glyph.bearing_y = 8;
        glyph.format = PixelFormat::Grayscale;
        glyph.bitmap.resize(8 * 8, 0xFF);
        return glyph;
    }

    FontMetrics getMetrics(FontFaceId /*face*/, float /*size*/) override {
        FontMetrics m{};
        m.cell_width = 8.0f;
        m.cell_height = 16.0f;
        m.ascent = 12.0f;
        m.descent = 4.0f;
        return m;
    }

    bool isColorGlyph(FontFaceId /*face*/, uint32_t /*glyph_index*/) override {
        return false;
    }

    uint32_t getGlyphIndex(FontFaceId /*face*/, char32_t codepoint) override {
        // Simple mapping: codepoint is glyph index
        return static_cast<uint32_t>(codepoint);
    }
};

static GlyphKey makeKey(FontFaceId face, uint32_t glyph, uint8_t sx = 0,
                         uint8_t sy = 0) {
    GlyphKey k;
    k.face_id = face;
    k.glyph_index = glyph;
    k.subpixel = {sx, sy};
    return k;
}

static GlyphInfo makeInfo(int x, int y, int w, int h) {
    GlyphInfo info;
    info.region.atlas_index = 0;
    info.region.x = x;
    info.region.y = y;
    info.region.width = w;
    info.region.height = h;
    info.region.bearing_x = 0;
    info.region.bearing_y = h;
    info.advance_x = static_cast<float>(w);
    info.advance_y = 0.0f;
    info.is_color = false;
    return info;
}

// 1. Empty cache returns nullopt
TEST(GlyphCacheTest, EmptyCacheReturnsNullopt) {
    GlyphCache cache(64);
    auto result = cache.get(makeKey(1, 42));
    EXPECT_FALSE(result.has_value());
}

// 2. Put and get returns cached info
TEST(GlyphCacheTest, PutAndGet) {
    GlyphCache cache(64);
    auto key = makeKey(1, 42);
    auto info = makeInfo(10, 20, 8, 8);
    cache.put(key, info);

    auto result = cache.get(key);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->region.x, 10);
    EXPECT_EQ(result->region.y, 20);
    EXPECT_EQ(result->region.width, 8);
    EXPECT_FLOAT_EQ(result->advance_x, 8.0f);
}

// 3. LRU eviction: fill cache to max, insert one more, oldest evicted
TEST(GlyphCacheTest, LruEviction) {
    GlyphCache cache(3);

    auto k1 = makeKey(1, 1);
    auto k2 = makeKey(1, 2);
    auto k3 = makeKey(1, 3);
    auto k4 = makeKey(1, 4);

    cache.put(k1, makeInfo(0, 0, 8, 8));
    cache.put(k2, makeInfo(0, 0, 8, 8));
    cache.put(k3, makeInfo(0, 0, 8, 8));
    EXPECT_EQ(cache.size(), 3u);

    // Insert k4 should evict k1 (oldest)
    cache.put(k4, makeInfo(0, 0, 8, 8));
    EXPECT_EQ(cache.size(), 3u);
    EXPECT_FALSE(cache.get(k1).has_value());
    EXPECT_TRUE(cache.get(k2).has_value());
    EXPECT_TRUE(cache.get(k3).has_value());
    EXPECT_TRUE(cache.get(k4).has_value());
}

// 4. Access moves entry to front (prevents eviction)
TEST(GlyphCacheTest, AccessMovesToFront) {
    GlyphCache cache(3);

    auto k1 = makeKey(1, 1);
    auto k2 = makeKey(1, 2);
    auto k3 = makeKey(1, 3);
    auto k4 = makeKey(1, 4);

    cache.put(k1, makeInfo(0, 0, 8, 8));
    cache.put(k2, makeInfo(0, 0, 8, 8));
    cache.put(k3, makeInfo(0, 0, 8, 8));

    // Access k1 to move it to front
    cache.get(k1);

    // Insert k4 — should evict k2 (now the oldest), not k1
    cache.put(k4, makeInfo(0, 0, 8, 8));
    EXPECT_TRUE(cache.get(k1).has_value());
    EXPECT_FALSE(cache.get(k2).has_value());
}

// 5. getOrRasterize: cache miss rasterizes and caches
TEST(GlyphCacheTest, GetOrRasterizeMiss) {
    GlyphCache cache(64);
    MockRasterizer rasterizer;
    GlyphAtlas atlas(256, 1024);

    auto key = makeKey(1, 42);
    auto result = cache.getOrRasterize(key, 16.0f, rasterizer, atlas);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->region.width, 8);
    EXPECT_EQ(result->region.height, 8);
    EXPECT_FALSE(result->is_color);
    EXPECT_EQ(rasterizer.rasterize_call_count, 1);

    // Should now be cached
    EXPECT_EQ(cache.size(), 1u);
}

// 6. getOrRasterize: cache hit returns without rasterizing
TEST(GlyphCacheTest, GetOrRasterizeHit) {
    GlyphCache cache(64);
    MockRasterizer rasterizer;
    GlyphAtlas atlas(256, 1024);

    auto key = makeKey(1, 42);

    // First call: rasterizes
    cache.getOrRasterize(key, 16.0f, rasterizer, atlas);
    EXPECT_EQ(rasterizer.rasterize_call_count, 1);

    // Second call: should hit cache, no rasterize
    auto result = cache.getOrRasterize(key, 16.0f, rasterizer, atlas);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(rasterizer.rasterize_call_count, 1);  // Still 1
}

// 7. Hit/miss statistics tracking
TEST(GlyphCacheTest, HitMissStats) {
    GlyphCache cache(64);

    auto key = makeKey(1, 42);
    cache.get(key);  // Miss
    EXPECT_EQ(cache.hits(), 0u);
    EXPECT_EQ(cache.misses(), 1u);

    cache.put(key, makeInfo(0, 0, 8, 8));
    cache.get(key);  // Hit
    EXPECT_EQ(cache.hits(), 1u);
    EXPECT_EQ(cache.misses(), 1u);

    cache.get(key);  // Hit again
    EXPECT_EQ(cache.hits(), 2u);

    cache.resetStats();
    EXPECT_EQ(cache.hits(), 0u);
    EXPECT_EQ(cache.misses(), 0u);
}

// 8. Clear empties the cache
TEST(GlyphCacheTest, Clear) {
    GlyphCache cache(64);
    cache.put(makeKey(1, 1), makeInfo(0, 0, 8, 8));
    cache.put(makeKey(1, 2), makeInfo(0, 0, 8, 8));
    EXPECT_EQ(cache.size(), 2u);

    cache.clear();
    EXPECT_EQ(cache.size(), 0u);
    EXPECT_FALSE(cache.get(makeKey(1, 1)).has_value());
}

// 9. precacheAscii: ASCII glyphs are cached after call
TEST(GlyphCacheTest, PrecacheAscii) {
    GlyphCache cache(8192);
    MockRasterizer rasterizer;
    GlyphAtlas atlas(512, 4096);

    cache.precacheAscii(1, 16.0f, rasterizer, atlas);

    // ASCII 32-126 = 95 glyphs
    EXPECT_EQ(cache.size(), 95u);

    // Check 'A' (65) is cached
    auto key_A = makeKey(1, 65);  // getGlyphIndex returns codepoint as index
    cache.resetStats();
    auto result = cache.get(key_A);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(cache.hits(), 1u);
    EXPECT_EQ(cache.misses(), 0u);
}

// 10. Size tracking
TEST(GlyphCacheTest, SizeTracking) {
    GlyphCache cache(64);
    EXPECT_EQ(cache.size(), 0u);

    cache.put(makeKey(1, 1), makeInfo(0, 0, 8, 8));
    EXPECT_EQ(cache.size(), 1u);

    cache.put(makeKey(1, 2), makeInfo(0, 0, 8, 8));
    EXPECT_EQ(cache.size(), 2u);

    cache.put(makeKey(1, 3), makeInfo(0, 0, 8, 8));
    EXPECT_EQ(cache.size(), 3u);
}

// 11. Duplicate put updates existing entry
TEST(GlyphCacheTest, DuplicatePutUpdates) {
    GlyphCache cache(64);
    auto key = makeKey(1, 42);

    cache.put(key, makeInfo(10, 20, 8, 8));
    EXPECT_EQ(cache.size(), 1u);

    // Update same key with different info
    cache.put(key, makeInfo(30, 40, 16, 16));
    EXPECT_EQ(cache.size(), 1u);  // Size unchanged

    auto result = cache.get(key);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->region.x, 30);
    EXPECT_EQ(result->region.y, 40);
    EXPECT_EQ(result->region.width, 16);
}
