#include <gtest/gtest.h>
#include "termcore/font/font_collection.h"
#include <unordered_map>

using namespace termcore;

// ---------------------------------------------------------------------------
// Mock implementations
// ---------------------------------------------------------------------------

class CollectionMockRasterizer : public IFontRasterizer {
public:
    FontFaceId loadFont(const std::string& path, int /*face_index*/, float /*size*/) override {
        auto it = path_to_face_.find(path);
        if (it != path_to_face_.end()) {
            return it->second;
        }
        FontFaceId id = next_id_++;
        path_to_face_[path] = id;
        face_to_path_[id] = path;
        return id;
    }

    RasterizedGlyph rasterize(FontFaceId /*face*/, uint32_t /*glyph_index*/,
                               float /*size*/, SubpixelOffset /*offset*/) override {
        RasterizedGlyph glyph;
        glyph.width = 8;
        glyph.height = 8;
        glyph.bitmap.resize(64, 128);
        glyph.format = PixelFormat::Grayscale;
        return glyph;
    }

    FontMetrics getMetrics(FontFaceId /*face*/, float size) override {
        FontMetrics m{};
        m.cell_width = size * 0.6f;
        m.cell_height = size * 1.2f;
        m.ascent = size * 0.8f;
        m.descent = size * 0.2f;
        m.underline_position = size * 0.1f;
        m.underline_thickness = 1.0f;
        m.strikethrough_position = size * 0.4f;
        m.strikethrough_thickness = 1.0f;
        return m;
    }

    bool isColorGlyph(FontFaceId /*face*/, uint32_t /*glyph_index*/) override {
        return false;
    }

    uint32_t getGlyphIndex(FontFaceId face, char32_t codepoint) override {
        // Look up which path this face came from
        auto it = face_to_path_.find(face);
        if (it == face_to_path_.end()) return 0;
        const auto& path = it->second;

        // TestMono fonts: have ASCII glyphs
        if (path.find("TestMono") != std::string::npos && codepoint < 128) {
            return static_cast<uint32_t>(codepoint);
        }
        // CJK font: has CJK range U+4E00-U+9FFF
        if (path.find("CJK") != std::string::npos &&
            codepoint >= 0x4E00 && codepoint <= 0x9FFF) {
            return static_cast<uint32_t>(codepoint);
        }
        // Emoji font: has emoji range U+1F600-U+1F64F
        if (path.find("Emoji") != std::string::npos &&
            codepoint >= 0x1F600 && codepoint <= 0x1F64F) {
            return static_cast<uint32_t>(codepoint);
        }
        return 0;
    }

private:
    FontFaceId next_id_ = 1;
    std::unordered_map<std::string, FontFaceId> path_to_face_;
    std::unordered_map<FontFaceId, std::string> face_to_path_;
};

class CollectionMockDiscovery : public IFontDiscovery {
public:
    std::vector<FontDescriptor> findFonts(const FontQuery& query) override {
        std::vector<FontDescriptor> results;

        if (query.family == "TestMono") {
            FontDescriptor desc;
            desc.family = "TestMono";
            desc.file_path = "/fake/fonts/TestMono-Regular.ttf";
            desc.face_index = 0;
            desc.style = query.style;
            desc.weight = query.weight;

            // Adjust path based on style
            if (query.style == FontStyle::Bold) {
                desc.file_path = "/fake/fonts/TestMono-Bold.ttf";
            } else if (query.style == FontStyle::Italic) {
                desc.file_path = "/fake/fonts/TestMono-Italic.ttf";
            } else if (query.style == FontStyle::BoldItalic) {
                desc.file_path = "/fake/fonts/TestMono-BoldItalic.ttf";
            }

            results.push_back(desc);
        } else if (query.family == "NotoSansCJK") {
            FontDescriptor desc;
            desc.family = "NotoSansCJK";
            desc.file_path = "/fake/fonts/NotoSansCJK.ttf";
            desc.face_index = 0;
            desc.style = FontStyle::Regular;
            desc.weight = 400;
            results.push_back(desc);
        }

        return results;
    }

