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
/// Metal aligns float4 to 16 bytes, making struct stride 80 bytes.
/// C++ struct must match by adding explicit padding after flags.
struct CellInstance {
    float position[2];     // offset 0,  8 bytes
    float atlas_uv[2];     // offset 8,  8 bytes
    float atlas_size[2];   // offset 16, 8 bytes
    float glyph_offset[2]; // offset 24, 8 bytes
    float fg_color[4];     // offset 32, 16 bytes
    float bg_color[4];     // offset 48, 16 bytes
    uint32_t flags;        // offset 64, 4 bytes
    uint32_t _pad[3];      // offset 68, 12 bytes → total 80 bytes (matches Metal)
};

/// GPU uniform data — must match Uniforms in cell.metal.
struct CellUniforms {
    float viewport_size[2];  // pixels (drawableSize)
    float cell_size[2];      // pixels
    float atlas_size[2];     // pixels (atlas texture dimensions)
    float ascent;            // pixels — for baseline positioning
    float _pad;
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
