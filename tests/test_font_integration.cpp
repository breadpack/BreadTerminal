#include <gtest/gtest.h>
#include "mock_font.h"
#include "termcore/font/font_collection.h"
#include "termcore/font/glyph_cache.h"
#include "termcore/font/glyph_atlas.h"
#include <memory>

using namespace termcore;
using namespace termcore::test;

// ===========================================================================
// Helpers
// ===========================================================================

static FontDescriptor makeDesc(const std::string& family,
                               const std::string& path,
                               FontStyle style = FontStyle::Regular,
                               int weight = 400) {
    FontDescriptor d;
    d.family = family;
    d.file_path = path;
    d.face_index = 0;
    d.style = style;
    d.weight = weight;
    return d;
}

static GlyphKey makeKey(FontFaceId face, uint32_t glyph,
                         uint8_t sx = 0, uint8_t sy = 0) {
    GlyphKey k;
    k.face_id = face;
    k.glyph_index = glyph;
    k.subpixel = {sx, sy};
    return k;
}

// ===========================================================================
// Font Collection with Mocks — Fixture
// ===========================================================================

class FontCollectionIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Primary font: ASCII only (codepoints 0x20-0x7E)
        auto primary_desc = makeDesc("Primary", "/fonts/Primary-Regular.ttf");
        discovery_.addFont(primary_desc);

        // Fallback font: CJK range (U+4E00-U+9FFF)
        auto fallback_desc = makeDesc("CJKFallback", "/fonts/CJKFallback.ttf");
        discovery_.addFont(fallback_desc);

        // System fallback for emoji (U+1F600-U+1F64F)
        auto emoji_desc = makeDesc("Emoji", "/fonts/Emoji.ttf");
        discovery_.addFallbackRange(0x1F600, 0x1F64F, emoji_desc);

        // Pre-register face IDs so we can configure per-face codepoint sets.
        primary_face_ = rasterizer_.registerFace("/fonts/Primary-Regular.ttf");
        fallback_face_ = rasterizer_.registerFace("/fonts/CJKFallback.ttf");
        emoji_face_ = rasterizer_.registerFace("/fonts/Emoji.ttf");

        // Primary supports ASCII
        auto& primary_cp = rasterizer_.face_codepoints[primary_face_];
        for (char32_t cp = 0x20; cp <= 0x7E; ++cp) primary_cp.insert(cp);

        // CJK fallback supports CJK Unified Ideographs
        auto& cjk_cp = rasterizer_.face_codepoints[fallback_face_];
        // Add a representative subset rather than the full 20k range.
        for (char32_t cp = 0x4E00; cp <= 0x4E40; ++cp) cjk_cp.insert(cp);

        // Emoji font supports emoji range
        auto& emoji_cp = rasterizer_.face_codepoints[emoji_face_];
        for (char32_t cp = 0x1F600; cp <= 0x1F610; ++cp) emoji_cp.insert(cp);

        collection_ = std::make_unique<FontCollection>(
            rasterizer_, discovery_, shaper_);
    }

    MockFontRasterizer rasterizer_;
    MockFontDiscovery discovery_;
    FontShaper shaper_;
    std::unique_ptr<FontCollection> collection_;

    FontFaceId primary_face_ = kInvalidFontFace;
    FontFaceId fallback_face_ = kInvalidFontFace;
    FontFaceId emoji_face_ = kInvalidFontFace;
};

// ---------------------------------------------------------------------------
// Font Collection Tests
// ---------------------------------------------------------------------------

// Primary font resolves ASCII codepoints; fallback resolves CJK.
TEST_F(FontCollectionIntegrationTest, FallbackChainResolvesCorrectFace) {
    ASSERT_TRUE(collection_->setPrimaryFontFromFile(
        "/fonts/Primary-Regular.ttf", 0, 14.0f));
    collection_->addFallbackFontFromFile("/fonts/CJKFallback.ttf", 0);

    // ASCII 'A' should resolve to primary (index 0)
    CollectionFaceId face_a = collection_->resolveFace(U'A');
    EXPECT_EQ(face_a, 0u);

    // CJK U+4E2D should resolve to fallback (index 1)
    CollectionFaceId face_cjk = collection_->resolveFace(U'\x4E2D');
    EXPECT_EQ(face_cjk, 1u);

    // The two faces must be different
    EXPECT_NE(face_a, face_cjk);
}

// When no font in the chain has the glyph AND system fallback fails,
// resolveFace returns the primary face as a last resort (not crashing).
TEST_F(FontCollectionIntegrationTest, MissingGlyphUsesLastResort) {
    ASSERT_TRUE(collection_->setPrimaryFontFromFile(
        "/fonts/Primary-Regular.ttf", 0, 14.0f));

    // U+FFFD is not in any of our mock font codepoint sets, and
    // no system fallback range is configured for it.
    CollectionFaceId face = collection_->resolveFace(U'\xFFFD');

    // Should fall back to primary (index 0) rather than kInvalidCollectionFace
    EXPECT_EQ(face, 0u);
}

