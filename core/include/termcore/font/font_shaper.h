#ifndef TERMCORE_FONT_SHAPER_H
#define TERMCORE_FONT_SHAPER_H

#include "font_metrics.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declare HarfBuzz types to avoid exposing in header
typedef struct hb_font_t hb_font_t;
typedef struct hb_face_t hb_face_t;
typedef struct hb_blob_t hb_blob_t;

namespace termcore {

/// A single shaped glyph with position info
struct ShapedGlyph {
    uint32_t glyph_index;    // HarfBuzz glyph ID
    FontFaceId face_id;       // Which font face produced this glyph
    int32_t x_advance;       // Horizontal advance in 26.6 fixed point (1/64 px)
    int32_t y_advance;
    int32_t x_offset;        // Offset from advance cursor
    int32_t y_offset;
    uint32_t cluster;         // Index into original codepoint array
};

/// A run of shaped glyphs mapped to terminal cells
struct ShapedRun {
    std::vector<ShapedGlyph> glyphs;
    int start_cell;           // First cell in terminal grid
    int cell_count;           // Number of cells this run occupies
};

/// Configuration for shaping
struct ShaperConfig {
    bool enable_ligatures = true;  // calt feature
    bool enable_liga = true;       // liga feature
    std::vector<std::string> extra_features;  // Additional OpenType features
};

/// HarfBuzz font shaper wrapper.
/// Manages HarfBuzz font objects and performs text shaping.
class FontShaper {
public:
    FontShaper();
    ~FontShaper();

    // Non-copyable
    FontShaper(const FontShaper&) = delete;
    FontShaper& operator=(const FontShaper&) = delete;

    /// Load a font for shaping from a file path.
    /// Returns a face ID, or kInvalidFontFace on failure.
    FontFaceId loadFont(const std::string& font_path, int face_index, float size);

    /// Set font size for an already loaded font.
    void setFontSize(FontFaceId face_id, float size);

    /// Shape a line of codepoints using the specified font.
    /// Returns a list of shaped glyphs with cluster info.
    std::vector<ShapedGlyph> shape(FontFaceId face_id,
                                    const std::u32string& codepoints,
                                    const ShaperConfig& config = {});

    /// Shape and map to terminal cells.
    /// cell_width: width of one terminal cell in pixels.
    /// Returns runs mapped to cell positions.
    std::vector<ShapedRun> shapeForGrid(FontFaceId face_id,
                                         const std::u32string& codepoints,
                                         float cell_width,
                                         const ShaperConfig& config = {});

    /// Get the glyph index for a codepoint (cmap lookup).
    uint32_t getGlyphIndex(FontFaceId face_id, char32_t codepoint);

    /// Check if a font has a glyph for a codepoint.
    bool hasGlyph(FontFaceId face_id, char32_t codepoint);

    /// Clear the shaper result cache (e.g. after font changes).
    void clearShaperCache();

private:
    // --- Shaper result cache ---
    struct ShaperCacheKey {
        FontFaceId face_id;
        uint64_t text_hash;
        uint64_t config_hash;

        bool operator==(const ShaperCacheKey&) const = default;
    };

    struct ShaperCacheKeyHash {
        size_t operator()(const ShaperCacheKey& k) const {
            size_t h = std::hash<uint32_t>{}(k.face_id);
            h ^= std::hash<uint64_t>{}(k.text_hash) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint64_t>{}(k.config_hash) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    struct ShaperCacheEntry {
        std::u32string codepoints;       // for collision verification
        std::vector<ShapedGlyph> result;
    };

    std::unordered_map<ShaperCacheKey, ShaperCacheEntry, ShaperCacheKeyHash> shaper_cache_;
    static constexpr size_t kMaxShaperCacheEntries = 4096;

    static uint64_t hashCodepoints(const std::u32string& codepoints);
    static uint64_t hashShaperConfig(const ShaperConfig& config);
    // --- End shaper result cache ---
    struct FontEntry {
        FontFaceId id;
        hb_blob_t* blob = nullptr;
        hb_face_t* face = nullptr;
        hb_font_t* font = nullptr;
        float size = 0;
    };

    std::vector<FontEntry> fonts_;
    FontFaceId next_id_ = 1;

    FontEntry* findFont(FontFaceId id);
};

} // namespace termcore
#endif
