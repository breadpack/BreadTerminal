#ifndef TERMCORE_GL_TEXT_RENDERER_H
#define TERMCORE_GL_TEXT_RENDERER_H

#include "termcore/screen.h"
#include "termcore/font/glyph_cache.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/font_collection.h"
#include "termcore/font/i_font_rasterizer.h"
#include <memory>

namespace termcore {

/// GPU cell instance data -- must match layout in GLSL shaders.
struct GLCellInstance {
    float position[2];     // Screen position (top-left of cell, in pixels)
    float atlas_uv[2];     // Top-left UV in atlas (in pixels)
    float atlas_size[2];   // Size of glyph in atlas (pixels)
    float glyph_offset[2]; // Bearing offset within cell
    float fg_color[4];     // Foreground color (RGBA, 0-1)
    float bg_color[4];     // Background color (RGBA, 0-1)
    uint32_t flags;        // Bit flags: bit0=has_glyph, bit1=is_color
};

/// OpenGL-based terminal text renderer using instanced draw calls.
class GLTextRenderer {
public:
    GLTextRenderer();
    ~GLTextRenderer();

    GLTextRenderer(const GLTextRenderer&) = delete;
    GLTextRenderer& operator=(const GLTextRenderer&) = delete;

    /// Initialize OpenGL resources (call after GL context is current).
    bool initialize();

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
#endif
