#ifndef TESTS_MOCK_FONT_H
#define TESTS_MOCK_FONT_H

#include "termcore/font/i_font_rasterizer.h"
#include "termcore/font/i_font_discovery.h"
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace termcore::test {

// ---------------------------------------------------------------------------
// MockFontRasterizer
// ---------------------------------------------------------------------------
// Configurable mock that returns synthetic glyph bitmaps (solid colored
// rectangles).  Tracks which glyphs were rasterized and can simulate
// rasterization failures for specific codepoints.

class MockFontRasterizer : public IFontRasterizer {
public:
    // -- Configuration ------------------------------------------------------

    /// Pixel value used to fill synthetic bitmaps (default 0xFF = opaque white).
    uint8_t fill_value = 0xFF;

    /// Configurable cell dimensions returned by getMetrics().
    float metric_cell_width = 8.0f;
    float metric_cell_height = 16.0f;
    float metric_ascent = 12.0f;
    float metric_descent = 4.0f;

    /// Configurable rasterized glyph dimensions.
    int32_t glyph_width = 8;
    int32_t glyph_height = 12;

    /// Set of glyph indices for which rasterize() returns an empty bitmap
    /// (simulating rasterization failure).
    std::unordered_set<uint32_t> failing_glyphs;

    /// Per-face set of supported codepoints.  If a face ID is not present in
    /// this map the mock falls back to "supports everything except 0".
    /// Map: FontFaceId -> set of codepoints that map to non-zero glyph indices.
    std::unordered_map<FontFaceId, std::set<char32_t>> face_codepoints;

    // -- Observation --------------------------------------------------------

    /// Number of times rasterize() was called.
    int rasterize_call_count = 0;

    /// Set of (face, glyph_index) pairs that were rasterized.
    std::set<std::pair<FontFaceId, uint32_t>> rasterized_glyphs;

    /// Number of times loadFont() was called.
    int load_call_count = 0;

    // -- IFontRasterizer implementation -------------------------------------

    FontFaceId loadFont(const std::string& path, int /*face_index*/,
                        float /*size*/) override {
        ++load_call_count;
        // Deterministic mapping: same path always returns same face ID.
        auto it = path_to_face_.find(path);
        if (it != path_to_face_.end()) {
            return it->second;
        }
        FontFaceId id = next_id_++;
        path_to_face_[path] = id;
        return id;
    }

    RasterizedGlyph rasterize(FontFaceId face, uint32_t glyph_index,
                               float /*size*/,
                               SubpixelOffset /*offset*/) override {
        ++rasterize_call_count;
        rasterized_glyphs.emplace(face, glyph_index);

        RasterizedGlyph glyph;
        if (failing_glyphs.count(glyph_index)) {
            // Simulate failure: return zero-sized bitmap.
            glyph.width = 0;
            glyph.height = 0;
            return glyph;
        }

        glyph.width = glyph_width;
        glyph.height = glyph_height;
        glyph.bearing_x = 0;
        glyph.bearing_y = glyph_height;
        glyph.format = PixelFormat::Grayscale;
        glyph.bitmap.resize(
            static_cast<size_t>(glyph_width) * static_cast<size_t>(glyph_height),
            fill_value);
        return glyph;
    }

    FontMetrics getMetrics(FontFaceId /*face*/, float /*size*/) override {
        FontMetrics m{};
        m.cell_width = metric_cell_width;
        m.cell_height = metric_cell_height;
        m.ascent = metric_ascent;
        m.descent = metric_descent;
        m.underline_position = 1.0f;
        m.underline_thickness = 1.0f;
        m.strikethrough_position = metric_ascent * 0.5f;
        m.strikethrough_thickness = 1.0f;
        return m;
    }

    bool isColorGlyph(FontFaceId /*face*/, uint32_t /*glyph_index*/) override {
        return false;
    }

    uint32_t getGlyphIndex(FontFaceId face, char32_t codepoint) override {
        auto it = face_codepoints.find(face);
        if (it != face_codepoints.end()) {
            // Explicit codepoint set for this face.
            if (it->second.count(codepoint)) {
                return static_cast<uint32_t>(codepoint);
            }
            return 0;
        }
        // Default: every non-zero codepoint is supported.
        return codepoint != 0 ? static_cast<uint32_t>(codepoint) : 0;
    }

    // -- Helpers ------------------------------------------------------------

    /// Register a face ID directly (useful to pre-populate face_codepoints
    /// before any loadFont call).
    FontFaceId registerFace(const std::string& path) {
        return loadFont(path, 0, 0.0f);
    }

    /// Check whether a specific glyph was rasterized at least once.
    bool wasRasterized(FontFaceId face, uint32_t glyph_index) const {
        return rasterized_glyphs.count({face, glyph_index}) > 0;
    }

    /// Reset all observation counters (does NOT clear configuration).
    void resetCounters() {
        rasterize_call_count = 0;
        rasterized_glyphs.clear();
        load_call_count = 0;
    }

private:
    FontFaceId next_id_ = 1;
    std::unordered_map<std::string, FontFaceId> path_to_face_;
};

// ---------------------------------------------------------------------------
// MockFontDiscovery
// ---------------------------------------------------------------------------
// Configurable mock that returns fonts from an in-memory registry.
// Supports lookup by family name/style and fallback by codepoint range.

class MockFontDiscovery : public IFontDiscovery {
public:
    // -- Configuration ------------------------------------------------------

    /// Register a font descriptor that findFonts() can return.
    void addFont(const FontDescriptor& desc) {
        fonts_.push_back(desc);
    }

    /// Register a codepoint range -> font fallback mapping.
    struct FallbackRange {
        char32_t lo;
        char32_t hi;
        FontDescriptor descriptor;
    };
    void addFallbackRange(char32_t lo, char32_t hi,
                          const FontDescriptor& desc) {
        fallbacks_.push_back({lo, hi, desc});
    }

    /// The descriptor returned by defaultMonospace().
    FontDescriptor default_monospace;

    /// If true, findFonts() always returns empty (simulates "no fonts found").
    bool simulate_no_fonts = false;

    /// If true, findFallback() always returns empty descriptor.
    bool simulate_no_fallback = false;

    // -- IFontDiscovery implementation --------------------------------------

    std::vector<FontDescriptor> findFonts(const FontQuery& query) override {
        if (simulate_no_fonts) return {};

        std::vector<FontDescriptor> results;
        for (const auto& desc : fonts_) {
            if (desc.family == query.family && desc.style == query.style) {
                results.push_back(desc);
            }
        }
        return results;
    }

    FontDescriptor findFallback(char32_t codepoint,
                                FontStyle /*style*/) override {
        if (simulate_no_fallback) return {};

        for (const auto& fb : fallbacks_) {
            if (codepoint >= fb.lo && codepoint <= fb.hi) {
                return fb.descriptor;
            }
        }
        return {};  // Empty descriptor = no fallback found
    }

    FontDescriptor defaultMonospace() override {
        return default_monospace;
    }

private:
    std::vector<FontDescriptor> fonts_;
    std::vector<FallbackRange> fallbacks_;
};

} // namespace termcore::test

#endif // TESTS_MOCK_FONT_H
