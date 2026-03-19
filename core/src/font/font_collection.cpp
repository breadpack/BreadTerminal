#include "termcore/font/font_collection.h"
#include <algorithm>
#include <cmath>

namespace termcore {

FontCollection::FontCollection(IFontRasterizer& rasterizer,
                               IFontDiscovery& discovery,
                               FontShaper& shaper)
    : rasterizer_(rasterizer)
    , discovery_(discovery)
    , shaper_(shaper)
{
}

bool FontCollection::setPrimaryFont(const std::string& family, float size) {
    size_ = size;
    chain_.clear();
    codepoint_cache_.clear();

    // Find the regular variant
    FontQuery query;
    query.family = family;
    query.style = FontStyle::Regular;
    query.weight = 400;

    auto results = discovery_.findFonts(query);
    if (results.empty()) {
        return false;
    }

    // Load primary font (regular)
    const auto& desc = results[0];
    FontEntry primary;
    primary.descriptor = desc;
    primary.rasterizer_face_id = rasterizer_.loadFont(desc.file_path, desc.face_index, size);
    if (primary.rasterizer_face_id == kInvalidFontFace) {
        return false;
    }
    primary.shaper_face_id = shaper_.loadFont(desc.file_path, desc.face_index, size);
    primary.scale_factor = 1.0f;
    primary.loaded = true;
    chain_.push_back(primary);

    // Try to find and add Bold variant
    FontQuery bold_query;
    bold_query.family = family;
    bold_query.style = FontStyle::Bold;
    bold_query.weight = 700;
    auto bold_results = discovery_.findFonts(bold_query);
    if (!bold_results.empty()) {
        FontEntry bold_entry;
        bold_entry.descriptor = bold_results[0];
        bold_entry.loaded = false;
        chain_.push_back(bold_entry);
    }

    // Try to find and add Italic variant
    FontQuery italic_query;
    italic_query.family = family;
    italic_query.style = FontStyle::Italic;
    italic_query.weight = 400;
    auto italic_results = discovery_.findFonts(italic_query);
    if (!italic_results.empty()) {
        FontEntry italic_entry;
        italic_entry.descriptor = italic_results[0];
        italic_entry.loaded = false;
        chain_.push_back(italic_entry);
    }

    // Try to find and add BoldItalic variant
    FontQuery bi_query;
    bi_query.family = family;
    bi_query.style = FontStyle::BoldItalic;
    bi_query.weight = 700;
    auto bi_results = discovery_.findFonts(bi_query);
    if (!bi_results.empty()) {
        FontEntry bi_entry;
        bi_entry.descriptor = bi_results[0];
        bi_entry.loaded = false;
        chain_.push_back(bi_entry);
    }

    return true;
}

bool FontCollection::setPrimaryFontFromFile(const std::string& path, int face_index, float size) {
    size_ = size;
    chain_.clear();
    codepoint_cache_.clear();

    FontEntry primary;
    primary.descriptor.file_path = path;
    primary.descriptor.face_index = face_index;
    primary.rasterizer_face_id = rasterizer_.loadFont(path, face_index, size);
    if (primary.rasterizer_face_id == kInvalidFontFace) {
        return false;
    }
    primary.shaper_face_id = shaper_.loadFont(path, face_index, size);
    primary.scale_factor = 1.0f;
    primary.loaded = true;
    chain_.push_back(primary);

    return true;
}

void FontCollection::addFallbackFont(const std::string& family) {
    FontQuery query;
    query.family = family;
    query.style = FontStyle::Regular;
    query.weight = 400;

    auto results = discovery_.findFonts(query);
    if (!results.empty()) {
        FontEntry entry;
        entry.descriptor = results[0];
        entry.loaded = false;
        chain_.push_back(entry);
    }
}

void FontCollection::addFallbackFontFromFile(const std::string& path, int face_index) {
    FontEntry entry;
    entry.descriptor.file_path = path;
    entry.descriptor.face_index = face_index;
    entry.loaded = false;
    chain_.push_back(entry);
}

void FontCollection::setFontSize(float size) {
    size_ = size;
    codepoint_cache_.clear();

    // Update size for all loaded fonts in-place (no re-loading)
    for (auto& entry : chain_) {
        if (entry.loaded) {
            // Use setFontSize to update existing HarfBuzz font scale
            if (entry.shaper_face_id != kInvalidFontFace) {
                shaper_.setFontSize(entry.shaper_face_id, size);
            }
            // Rasterizer needs re-load (no resize API) — reuse same entry
            entry.rasterizer_face_id = rasterizer_.loadFont(
                entry.descriptor.file_path, entry.descriptor.face_index, size);

            // Recalculate scale factor (skip primary which is always index 0)
            if (&entry != &chain_[0]) {
                entry.scale_factor = calculateScaleFactor(entry.rasterizer_face_id);
            }
        }
    }
}

FontMetrics FontCollection::primaryMetrics() const {
    if (chain_.empty() || !chain_[0].loaded) {
        return {};
    }
    return rasterizer_.getMetrics(chain_[0].rasterizer_face_id, size_);
}

CollectionFaceId FontCollection::resolveFace(char32_t codepoint) {
    // Check cache first
    auto it = codepoint_cache_.find(codepoint);
    if (it != codepoint_cache_.end()) {
        return static_cast<CollectionFaceId>(it->second);
    }

    // Walk the chain
    for (size_t i = 0; i < chain_.size(); ++i) {
        auto& entry = chain_[i];
        if (!ensureLoaded(entry)) {
            continue;
        }

        uint32_t glyph_index = rasterizer_.getGlyphIndex(entry.rasterizer_face_id, codepoint);
        if (glyph_index != 0) {
            codepoint_cache_[codepoint] = i;
            return static_cast<CollectionFaceId>(i);
        }
    }

    // Try system fallback
    CollectionFaceId fallback = trySystemFallback(codepoint);
    if (fallback != kInvalidCollectionFace) {
        return fallback;
    }

    // Return primary face as last resort
    if (!chain_.empty()) {
        codepoint_cache_[codepoint] = 0;
        return 0;
    }

    return kInvalidCollectionFace;
}

FontFaceId FontCollection::rasterizerFaceId(CollectionFaceId face) const {
    size_t idx = static_cast<size_t>(face);
    if (idx >= chain_.size()) {
        return kInvalidFontFace;
    }
    return chain_[idx].rasterizer_face_id;
}

FontFaceId FontCollection::shaperFaceId(CollectionFaceId face) const {
    size_t idx = static_cast<size_t>(face);
    if (idx >= chain_.size()) {
        return kInvalidFontFace;
    }
    return chain_[idx].shaper_face_id;
}

float FontCollection::scaleFactor(CollectionFaceId face) const {
    size_t idx = static_cast<size_t>(face);
    if (idx >= chain_.size()) {
        return 1.0f;
    }
    return chain_[idx].scale_factor;
}

bool FontCollection::ensureLoaded(FontEntry& entry) {
    if (entry.loaded) {
        return true;
    }

    if (entry.descriptor.file_path.empty()) {
        return false;
    }

    entry.rasterizer_face_id = rasterizer_.loadFont(
        entry.descriptor.file_path, entry.descriptor.face_index, size_);
    if (entry.rasterizer_face_id == kInvalidFontFace) {
        return false;
    }

    entry.shaper_face_id = shaper_.loadFont(
        entry.descriptor.file_path, entry.descriptor.face_index, size_);

    // Calculate scale factor relative to primary font
    if (!chain_.empty() && &entry != &chain_[0]) {
        entry.scale_factor = calculateScaleFactor(entry.rasterizer_face_id);
    }

    entry.loaded = true;
    return true;
}

float FontCollection::calculateScaleFactor(FontFaceId face_id) {
    if (chain_.empty() || !chain_[0].loaded) {
        return 1.0f;
    }

    FontMetrics primary = rasterizer_.getMetrics(chain_[0].rasterizer_face_id, size_);
    FontMetrics fallback = rasterizer_.getMetrics(face_id, size_);

    if (fallback.ascent <= 0.0f) {
        return 1.0f;
    }

    float scale = primary.ascent / fallback.ascent;

    // Clamp to reasonable range
    scale = std::clamp(scale, 0.7f, 1.3f);

    return scale;
}

CollectionFaceId FontCollection::trySystemFallback(char32_t codepoint) {
    FontStyle style = FontStyle::Regular;
    if (!chain_.empty()) {
        style = chain_[0].descriptor.style;
    }

    FontDescriptor desc = discovery_.findFallback(codepoint, style);
    if (desc.file_path.empty()) {
        fprintf(stderr, "[FONT FALLBACK] U+%04X: no fallback found\n", (unsigned)codepoint);
        return kInvalidCollectionFace;
    }

    fprintf(stderr, "[FONT FALLBACK] U+%04X: found '%s' at '%s'\n",
            (unsigned)codepoint, desc.family.c_str(), desc.file_path.c_str());

    // Check if we already have this font in the chain
    for (size_t i = 0; i < chain_.size(); ++i) {
        if (chain_[i].descriptor.file_path == desc.file_path &&
            chain_[i].descriptor.face_index == desc.face_index) {
            // Already in chain — but check if glyph is actually there
            uint32_t gi = rasterizer_.getGlyphIndex(chain_[i].rasterizer_face_id, codepoint);
            if (gi != 0) {
                codepoint_cache_[codepoint] = i;
                return static_cast<CollectionFaceId>(i);
            }
            codepoint_cache_[codepoint] = 0;
            return kInvalidCollectionFace;
        }
    }

    // Add new font to chain
    FontEntry entry;
    entry.descriptor = desc;
    if (!ensureLoaded(entry)) {
        return kInvalidCollectionFace;
    }

    // Verify it actually has the glyph
    uint32_t glyph_index = rasterizer_.getGlyphIndex(entry.rasterizer_face_id, codepoint);
    if (glyph_index == 0) {
        codepoint_cache_[codepoint] = 0;
        return kInvalidCollectionFace;
    }

    chain_.push_back(entry);
    size_t idx = chain_.size() - 1;
    codepoint_cache_[codepoint] = idx;
    return static_cast<CollectionFaceId>(idx);
}

} // namespace termcore
