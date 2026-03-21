#ifndef TERMCORE_METAL_ATLAS_UPLOADER_H
#define TERMCORE_METAL_ATLAS_UPLOADER_H

#include "termcore/i_text_renderer.h"
#include <cstdint>
#include <memory>

#ifdef __OBJC__
#import <Metal/Metal.h>
#endif

namespace termcore {

class GlyphAtlas;
enum class AtlasFormat : uint8_t;

/// Uploads CPU-side GlyphAtlas pages to Metal textures.
/// Each AtlasFormat (R8, BGRA) gets its own MTLTexture.
class MetalAtlasUploader : public IAtlasUploader {
public:
#ifdef __OBJC__
    explicit MetalAtlasUploader(id<MTLDevice> device);
#endif
    ~MetalAtlasUploader();

    // Non-copyable
    MetalAtlasUploader(const MetalAtlasUploader&) = delete;
    MetalAtlasUploader& operator=(const MetalAtlasUploader&) = delete;

    /// Upload dirty atlas pages to Metal textures.
    void upload(GlyphAtlas& atlas) override;

#ifdef __OBJC__
    /// Get the Metal texture for an atlas format.
    id<MTLTexture> textureForFormat(AtlasFormat format) const;
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termcore

#endif // TERMCORE_METAL_ATLAS_UPLOADER_H