    FontDescriptor findFallback(char32_t codepoint, FontStyle /*style*/) override {
        FontDescriptor desc;

        // CJK codepoints -> CJK font
        if (codepoint >= 0x4E00 && codepoint <= 0x9FFF) {
            desc.family = "NotoSansCJK";
            desc.file_path = "/fake/fonts/NotoSansCJK.ttf";
            desc.face_index = 0;
            desc.style = FontStyle::Regular;
            desc.weight = 400;
        }
        // Emoji codepoints -> Emoji font
        else if (codepoint >= 0x1F600 && codepoint <= 0x1F64F) {
            desc.family = "NotoColorEmoji";
            desc.file_path = "/fake/fonts/NotoColorEmoji.ttf";
            desc.face_index = 0;
            desc.style = FontStyle::Regular;
            desc.weight = 400;
        }
        // Unknown -> empty descriptor (no fallback)

        return desc;
    }

    FontDescriptor defaultMonospace() override {
        FontDescriptor desc;
        desc.family = "TestMono";
        desc.file_path = "/fake/fonts/TestMono-Regular.ttf";
        desc.face_index = 0;
        desc.style = FontStyle::Regular;
        desc.weight = 400;
        return desc;
    }
};

// Minimal shaper stub that doesn't actually use HarfBuzz
// We create a stub that satisfies the linker for tests.
// FontShaper::loadFont is used but we can't easily mock it since it's
// not virtual. We'll construct a real FontShaper and accept that loadFont
// will fail (return kInvalidFontFace) for fake paths. That's fine for
// collection-level tests.

class FontCollectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        collection_ = std::make_unique<FontCollection>(
            rasterizer_, discovery_, shaper_);
    }

    CollectionMockRasterizer rasterizer_;
    CollectionMockDiscovery discovery_;
    FontShaper shaper_;
    std::unique_ptr<FontCollection> collection_;
};

// Test 1: setPrimaryFont loads at least 1 entry
TEST_F(FontCollectionTest, SetPrimaryFont_ChainHasEntries) {
    bool ok = collection_->setPrimaryFont("TestMono", 14.0f);
    ASSERT_TRUE(ok);
    // Primary + Bold + Italic + BoldItalic = 4
    EXPECT_GE(collection_->chainLength(), 1u);
}

// Test 2: resolveFace for ASCII returns primary face (index 0)
TEST_F(FontCollectionTest, ResolveFace_ASCII_ReturnsPrimary) {
    ASSERT_TRUE(collection_->setPrimaryFont("TestMono", 14.0f));
    FontFaceId face = collection_->resolveFace(U'A');
    EXPECT_EQ(face, 0u);
}

// Test 3: resolveFace for CJK codepoint triggers system fallback
TEST_F(FontCollectionTest, ResolveFace_CJK_TriesSystemFallback) {
    ASSERT_TRUE(collection_->setPrimaryFont("TestMono", 14.0f));
    size_t chain_before = collection_->chainLength();
    FontFaceId face = collection_->resolveFace(U'\x4E2D');  // CJK char
    // System fallback should have added a new entry
    EXPECT_GT(collection_->chainLength(), chain_before);
    EXPECT_NE(face, kInvalidFontFace);
}

// Test 4: Codepoint cache - second lookup uses cache
TEST_F(FontCollectionTest, CodepointCache_SecondLookupCached) {
    ASSERT_TRUE(collection_->setPrimaryFont("TestMono", 14.0f));

    FontFaceId face1 = collection_->resolveFace(U'A');
    FontFaceId face2 = collection_->resolveFace(U'A');
    EXPECT_EQ(face1, face2);
}

// Test 5: addFallbackFont increases chain length
TEST_F(FontCollectionTest, AddFallbackFont_IncreasesChainLength) {
    ASSERT_TRUE(collection_->setPrimaryFont("TestMono", 14.0f));
    size_t before = collection_->chainLength();
    collection_->addFallbackFont("NotoSansCJK");
    EXPECT_EQ(collection_->chainLength(), before + 1);
}

