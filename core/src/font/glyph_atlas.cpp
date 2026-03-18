#include "termcore/font/glyph_atlas.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>

namespace termcore {

// --- AtlasPage ---

static int bytesPerPixel(AtlasFormat fmt) {
    switch (fmt) {
        case AtlasFormat::R8:   return 1;
        case AtlasFormat::BGRA: return 4;
        case AtlasFormat::RGB:  return 3;
        default:                return 1;
    }
}

AtlasPage::AtlasPage(int width, int height, AtlasFormat format)
    : width_(width), height_(height), format_(format)
{
    pixels_.resize(static_cast<size_t>(width) * height * bytesPerPixel(format), 0);
    free_rects_.push_back({0, 0, width, height});
}

std::optional<size_t> AtlasPage::findBestFit(int w, int h) {
    std::optional<size_t> best;
    int best_y = std::numeric_limits<int>::max();
    int best_area = std::numeric_limits<int>::max();

    for (size_t i = 0; i < free_rects_.size(); ++i) {
        const auto& r = free_rects_[i];
        if (r.width >= w && r.height >= h) {
            // Prefer lowest Y, then smallest area
            int area = r.width * r.height;
            if (r.y < best_y || (r.y == best_y && area < best_area)) {
                best = i;
                best_y = r.y;
                best_area = area;
            }
        }
    }
    return best;
}

void AtlasPage::splitRect(size_t rect_index, int pw, int ph) {
    FreeRect original = free_rects_[rect_index];
    // Remove the used rect (swap with last for O(1) removal)
    free_rects_[rect_index] = free_rects_.back();
    free_rects_.pop_back();

    int remain_w = original.width - pw;
    int remain_h = original.height - ph;

    // Guillotine split: prefer splitting along the longer remaining axis.
    // This creates two new free rects from the leftover space.
    if (remain_w > 0 && remain_h > 0) {
        if (remain_w >= remain_h) {
            // Split vertically: right rect gets full height, bottom rect is narrower
            free_rects_.push_back({original.x + pw, original.y, remain_w, original.height});
            free_rects_.push_back({original.x, original.y + ph, pw, remain_h});
        } else {
            // Split horizontally: bottom rect gets full width, right rect is shorter
            free_rects_.push_back({original.x, original.y + ph, original.width, remain_h});
            free_rects_.push_back({original.x + pw, original.y, remain_w, ph});
        }
    } else if (remain_w > 0) {
        free_rects_.push_back({original.x + pw, original.y, remain_w, original.height});
    } else if (remain_h > 0) {
        free_rects_.push_back({original.x, original.y + ph, original.width, remain_h});
    }
}

std::optional<GlyphRegion> AtlasPage::pack(int glyph_width, int glyph_height,
                                             int bearing_x, int bearing_y) {
    // Add 1px border on each side to prevent texture bleed
    int padded_w = glyph_width + 2;
    int padded_h = glyph_height + 2;

    auto best = findBestFit(padded_w, padded_h);
    if (!best) {
        return std::nullopt;
    }

    const auto& rect = free_rects_[*best];
    int rx = rect.x;
    int ry = rect.y;

    splitRect(*best, padded_w, padded_h);

    dirty_ = true;

    // The glyph data goes at (rx+1, ry+1), inside the 1px border
    GlyphRegion region;
    region.atlas_index = static_cast<int>(format_);
    region.x = rx + 1;
    region.y = ry + 1;
    region.width = glyph_width;
    region.height = glyph_height;
    region.bearing_x = bearing_x;
    region.bearing_y = bearing_y;
    return region;
}

void AtlasPage::blit(const GlyphRegion& region, const uint8_t* data, int data_stride) {
    if (!data || region.width <= 0 || region.height <= 0) return;

    int bpp = bytesPerPixel(format_);
    int atlas_stride = width_ * bpp;

    for (int row = 0; row < region.height; ++row) {
        int dst_offset = (region.y + row) * atlas_stride + region.x * bpp;
        int src_offset = row * data_stride;
        std::memcpy(&pixels_[dst_offset], &data[src_offset], region.width * bpp);
    }
    dirty_ = true;
}

void AtlasPage::expand(int new_width, int new_height) {
    assert(new_width >= width_ && new_height >= height_);
    if (new_width == width_ && new_height == height_) return;

    int bpp = bytesPerPixel(format_);
    std::vector<uint8_t> new_pixels(
        static_cast<size_t>(new_width) * new_height * bpp, 0);

    // Copy existing rows
    int old_stride = width_ * bpp;
    int new_stride = new_width * bpp;
    for (int row = 0; row < height_; ++row) {
        std::memcpy(&new_pixels[row * new_stride],
                     &pixels_[row * old_stride],
                     old_stride);
    }

    // Add free rects for the new space
    // Right strip (if width expanded)
    if (new_width > width_) {
        free_rects_.push_back({width_, 0, new_width - width_, new_height});
    }
    // Bottom strip (if height expanded), only covering the original width
    if (new_height > height_) {
        free_rects_.push_back({0, height_, width_, new_height - height_});
    }

    pixels_ = std::move(new_pixels);
    width_ = new_width;
    height_ = new_height;
    dirty_ = true;
}

// --- GlyphAtlas ---

GlyphAtlas::GlyphAtlas(int initial_size, int max_size)
    : initial_size_(initial_size), max_size_(max_size)
{
}

AtlasFormat GlyphAtlas::formatForPixelFormat(PixelFormat pf) {
    switch (pf) {
        case PixelFormat::Grayscale: return AtlasFormat::R8;
        case PixelFormat::BGRA:      return AtlasFormat::BGRA;
        case PixelFormat::RGB:       return AtlasFormat::RGB;
        default:                     return AtlasFormat::R8;
    }
}

AtlasPage& GlyphAtlas::getOrCreatePage(AtlasFormat format) {
    int idx = static_cast<int>(format);
    // Ensure pages_ vector is large enough
    while (static_cast<int>(pages_.size()) <= idx) {
        auto fmt = static_cast<AtlasFormat>(pages_.size());
        pages_.emplace_back(initial_size_, initial_size_, fmt);
    }
    return pages_[idx];
}

AtlasPage& GlyphAtlas::expandPage(AtlasFormat format) {
    auto& page = getOrCreatePage(format);
    int new_w = std::min(page.width() * 2, max_size_);
    int new_h = std::min(page.height() * 2, max_size_);
    if (new_w == page.width() && new_h == page.height()) {
        // Already at max, can't expand
        return page;
    }
    page.expand(new_w, new_h);
    return page;
}

std::optional<GlyphRegion> GlyphAtlas::pack(const RasterizedGlyph& glyph) {
    // Handle empty glyphs gracefully
    if (glyph.width <= 0 || glyph.height <= 0) {
        AtlasFormat fmt = formatForPixelFormat(glyph.format);
        GlyphRegion region{};
        region.atlas_index = static_cast<int>(fmt);
        region.x = 0;
        region.y = 0;
        region.width = 0;
        region.height = 0;
        region.bearing_x = glyph.bearing_x;
        region.bearing_y = glyph.bearing_y;
        return region;
    }

    AtlasFormat fmt = formatForPixelFormat(glyph.format);
    auto& page = getOrCreatePage(fmt);

    // Try to pack
    auto result = page.pack(glyph.width, glyph.height,
                            glyph.bearing_x, glyph.bearing_y);
    if (result) {
        // Blit glyph data
        int bpp = 1;
        switch (fmt) {
            case AtlasFormat::R8:   bpp = 1; break;
            case AtlasFormat::BGRA: bpp = 4; break;
            case AtlasFormat::RGB:  bpp = 3; break;
            default: break;
        }
        page.blit(*result, glyph.bitmap.data(), glyph.width * bpp);
        return result;
    }

    // Try expanding
    int old_w = page.width();
    int old_h = page.height();
    expandPage(fmt);

    if (page.width() == old_w && page.height() == old_h) {
        // Can't expand further
        return std::nullopt;
    }

    // Retry after expansion
    result = page.pack(glyph.width, glyph.height,
                       glyph.bearing_x, glyph.bearing_y);
    if (result) {
        int bpp = 1;
        switch (fmt) {
            case AtlasFormat::R8:   bpp = 1; break;
            case AtlasFormat::BGRA: bpp = 4; break;
            case AtlasFormat::RGB:  bpp = 3; break;
            default: break;
        }
        page.blit(*result, glyph.bitmap.data(), glyph.width * bpp);
    }
    return result;
}

const AtlasPage* GlyphAtlas::getPage(AtlasFormat format) const {
    int idx = static_cast<int>(format);
    if (idx < 0 || static_cast<size_t>(idx) >= pages_.size()) {
        return nullptr;
    }
    return &pages_[idx];
}

AtlasPage* GlyphAtlas::getPage(AtlasFormat format) {
    int idx = static_cast<int>(format);
    if (idx < 0 || static_cast<size_t>(idx) >= pages_.size()) {
        return nullptr;
    }
    return &pages_[idx];
}

bool GlyphAtlas::anyDirty() const {
    for (const auto& page : pages_) {
        if (page.isDirty()) return true;
    }
    return false;
}

void GlyphAtlas::clearAllDirty() {
    for (auto& page : pages_) {
        page.clearDirty();
    }
}

} // namespace termcore
