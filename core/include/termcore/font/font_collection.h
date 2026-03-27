#ifndef TERMCORE_FONT_COLLECTION_H
#define TERMCORE_FONT_COLLECTION_H

#include "font_metrics.h"
#include "i_font_rasterizer.h"
#include "i_font_discovery.h"
#include "font_shaper.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace termcore {

/// Represents a loaded font in the fallback chain
struct FontEntry {
    FontFaceId rasterizer_face_id = kInvalidFontFace;   // ID from IFontRasterizer
    FontFaceId shaper_face_id = kInvalidFontFace;        // ID from FontShaper
    FontDescriptor descriptor;
    float scale_factor = 1.0f;  // Scale to match primary font's x-height
    bool loaded = false;
};

/// Opaque ID returned by FontCollection::resolveFace().
/// This is a collection-internal index, NOT a rasterizer or shaper FontFaceId.
/// Must only be used with FontCollection accessor methods.
using CollectionFaceId = uint32_t;
static constexpr CollectionFaceId kInvalidCollectionFace = UINT32_MAX;

/// Manages a font fallback chain for terminal text rendering.
/// Fallback order: primary -> bold/italic variants -> symbol fonts -> system fallback -> emoji
class FontCollection {
public:
    FontCollection(IFontRasterizer& rasterizer,
                   IFontDiscovery& discovery,
                   FontShaper& shaper);
    ~FontCollection() = default;

    /// Set the primary font by family name and size.
    bool setPrimaryFont(const std::string& family, float size);

    /// Set the primary font by file path.
    bool setPrimaryFontFromFile(const std::string& path, int face_index, float size);

    /// Add a font to the fallback chain explicitly.
    void addFallbackFont(const std::string& family);
    void addFallbackFontFromFile(const std::string& path, int face_index = 0);

    /// Get font size.
    float fontSize() const { return size_; }

    /// Set font size (updates all loaded fonts in-place).
    void setFontSize(float size);

    /// Get metrics for the primary font.
    FontMetrics primaryMetrics() const;

    /// Resolve a codepoint to the best font in the collection.
    /// Returns a CollectionFaceId (NOT a rasterizer/shaper FontFaceId).
    /// Use rasterizerFaceId()/shaperFaceId() to get platform-specific IDs.
    CollectionFaceId resolveFace(char32_t codepoint);

    /// Get rasterizer face ID (for rasterizing glyphs)
    FontFaceId rasterizerFaceId(CollectionFaceId face) const;

    /// Get shaper face ID (for HarfBuzz shaping)
    FontFaceId shaperFaceId(CollectionFaceId face) const;

    /// Get scale factor for a face (for fallback font x-height matching)
    float scaleFactor(CollectionFaceId face) const;

    /// Shape a multi-codepoint grapheme cluster via HarfBuzz and return
    /// the primary glyph index.  Resolves the font from the base codepoint.
    /// Returns {CollectionFaceId, glyph_index}; glyph_index=0 on failure.
    struct ShapedClusterResult {
        CollectionFaceId face = kInvalidCollectionFace;
        uint32_t glyph_index = 0;
    };
    ShapedClusterResult shapeCluster(const std::u32string& codepoints);

    /// Get the number of fonts in the chain.
    size_t chainLength() const { return chain_.size(); }

    /// Access the underlying font shaper (needed for ligature shaping).
    FontShaper& shaper() { return shaper_; }

private:
    /// Ensure a font entry is loaded (lazy loading).
    bool ensureLoaded(FontEntry& entry);

    /// Calculate scale factor to match primary font's x-height.
    float calculateScaleFactor(FontFaceId face_id);

    /// Try system font discovery for a codepoint.
    CollectionFaceId trySystemFallback(char32_t codepoint);

    IFontRasterizer& rasterizer_;
    IFontDiscovery& discovery_;
    FontShaper& shaper_;

    float size_ = 14.0f;
    std::vector<FontEntry> chain_;

    // Cache: codepoint -> index into chain_ (avoid repeated lookups)
    std::unordered_map<char32_t, size_t> codepoint_cache_;
};

} // namespace termcore
#endif
