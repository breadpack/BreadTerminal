#ifndef TERMCORE_METAL_TEXT_RENDERER_H
#define TERMCORE_METAL_TEXT_RENDERER_H

#include "termcore/screen.h"
#include "termcore/font/glyph_cache.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/font_shaper.h"
#include "termcore/font/font_collection.h"
#include <memory>

#ifdef __OBJC__
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#endif

namespace termcore {

/// GPU cell instance data -- Ghostty-style compact layout (32 bytes).
/// Must match CellInstance in cell.metal exactly.
struct alignas(16) CellInstance {
    uint16_t grid_col;      // Terminal column (0-based)
    uint16_t grid_row;      // Terminal row (0-based)
    uint16_t glyph_x;       // Atlas X position (pixels)
    uint16_t glyph_y;       // Atlas Y position (pixels)
    uint16_t glyph_width;   // Glyph width in atlas (pixels)
    uint16_t glyph_height;  // Glyph height in atlas (pixels)
    int16_t  offset_x;      // Bearing X offset from cell origin (signed pixels)
    int16_t  offset_y;      // Bearing Y offset: ascent - bearing_y (signed pixels)
    uint8_t  fg_r, fg_g, fg_b, fg_a;  // Foreground color
    uint8_t  bg_r, bg_g, bg_b, bg_a;  // Background color
    uint8_t  flags;          // bit0=has_glyph, bit1=is_color, bit2=is_bg_pass
    uint8_t  _pad[3];        // Pad to 32 bytes
};
static_assert(sizeof(CellInstance) == 32, "CellInstance must be exactly 32 bytes");

/// GPU uniform data -- must match Uniforms in cell.metal.
struct CellUniforms {
    float viewport_size[2];   // Physical pixels (drawableSize)
    float cell_size[2];       // Physical pixels (cell_width_px, cell_height_px)
    float atlas_size[2];      // Atlas texture dimensions (pixels)
    float grid_padding[2];    // Top-left padding offset (pixels), can be 0
};
static_assert(sizeof(CellUniforms) == 32, "CellUniforms must be exactly 32 bytes");

/// Metal-based terminal text renderer.
/// Reads Screen data and renders cells using instanced draw calls.
class MetalTextRenderer {
public:
#ifdef __OBJC__
    MetalTextRenderer(id<MTLDevice> device, CAMetalLayer* layer);
#endif
    ~MetalTextRenderer();

    // Non-copyable
    MetalTextRenderer(const MetalTextRenderer&) = delete;
    MetalTextRenderer& operator=(const MetalTextRenderer&) = delete;

    /// Set the font collection and related objects for rendering.
    void setFontStack(FontCollection* collection,
                      GlyphCache* cache,
                      GlyphAtlas* atlas,
                      IFontRasterizer* rasterizer);

    /// Render a frame: read Screen data, build cell buffer, draw.
    void render(const Screen& screen);

    /// Handle viewport resize.
    void resize(float width, float height);

    /// Set background opacity for transparency support (0.0 - 1.0).
    void setBackgroundOpacity(float opacity);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termcore

#endif // TERMCORE_METAL_TEXT_RENDERER_H
