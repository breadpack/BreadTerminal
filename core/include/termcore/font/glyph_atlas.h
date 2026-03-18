#ifndef TERMCORE_GLYPH_ATLAS_H
#define TERMCORE_GLYPH_ATLAS_H

#include "font_metrics.h"
#include "i_font_rasterizer.h"
#include <cstdint>
#include <vector>
#include <optional>

namespace termcore {

/// UV coordinates for a glyph in the atlas
struct GlyphRegion {
    int atlas_index;    // Which atlas (0=R8, 1=BGRA, 2=RGB)
    int x, y;           // Top-left corner in atlas
    int width, height;  // Size in pixels
    int bearing_x, bearing_y;  // Glyph bearing for positioning
};

/// Atlas pixel formats correspond to separate atlas textures
enum class AtlasFormat : uint8_t {
    R8 = 0,        // Grayscale text
    BGRA = 1,      // Color emoji
    RGB = 2,        // ClearType subpixel (Windows)
    Count = 3
};

/// A single atlas texture (CPU-side)
class AtlasPage {
public:
    AtlasPage(int width, int height, AtlasFormat format);

    /// Try to pack a glyph into this page. Returns region if successful.
    std::optional<GlyphRegion> pack(int glyph_width, int glyph_height,
                                     int bearing_x, int bearing_y);

    /// Write bitmap data into a packed region.
    void blit(const GlyphRegion& region, const uint8_t* data, int data_stride);

    /// Get raw pixel data.
    const uint8_t* data() const { return pixels_.data(); }
    int width() const { return width_; }
    int height() const { return height_; }
    AtlasFormat format() const { return format_; }

    /// Check if the atlas has been modified since last query (for GPU upload).
    bool isDirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }

    /// Expand the atlas to new dimensions, preserving existing content.
    void expand(int new_width, int new_height);

private:
    /// Guillotine bin packing: free rectangles
    struct FreeRect {
        int x, y, width, height;
    };

    /// Split a free rect after placing a glyph (guillotine split).
    void splitRect(size_t rect_index, int placed_width, int placed_height);

    /// Find best fitting free rect (best-fit by Y, then by area).
    std::optional<size_t> findBestFit(int width, int height);

    int width_;
    int height_;
    AtlasFormat format_;
    std::vector<uint8_t> pixels_;
    std::vector<FreeRect> free_rects_;
    bool dirty_ = false;
};

/// Manages multiple atlas pages (one per format, expandable).
class GlyphAtlas {
public:
    /// Initial atlas size (default 512x512), max size for expansion.
    GlyphAtlas(int initial_size = 512, int max_size = 4096);

    /// Pack a rasterized glyph into the appropriate atlas.
    /// Adds 1px border around the glyph to prevent texture bleed.
    /// Returns the region, or nullopt if all atlases are full.
    std::optional<GlyphRegion> pack(const RasterizedGlyph& glyph);

    /// Get atlas page by format index.
    const AtlasPage* getPage(AtlasFormat format) const;
    AtlasPage* getPage(AtlasFormat format);

    /// Get total number of pages.
    size_t pageCount() const { return pages_.size(); }

    /// Check if any page is dirty (needs GPU re-upload).
    bool anyDirty() const;

    /// Clear all dirty flags.
    void clearAllDirty();

    /// Get the atlas format for a given pixel format.
    static AtlasFormat formatForPixelFormat(PixelFormat pf);

private:
    AtlasPage& getOrCreatePage(AtlasFormat format);
    AtlasPage& expandPage(AtlasFormat format);

    int initial_size_;
    int max_size_;
    std::vector<AtlasPage> pages_;
};

} // namespace termcore
#endif
