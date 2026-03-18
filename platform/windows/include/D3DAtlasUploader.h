#ifndef TERMCORE_D3D_ATLAS_UPLOADER_H
#define TERMCORE_D3D_ATLAS_UPLOADER_H

#include "termcore/font/glyph_atlas.h"
#include <cstdint>
#include <memory>

#if defined(_WIN32)
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
#endif

namespace termcore {

/// D3D11 atlas texture manager.
/// Uploads GlyphAtlas pages to D3D11 textures for rendering.
class D3DAtlasUploader {
public:
    D3DAtlasUploader();
    ~D3DAtlasUploader();

    D3DAtlasUploader(const D3DAtlasUploader&) = delete;
    D3DAtlasUploader& operator=(const D3DAtlasUploader&) = delete;

#if defined(_WIN32)
    /// Set the D3D11 device and context for texture management.
    void setDevice(ID3D11Device* device, ID3D11DeviceContext* context);

    /// Upload all dirty atlas pages to GPU textures.
    void upload(GlyphAtlas& atlas);

    /// Get the shader resource view for a given atlas format.
    ID3D11ShaderResourceView* srvForFormat(AtlasFormat format) const;
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termcore
#endif
