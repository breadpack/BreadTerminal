#include "termcore/font/glyph_cache.h"

namespace termcore {

GlyphCache::GlyphCache(size_t max_entries)
    : max_entries_(max_entries) {}

std::optional<GlyphInfo> GlyphCache::get(const GlyphKey& key) {
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        ++misses_;
        return std::nullopt;
    }

    // Move to front of LRU list (most recently used)
    lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
    ++hits_;
    return it->second->second;
}

void GlyphCache::put(const GlyphKey& key, const GlyphInfo& info) {
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        // Update existing entry and move to front
        it->second->second = info;
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
        return;
    }

    // Evict if full
    if (cache_.size() >= max_entries_) {
        evict();
    }

    // Insert at front
    lru_list_.emplace_front(key, info);
    cache_[key] = lru_list_.begin();
}

std::optional<GlyphInfo> GlyphCache::getOrRasterize(
    const GlyphKey& key,
    float size,
    IFontRasterizer& rasterizer,
    GlyphAtlas& atlas) {

    // Check cache first
    auto cached = get(key);
    if (cached.has_value()) {
        return cached;
    }

    // Cache miss — rasterize
    RasterizedGlyph rasterized = rasterizer.rasterize(
        key.face_id, key.glyph_index, size, key.subpixel);

    // Pack into atlas
    auto region = atlas.pack(rasterized);
    if (!region.has_value()) {
        return std::nullopt;  // Atlas full
    }

    // Build GlyphInfo
    GlyphInfo info;
    info.region = region.value();
    info.advance_x = static_cast<float>(rasterized.width + rasterized.bearing_x);
    info.advance_y = 0.0f;
    info.is_color = rasterizer.isColorGlyph(key.face_id, key.glyph_index);

    put(key, info);
    return info;
}

void GlyphCache::precacheAscii(
    FontFaceId face_id,
    float size,
    IFontRasterizer& rasterizer,
    GlyphAtlas& atlas) {

    for (char32_t cp = 32; cp <= 126; ++cp) {
        uint32_t glyph_index = rasterizer.getGlyphIndex(face_id, cp);
        if (glyph_index == 0) {
            continue;  // No glyph for this codepoint
        }

        GlyphKey key;
        key.face_id = face_id;
        key.glyph_index = glyph_index;
        key.subpixel = {0, 0};

        getOrRasterize(key, size, rasterizer, atlas);
    }
}

void GlyphCache::clear() {
    cache_.clear();
    lru_list_.clear();
}

void GlyphCache::evict() {
    if (lru_list_.empty()) {
        return;
    }
    // Remove from hash map
    auto& back = lru_list_.back();
    cache_.erase(back.first);
    // Remove from LRU list
    lru_list_.pop_back();
}

} // namespace termcore