// Test 6: primaryMetrics returns valid metrics
TEST_F(FontCollectionTest, PrimaryMetrics_ReturnsValidMetrics) {
    ASSERT_TRUE(collection_->setPrimaryFont("TestMono", 14.0f));
    FontMetrics m = collection_->primaryMetrics();
    EXPECT_GT(m.cell_width, 0.0f);
    EXPECT_GT(m.cell_height, 0.0f);
    EXPECT_GT(m.ascent, 0.0f);
    EXPECT_GT(m.descent, 0.0f);
}

// Test 7: setPrimaryFontFromFile loads successfully
TEST_F(FontCollectionTest, SetPrimaryFontFromFile_LoadsSuccessfully) {
    bool ok = collection_->setPrimaryFontFromFile("/fake/fonts/TestMono-Regular.ttf", 0, 14.0f);
    ASSERT_TRUE(ok);
    EXPECT_EQ(collection_->chainLength(), 1u);
}

// Test 8: Chain length tracking
TEST_F(FontCollectionTest, ChainLengthTracking) {
    EXPECT_EQ(collection_->chainLength(), 0u);
    ASSERT_TRUE(collection_->setPrimaryFont("TestMono", 14.0f));
    size_t after_primary = collection_->chainLength();
    EXPECT_GE(after_primary, 1u);

    collection_->addFallbackFont("NotoSansCJK");
    EXPECT_EQ(collection_->chainLength(), after_primary + 1);
}

// Test 9: rasterizerFaceId / shaperFaceId for primary
TEST_F(FontCollectionTest, FaceIdAccessors) {
    ASSERT_TRUE(collection_->setPrimaryFont("TestMono", 14.0f));
    FontFaceId rast = collection_->rasterizerFaceId(0);
    EXPECT_NE(rast, kInvalidFontFace);

    // Shaper may return kInvalidFontFace since FontShaper::loadFont
    // can't actually load fake paths, but the accessor should not crash
    collection_->shaperFaceId(0);

    // Out of bounds should return kInvalidFontFace
    EXPECT_EQ(collection_->rasterizerFaceId(999), kInvalidFontFace);
}

// Test 10: scaleFactor for primary is 1.0
TEST_F(FontCollectionTest, ScaleFactor_PrimaryIsOne) {
    ASSERT_TRUE(collection_->setPrimaryFont("TestMono", 14.0f));
    EXPECT_FLOAT_EQ(collection_->scaleFactor(0), 1.0f);
}

// Test 11: Unknown font family returns false
TEST_F(FontCollectionTest, SetPrimaryFont_UnknownFamily_ReturnsFalse) {
    bool ok = collection_->setPrimaryFont("NonExistentFont", 14.0f);
    EXPECT_FALSE(ok);
    EXPECT_EQ(collection_->chainLength(), 0u);
}

// Test 12: setFontSize changes font size
TEST_F(FontCollectionTest, SetFontSize_ChangesFontSize) {
    ASSERT_TRUE(collection_->setPrimaryFont("TestMono", 14.0f));
    EXPECT_FLOAT_EQ(collection_->fontSize(), 14.0f);
    collection_->setFontSize(20.0f);
    EXPECT_FLOAT_EQ(collection_->fontSize(), 20.0f);
}

// Test 13: addFallbackFontFromFile
TEST_F(FontCollectionTest, AddFallbackFontFromFile) {
    ASSERT_TRUE(collection_->setPrimaryFont("TestMono", 14.0f));
    size_t before = collection_->chainLength();
    collection_->addFallbackFontFromFile("/fake/fonts/SomeFont.ttf", 0);
    EXPECT_EQ(collection_->chainLength(), before + 1);
}

// Test 14: Emoji fallback
TEST_F(FontCollectionTest, ResolveFace_Emoji_TriesFallback) {
    ASSERT_TRUE(collection_->setPrimaryFont("TestMono", 14.0f));
    size_t chain_before = collection_->chainLength();
    FontFaceId face = collection_->resolveFace(U'\x1F600');  // grinning face emoji
    EXPECT_GT(collection_->chainLength(), chain_before);
    EXPECT_NE(face, kInvalidFontFace);
}
