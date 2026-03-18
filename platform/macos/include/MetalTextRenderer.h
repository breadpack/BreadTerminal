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

/// GPU cell instance data — must match CellInstance in cell.metal.
struct CellInstance {
    float position[2];     // Screen position (top-left of cell, in pixels)
    float atlas_uv[2];     // Top-left UV in atlas (in pixels)
    float atlas_size[2];   // Size of glyph in atlas (pixels)
    float glyph_offset[2]; // Bearing offset within cell
    float fg_color[4];     // Foreground color (RGBA, 0-1)
    float bg_color[4];     // Background color (RGBA, 0-1)
    uint32_t flags;        // Bit flags: bit0=has_glyph, bit1=is_color
};

/// GPU uniform data — must match Uniforms in cell.metal.
struct CellUniforms {
    float viewport_size[2];
    float cell_size[2];
    float atlas_size[2];
};

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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termcore

#endif // TERMCORE_METAL_TEXT_RENDERER_H
