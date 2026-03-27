#ifndef TERMCORE_GLYPH_CACHE_H
#define TERMCORE_GLYPH_CACHE_H

#include "font_metrics.h"
#include "glyph_atlas.h"
#include "i_font_rasterizer.h"
#include <cstdint>
#include <list>
#include <optional>
#include <unordered_map>

namespace termcore {

/// Cached glyph information: atlas location + rendering metrics
struct GlyphInfo {
    GlyphRegion region;       // Where in the atlas
    float advance_x;          // Horizontal advance in pixels
    float advance_y;          // Vertical advance in pixels
    bool is_color;            // Whether this is a color (emoji) glyph
    uint32_t last_used_generation = 0;  // Generation when last accessed
    bool needs_rerasterize = false;     // True after atlas reset (region is stale)
};

/// LRU glyph cache with atlas integration.
class GlyphCache {
public:
    /// max_entries: maximum number of cached glyphs before LRU eviction
    explicit GlyphCache(size_t max_entries = 8192);

    /// Look up a glyph. Returns cached info if available.
    std::optional<GlyphInfo> get(const GlyphKey& key);

    /// Insert a glyph into the cache.
    void put(const GlyphKey& key, const GlyphInfo& info);

    /// Rasterize and cache a glyph using the provided rasterizer and atlas.
    /// This is the main entry point: check cache, rasterize if miss, pack into atlas, cache.
    std::optional<GlyphInfo> getOrRasterize(const GlyphKey& key,
                                             float size,
                                             IFontRasterizer& rasterizer,
                                             GlyphAtlas& atlas);

    /// Precache ASCII range (32-126) for a font face in all 4 styles.
    /// For terminals, this avoids cache misses for common characters.
    void precacheAscii(FontFaceId face_id, float size,
                       IFontRasterizer& rasterizer,
                       GlyphAtlas& atlas);

    /// Clear all cached entries.
    void clear();

    /// Current number of cached entries.
    size_t size() const { return cache_.size(); }

    /// Cache hit statistics
    uint64_t hits() const { return hits_; }
    uint64_t misses() const { return misses_; }
    void resetStats() { hits_ = 0; misses_ = 0; }

    /// Current generation (incremented on atlas compaction).
    uint32_t generation() const { return current_generation_; }

private:
    void evict();  // Remove least recently used entry

    /// Generation-based compaction: evict old entries and mark survivors
    /// as needing re-rasterization (their atlas regions are stale).
    void compactForAtlasReset();

    size_t max_entries_;
    uint32_t current_generation_ = 0;

    // LRU list: front = most recently used, back = least recently used
    using LruList = std::list<std::pair<GlyphKey, GlyphInfo>>;
    LruList lru_list_;

    // Hash map for O(1) lookup
    std::unordered_map<GlyphKey, LruList::iterator> cache_;

    // Stats
    uint64_t hits_ = 0;
    uint64_t misses_ = 0;
};

} // namespace termcore
#endif
