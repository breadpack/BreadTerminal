#ifndef TERMCORE_I_TEXT_RENDERER_H
#define TERMCORE_I_TEXT_RENDERER_H

#include <cstdint>
#include <cstddef>

namespace termcore {

// Forward declarations
class Screen;
class FontCollection;
class GlyphCache;
class GlyphAtlas;
class IFontRasterizer;

/// Atlas upload abstraction.
/// Each platform uploads atlas page data to GPU textures in its own way
/// (Metal textures, D3D11 SRVs, GL textures).  This interface lets core
/// code trigger uploads without knowing the backend.
class IAtlasUploader {
public:
    virtual ~IAtlasUploader() = default;

    /// Upload all dirty atlas pages to GPU textures.
    virtual void upload(GlyphAtlas& atlas) = 0;
};

/// Text renderer abstraction.
///
/// Captures the common operations shared by all platform renderers
/// (D3DTextRenderer, MetalTextRenderer, GLTextRenderer).  Platform-specific
/// features (tab bars, pane borders, URL highlights, etc.) remain on the
/// concrete renderer classes -- this interface covers only the cross-platform
/// contract needed to drive a terminal rendering loop.
class ITextRenderer {
public:
    virtual ~ITextRenderer() = default;

    // -- Font stack ----------------------------------------------------------

    /// Set the font collection and related objects for rendering.
    /// The renderer does NOT take ownership of these pointers.
    virtual void setFontStack(FontCollection* collection,
                              GlyphCache* cache,
                              GlyphAtlas* atlas,
                              IFontRasterizer* rasterizer) = 0;

    // -- Frame rendering -----------------------------------------------------

    /// Render one frame: read Screen data, build instance buffer, draw.
    virtual void render(const Screen& screen) = 0;

    // -- Viewport ------------------------------------------------------------

    /// Handle viewport resize (dimensions in physical pixels).
    virtual void resize(float width, float height) = 0;

    // -- Dirty tracking ------------------------------------------------------

    /// Mark content as dirty so the next render does a full rebuild.
    /// Renderers that don't do incremental updates may implement this as a
    /// no-op.
    virtual void markContentDirty() {}

    // -- Atlas ---------------------------------------------------------------

    /// Return the platform atlas uploader (may be nullptr before init).
    virtual IAtlasUploader* atlasUploader() = 0;
};

} // namespace termcore

#endif // TERMCORE_I_TEXT_RENDERER_H