// Second lookup for the same codepoint uses the internal codepoint cache
// and does NOT walk the chain again (no extra getGlyphIndex calls).
TEST_F(FontCollectionIntegrationTest, CacheHitSkipsRasterization) {
    ASSERT_TRUE(collection_->setPrimaryFontFromFile(
        "/fonts/Primary-Regular.ttf", 0, 14.0f));

    // First resolve — populates codepoint cache inside FontCollection
    collection_->resolveFace(U'A');

    // Reset observation counters
    rasterizer_.resetCounters();

    // Second resolve — should hit internal cache
    CollectionFaceId face = collection_->resolveFace(U'A');
    EXPECT_EQ(face, 0u);

    // loadFont should NOT have been called again (face already loaded)
    EXPECT_EQ(rasterizer_.load_call_count, 0);
}

// First lookup for a codepoint is a cache miss and the rasterizer's
// getGlyphIndex is invoked.
TEST_F(FontCollectionIntegrationTest, CacheMissTriggersRasterization) {
    ASSERT_TRUE(collection_->setPrimaryFontFromFile(
        "/fonts/Primary-Regular.ttf", 0, 14.0f));
    collection_->addFallbackFontFromFile("/fonts/CJKFallback.ttf", 0);

    rasterizer_.resetCounters();

    // First lookup for CJK — must trigger ensureLoaded on the fallback
    CollectionFaceId face = collection_->resolveFace(U'\x4E00');
    EXPECT_EQ(face, 1u);

    // loadFont called at least once for the fallback font
    EXPECT_GE(rasterizer_.load_call_count, 1);
}

// System fallback returns an empty descriptor (no suitable font).
// resolveFace must not crash and should return the primary face.
TEST_F(FontCollectionIntegrationTest, RasterizationFailureHandledGracefully) {
    ASSERT_TRUE(collection_->setPrimaryFontFromFile(
        "/fonts/Primary-Regular.ttf", 0, 14.0f));

    // Disable all system fallback
    discovery_.simulate_no_fallback = true;

    // Try a codepoint that the primary doesn't cover
    CollectionFaceId face = collection_->resolveFace(U'\x4E00');

    // Must not crash; returns primary as last resort
    EXPECT_EQ(face, 0u);
}

// ===========================================================================
// Glyph Cache with Mock Rasterizer — Fixture
// ===========================================================================

class GlyphCacheMockTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Pre-register a face so we have a valid ID
        face_id_ = rasterizer_.registerFace("/fonts/TestFont.ttf");
    }

    MockFontRasterizer rasterizer_;
    FontFaceId face_id_ = kInvalidFontFace;
};

// getOrRasterize with a cache miss calls the rasterizer, and a subsequent
// insertFromBackground completes without error (simulating async flow).
TEST_F(GlyphCacheMockTest, BackgroundRasterizationCompletes) {
    GlyphCache cache(64);
    GlyphAtlas atlas(256, 1024);

    auto key = makeKey(face_id_, 65);  // 'A'

    // Simulate the async flow:
    // 1. tryGet returns nullopt (cache miss)
    auto cached = cache.tryGet(key);
    EXPECT_FALSE(cached.has_value());

    // 2. "Background" rasterization via mock
    RasterizedGlyph rasterized = rasterizer_.rasterize(
        face_id_, 65, 14.0f, {0, 0});
    EXPECT_GT(rasterized.width, 0);
    EXPECT_GT(rasterized.height, 0);

    // 3. Insert the background result into cache
    bool is_color = rasterizer_.isColorGlyph(face_id_, 65);
    auto info = cache.insertFromBackground(key, rasterized, is_color, atlas);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->region.width, rasterizer_.glyph_width);
    EXPECT_EQ(info->region.height, rasterizer_.glyph_height);

    // 4. Now tryGet should hit
    auto hit = cache.tryGet(key);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->region.width, info->region.width);
}

// After LRU eviction, the next access to the evicted glyph must trigger
// a fresh rasterization (cache miss).
TEST_F(GlyphCacheMockTest, EvictionTriggersReRasterization) {
    // Tiny cache: only 2 entries
    GlyphCache cache(2);
    GlyphAtlas atlas(256, 1024);

    auto key_a = makeKey(face_id_, 65);  // 'A'
    auto key_b = makeKey(face_id_, 66);  // 'B'
    auto key_c = makeKey(face_id_, 67);  // 'C'

    // Fill cache with A and B
    auto res_a = cache.getOrRasterize(key_a, 14.0f, rasterizer_, atlas);
    ASSERT_TRUE(res_a.has_value());
    auto res_b = cache.getOrRasterize(key_b, 14.0f, rasterizer_, atlas);
    ASSERT_TRUE(res_b.has_value());
    EXPECT_EQ(cache.size(), 2u);
    EXPECT_EQ(rasterizer_.rasterize_call_count, 2);

    // Insert C — evicts A (LRU)
    auto res_c = cache.getOrRasterize(key_c, 14.0f, rasterizer_, atlas);
    ASSERT_TRUE(res_c.has_value());
    EXPECT_EQ(cache.size(), 2u);
    EXPECT_EQ(rasterizer_.rasterize_call_count, 3);

    // A should now be a cache miss
    EXPECT_FALSE(cache.get(key_a).has_value());

    // Re-access A — should trigger rasterization again
    rasterizer_.resetCounters();
    auto res_a2 = cache.getOrRasterize(key_a, 14.0f, rasterizer_, atlas);
    ASSERT_TRUE(res_a2.has_value());
    EXPECT_EQ(rasterizer_.rasterize_call_count, 1);  // re-rasterized
}
