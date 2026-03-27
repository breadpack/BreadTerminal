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

    auto& info = it->second->second;

    // If entry needs re-rasterization (atlas was reset), treat as miss
    // so caller goes through getOrRasterize path
    if (info.needs_rerasterize) {
        ++misses_;
        return std::nullopt;
    }

    // Move to front of LRU list (most recently used)
    lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
    info.last_used_generation = current_generation_;
    ++hits_;
    return info;
}

void GlyphCache::put(const GlyphKey& key, const GlyphInfo& info) {
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        // Update existing entry and move to front
        it->second->second = info;
        it->second->second.last_used_generation = current_generation_;
        it->second->second.needs_rerasterize = false;
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
        return;
    }

    // Evict if full
    if (cache_.size() >= max_entries_) {
        evict();
    }

    // Insert at front with current generation
    GlyphInfo stamped = info;
    stamped.last_used_generation = current_generation_;
    stamped.needs_rerasterize = false;
    lru_list_.emplace_front(key, stamped);
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

    // Cache miss or stale entry — rasterize
    RasterizedGlyph rasterized = rasterizer.rasterize(
        key.face_id, key.glyph_index, size, key.subpixel);

    // Pack into atlas
    bool atlas_was_reset = false;
    auto region = atlas.pack(rasterized, &atlas_was_reset);

    // If atlas was reset, use generation-based compaction instead of full clear.
    // This evicts old entries and marks recently-used ones for re-rasterization,
    // avoiding a full cache rebuild stall.
    if (atlas_was_reset) {
        compactForAtlasReset();
    }

    if (!region.has_value()) {
        return std::nullopt;  // Atlas full
    }

    // Build GlyphInfo
    GlyphInfo info;
    info.region = region.value();
    // TODO: advance_x should come from font metrics/shaper, not bitmap dimensions.
    // For monospace fonts this is acceptable; fix when integrating with shaper in Phase 3.
    info.advance_x = static_cast<float>(rasterized.bearing_x + rasterized.width);
    info.advance_y = 0.0f;
    info.is_color = rasterizer.isColorGlyph(key.face_id, key.glyph_index);
    info.last_used_generation = current_generation_;
    info.needs_rerasterize = false;

    put(key, info);
    return info;
}

std::optional<GlyphInfo> GlyphCache::tryGet(const GlyphKey& key) {
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        ++misses_;
        return std::nullopt;
    }

    auto& info = it->second->second;

    // If entry needs re-rasterization (atlas was reset), treat as miss
    if (info.needs_rerasterize) {
        ++misses_;
        return std::nullopt;
    }

    // Move to front of LRU list (most recently used)
    lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
    info.last_used_generation = current_generation_;
    ++hits_;
    return info;
}

std::optional<GlyphInfo> GlyphCache::insertFromBackground(
    const GlyphKey& key,
    const RasterizedGlyph& glyph,
    bool is_color,
    GlyphAtlas& atlas) {

    // Pack into atlas
    bool atlas_was_reset = false;
    auto region = atlas.pack(glyph, &atlas_was_reset);

    if (atlas_was_reset) {
        compactForAtlasReset();
    }

    if (!region.has_value()) {
        return std::nullopt;  // Atlas full
    }

    // Build GlyphInfo
    GlyphInfo info;
    info.region = region.value();
    info.advance_x = static_cast<float>(glyph.bearing_x + glyph.width);
    info.advance_y = 0.0f;
    info.is_color = is_color;
    info.last_used_generation = current_generation_;
    info.needs_rerasterize = false;

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

void GlyphCache::compactForAtlasReset() {
    ++current_generation_;

    // Remove entries not used in the previous generation (old glyphs).
    // Keep entries from current_generation_ - 1 or later (recently used).
    // Since we just incremented, recently-used means generation >= current_generation_ - 1.
    auto it = lru_list_.begin();
    while (it != lru_list_.end()) {
        auto& info = it->second;
        if (info.last_used_generation + 1 < current_generation_) {
            // Old entry — evict completely
            cache_.erase(it->first);
            it = lru_list_.erase(it);
        } else {
            // Recent entry — mark as needing re-rasterization since atlas region is stale
            info.needs_rerasterize = true;
            ++it;
        }
    }
}

} // namespace termcore
