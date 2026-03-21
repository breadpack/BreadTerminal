#ifndef TERMCORE_GL_ATLAS_UPLOADER_H
#define TERMCORE_GL_ATLAS_UPLOADER_H

#include "termcore/font/glyph_atlas.h"
#include "termcore/i_text_renderer.h"
#include <cstdint>
#include <memory>

namespace termcore {

/// OpenGL atlas texture manager.
/// Uploads GlyphAtlas pages to GL textures for rendering.
class GLAtlasUploader : public IAtlasUploader {
public:
    GLAtlasUploader();
    ~GLAtlasUploader();

    GLAtlasUploader(const GLAtlasUploader&) = delete;
    GLAtlasUploader& operator=(const GLAtlasUploader&) = delete;

    /// Upload all dirty atlas pages to GPU textures.
    void upload(GlyphAtlas& atlas) override;

    /// Get the GL texture ID for a given atlas format.
    uint32_t textureForFormat(AtlasFormat format) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termcore
#endif
