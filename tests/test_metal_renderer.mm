#import <gtest/gtest.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import "MetalAtlasUploader.h"
#import "MetalTextRenderer.h"
#import "termcore/font/glyph_atlas.h"
#import "termcore/screen.h"

using namespace termcore;

// Macro to get a Metal device and skip the test if unavailable.
#define GET_METAL_DEVICE_OR_SKIP(varName) \
    id<MTLDevice> varName = MTLCreateSystemDefaultDevice(); \
    if (!varName) { GTEST_SKIP() << "No Metal device available"; }

// =============================================================================
// MetalAtlasUploader tests
// =============================================================================

TEST(MetalAtlasUploader, CanBeCreated) {
    GET_METAL_DEVICE_OR_SKIP(device);
    MetalAtlasUploader uploader(device);
    // Should not crash; texture should be nil before any upload
    EXPECT_EQ(uploader.textureForFormat(AtlasFormat::R8), nil);
    EXPECT_EQ(uploader.textureForFormat(AtlasFormat::BGRA), nil);
}

TEST(MetalAtlasUploader, UploadCreatesTexture) {
    GET_METAL_DEVICE_OR_SKIP(device);
    MetalAtlasUploader uploader(device);

    GlyphAtlas atlas(256, 2048);

    // Create a fake rasterized glyph to make the R8 page dirty
    RasterizedGlyph glyph;
    glyph.width = 8;
    glyph.height = 12;
    glyph.bearing_x = 0;
    glyph.bearing_y = 10;
    glyph.format = PixelFormat::Grayscale;
    glyph.bitmap.resize(8 * 12, 128); // 8x12 grayscale

    auto region = atlas.pack(glyph);
    ASSERT_TRUE(region.has_value());
    EXPECT_TRUE(atlas.anyDirty());

    uploader.upload(atlas);

    id<MTLTexture> r8Tex = uploader.textureForFormat(AtlasFormat::R8);
    ASSERT_NE(r8Tex, nil);
    EXPECT_EQ(r8Tex.width, 256u);
    EXPECT_EQ(r8Tex.height, 256u);
    EXPECT_EQ(r8Tex.pixelFormat, MTLPixelFormatR8Unorm);

    // After upload, atlas should no longer be dirty
    EXPECT_FALSE(atlas.anyDirty());
}

TEST(MetalAtlasUploader, UploadBGRACreatesTexture) {
    GET_METAL_DEVICE_OR_SKIP(device);
    MetalAtlasUploader uploader(device);

    GlyphAtlas atlas(256, 2048);

    RasterizedGlyph glyph;
    glyph.width = 16;
    glyph.height = 16;
    glyph.bearing_x = 0;
    glyph.bearing_y = 14;
    glyph.format = PixelFormat::BGRA;
    glyph.bitmap.resize(16 * 16 * 4, 200);

    auto region = atlas.pack(glyph);
    ASSERT_TRUE(region.has_value());

    uploader.upload(atlas);

    id<MTLTexture> bgraTex = uploader.textureForFormat(AtlasFormat::BGRA);
    ASSERT_NE(bgraTex, nil);
    EXPECT_EQ(bgraTex.pixelFormat, MTLPixelFormatBGRA8Unorm_sRGB);
}

// =============================================================================
// MetalTextRenderer tests
// =============================================================================

TEST(MetalTextRenderer, CanBeCreated) {
    GET_METAL_DEVICE_OR_SKIP(device);

    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.drawableSize = CGSizeMake(800, 600);

    MetalTextRenderer renderer(device, layer);
    // Should not crash
}

TEST(MetalTextRenderer, ResizeDoesNotCrash) {
    GET_METAL_DEVICE_OR_SKIP(device);

    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.drawableSize = CGSizeMake(800, 600);

    MetalTextRenderer renderer(device, layer);
    renderer.resize(1024, 768);
    // Should not crash
}

TEST(MetalTextRenderer, RenderWithNoFontStackDoesNotCrash) {
    GET_METAL_DEVICE_OR_SKIP(device);

    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.drawableSize = CGSizeMake(800, 600);

    MetalTextRenderer renderer(device, layer);
    renderer.resize(800, 600);

    Screen screen(24, 80);
    // Render with no font stack set — should return early gracefully
    renderer.render(screen);
}
