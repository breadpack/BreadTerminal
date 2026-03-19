#import "MetalAtlasUploader.h"
#import "termcore/font/glyph_atlas.h"
#import <Metal/Metal.h>
#import <array>

namespace termcore {

struct MetalAtlasUploader::Impl {
    id<MTLDevice> device;

    // One texture per AtlasFormat (R8, BGRA, RGB)
    static constexpr size_t kMaxFormats = static_cast<size_t>(AtlasFormat::Count);
    std::array<id<MTLTexture>, kMaxFormats> textures = {};
    std::array<int, kMaxFormats> textureWidths = {};
    std::array<int, kMaxFormats> textureHeights = {};

    explicit Impl(id<MTLDevice> dev) : device(dev) {}

    static MTLPixelFormat metalPixelFormat(AtlasFormat format) {
        switch (format) {
            case AtlasFormat::R8:   return MTLPixelFormatR8Unorm;
            case AtlasFormat::BGRA: return MTLPixelFormatBGRA8Unorm_sRGB;
            case AtlasFormat::RGB:  return MTLPixelFormatRGBA8Unorm;
            default:                return MTLPixelFormatR8Unorm;
        }
    }

    static int bytesPerPixel(AtlasFormat format) {
        switch (format) {
            case AtlasFormat::R8:   return 1;
            case AtlasFormat::BGRA: return 4;
            case AtlasFormat::RGB:  return 4; // Stored as RGBA on GPU
            default:                return 1;
        }
    }

    void uploadPage(AtlasPage& page) {
        auto fmt = page.format();
        auto idx = static_cast<size_t>(fmt);

        int pageW = page.width();
        int pageH = page.height();
        int bpp = bytesPerPixel(fmt);
        bool textureRecreated = false;

        // Recreate texture if dimensions changed (atlas expanded)
        if (!textures[idx] ||
            textureWidths[idx] != pageW ||
            textureHeights[idx] != pageH) {

            MTLTextureDescriptor* desc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:metalPixelFormat(fmt)
                                             width:pageW
                                            height:pageH
                                         mipmapped:NO];
            desc.usage = MTLTextureUsageShaderRead;
            desc.storageMode = MTLStorageModeShared;

            textures[idx] = [device newTextureWithDescriptor:desc];
            textureWidths[idx] = pageW;
            textureHeights[idx] = pageH;
            textureRecreated = true;
        }

        // Use dirty rect to upload only the modified region
        auto dirty = page.dirtyRect();
        if (textureRecreated || dirty.width <= 0 || dirty.height <= 0
            || page.isFullDirty()) {
            // Full upload needed (texture recreated or full dirty)
            MTLRegion region = MTLRegionMake2D(0, 0, pageW, pageH);
            [textures[idx] replaceRegion:region
                             mipmapLevel:0
                               withBytes:page.data()
                             bytesPerRow:pageW * bpp];
        } else {
            // Partial upload: only the dirty bounding box
            MTLRegion region = MTLRegionMake2D(dirty.x, dirty.y,
                                                dirty.width, dirty.height);
            // Compute pointer to the first row of the dirty region
            const uint8_t* src = page.data()
                + static_cast<size_t>(dirty.y) * pageW * bpp
                + static_cast<size_t>(dirty.x) * bpp;
            [textures[idx] replaceRegion:region
                             mipmapLevel:0
                               withBytes:src
                             bytesPerRow:pageW * bpp];
        }

        page.clearDirty();
    }
};

MetalAtlasUploader::MetalAtlasUploader(id<MTLDevice> device)
    : impl_(std::make_unique<Impl>(device)) {}

MetalAtlasUploader::~MetalAtlasUploader() = default;

void MetalAtlasUploader::upload(GlyphAtlas& atlas) {
    // Upload each dirty page
    for (size_t i = 0; i < static_cast<size_t>(AtlasFormat::Count); ++i) {
        auto fmt = static_cast<AtlasFormat>(i);
        AtlasPage* page = atlas.getPage(fmt);
        if (page && page->isDirty()) {
            impl_->uploadPage(*page);
        }
    }
}

id<MTLTexture> MetalAtlasUploader::textureForFormat(AtlasFormat format) const {
    auto idx = static_cast<size_t>(format);
    if (idx < Impl::kMaxFormats) {
        return impl_->textures[idx];
    }
    return nil;
}

} // namespace termcore
