#ifndef TERMCORE_D3D_TEXT_RENDERER_H
#define TERMCORE_D3D_TEXT_RENDERER_H

#include "termcore/screen.h"
#include "termcore/font/glyph_cache.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/font_collection.h"
#include "termcore/font/i_font_rasterizer.h"
#include <memory>

#if defined(_WIN32)
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain1;
struct ID3D11RenderTargetView;
#endif

namespace termcore {

/// GPU cell instance data -- must match layout in HLSL shaders.
struct D3DCellInstance {
    float position[2];     // Screen position (top-left of cell, in pixels)
    float atlas_uv[2];     // Top-left UV in atlas (in pixels)
    float atlas_size[2];   // Size of glyph in atlas (pixels)
    float glyph_offset[2]; // Bearing offset within cell
    float fg_color[4];     // Foreground color (RGBA, 0-1)
    float bg_color[4];     // Background color (RGBA, 0-1)
    uint32_t flags;        // Bit flags: bit0=has_glyph, bit1=is_color, bit2=is_bg, bit3=is_cursor
};

/// D3D11-based terminal text renderer using instanced draw calls.
class D3DTextRenderer {
public:
    D3DTextRenderer();
    ~D3DTextRenderer();

    D3DTextRenderer(const D3DTextRenderer&) = delete;
    D3DTextRenderer& operator=(const D3DTextRenderer&) = delete;

#if defined(_WIN32)
    /// Initialize D3D11 resources.
    bool initialize(ID3D11Device* device, ID3D11DeviceContext* context);

    /// Set the render target view.
    void setRenderTarget(ID3D11RenderTargetView* rtv);
#endif

    /// Set the font collection and related objects for rendering.
    void setFontStack(FontCollection* collection,
                      GlyphCache* cache,
                      GlyphAtlas* atlas,
                      IFontRasterizer* rasterizer);

    /// Render a frame: read Screen data, build cell buffer, draw.
    void render(const Screen& screen);

    /// Handle viewport resize.
    void resize(float width, float height);

    /// Selection highlight state.
    struct Selection {
        int startRow = 0;
        int startCol = 0;
        int endRow = 0;
        int endCol = 0;
        bool active = false;
    };

    /// Set text selection for highlight rendering.
    void setSelection(const Selection& sel);

    /// Set cursor blink visibility (called by blink timer).
    void setCursorBlink(bool visible);

    /// Search highlight for a range of cells on a row.
    struct SearchHighlight {
        int row;
        int startCol;
        int endCol;       // exclusive
    };

    /// Set search highlights for rendering. currentIndex is the index into
    /// the highlights vector for the "current" match (rendered differently).
    void setSearchHighlights(const std::vector<SearchHighlight>& highlights,
                             int currentIndex);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termcore
#endif
