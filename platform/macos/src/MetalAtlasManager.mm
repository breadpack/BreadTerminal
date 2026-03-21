#import "MetalTextRendererImpl.h"

namespace termcore {

// ---------------------------------------------------------------------------
// WCAG 2.0 minimum contrast helpers
// ---------------------------------------------------------------------------

float linearize(float srgb) {
    return srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
}

float relativeLuminance(uint32_t color) {
    float r = linearize(((color >> 16) & 0xFF) / 255.0f);
    float g = linearize(((color >> 8) & 0xFF) / 255.0f);
    float b = linearize((color & 0xFF) / 255.0f);
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

float contrastRatio(uint32_t fg, uint32_t bg) {
    float l1 = relativeLuminance(fg);
    float l2 = relativeLuminance(bg);
    if (l1 < l2) std::swap(l1, l2);
    return (l1 + 0.05f) / (l2 + 0.05f);
}

uint32_t ensureContrast(uint32_t fg, uint32_t bg, float minRatio) {
    if (contrastRatio(fg, bg) >= minRatio) return fg;

    float bgL = relativeLuminance(bg);

    // Try lightening the foreground first, then darkening
    // We adjust the foreground luminance to meet the contrast requirement
    // Target luminance: (targetL + 0.05) / (bgL + 0.05) = minRatio
    //   => targetL = minRatio * (bgL + 0.05) - 0.05
    // Or for darker fg: (bgL + 0.05) / (targetL + 0.05) = minRatio
    //   => targetL = (bgL + 0.05) / minRatio - 0.05

    // Determine if we should lighten or darken
    float lightTarget = minRatio * (bgL + 0.05f) - 0.05f;
    float darkTarget = (bgL + 0.05f) / minRatio - 0.05f;

    // Pick the adjustment direction that requires less change
    float fgL = relativeLuminance(fg);
    bool lighten = (std::abs(lightTarget - fgL) <= std::abs(darkTarget - fgL));

    // Clamp target luminance to [0, 1]
    float targetL = lighten ? lightTarget : darkTarget;
    targetL = std::clamp(targetL, 0.0f, 1.0f);

    // If lightening leads to > 1.0, try darkening instead and vice versa
    if (lighten && lightTarget > 1.0f) {
        targetL = std::clamp(darkTarget, 0.0f, 1.0f);
    } else if (!lighten && darkTarget < 0.0f) {
        targetL = std::clamp(lightTarget, 0.0f, 1.0f);
    }

    // Scale RGB channels uniformly to reach target luminance
    if (fgL < 0.001f) {
        // Nearly black fg: just set to gray
        uint8_t v = static_cast<uint8_t>(std::sqrt(targetL) * 255.0f);
        return (v << 16) | (v << 8) | v;
    }
    float scale = targetL / fgL;
    auto scaleChannel = [&](int shift) -> uint8_t {
        float c = ((fg >> shift) & 0xFF) / 255.0f;
        c = std::clamp(c * std::sqrt(scale), 0.0f, 1.0f);
        return static_cast<uint8_t>(c * 255.0f);
    };
    uint8_t r = scaleChannel(16);
    uint8_t g = scaleChannel(8);
    uint8_t b = scaleChannel(0);
    return (r << 16) | (g << 8) | b;
}

// ---------------------------------------------------------------------------
// Impl constructor
// ---------------------------------------------------------------------------
MetalTextRenderer::Impl::Impl(id<MTLDevice> dev, CAMetalLayer* metalLayer)
    : device(dev), layer(metalLayer)
{
    commandQueue = [device newCommandQueue];
    atlasUploader = std::make_unique<MetalAtlasUploader>(device);
    frameSemaphore = dispatch_semaphore_create(kBufferCount);
    allocateInstanceBuffers(kInitialMaxInstances);
    createDummyTextures();
}

void MetalTextRenderer::Impl::allocateInstanceBuffers(size_t maxInstances) {
    size_t bufSize = maxInstances * sizeof(CellInstance);
    for (int i = 0; i < kBufferCount; ++i) {
        instanceBuffers[i] = [device newBufferWithLength:bufSize
                                                 options:MTLResourceStorageModeShared];
    }
    instanceBufferCapacity = maxInstances;
}

// ---------------------------------------------------------------------------
// Dummy 1x1 textures
// ---------------------------------------------------------------------------
void MetalTextRenderer::Impl::createDummyTextures() {
    MTLTextureDescriptor* r8Desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                    width:1 height:1 mipmapped:NO];
    r8Desc.usage = MTLTextureUsageShaderRead;
    dummyR8 = [device newTextureWithDescriptor:r8Desc];
    uint8_t zero = 0;
    [dummyR8 replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
               mipmapLevel:0 withBytes:&zero bytesPerRow:1];

    MTLTextureDescriptor* bgraDesc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm_sRGB
                                    width:1 height:1 mipmapped:NO];
    bgraDesc.usage = MTLTextureUsageShaderRead;
    dummyBGRA = [device newTextureWithDescriptor:bgraDesc];
    uint32_t zeroPixel = 0;
    [dummyBGRA replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                 mipmapLevel:0 withBytes:&zeroPixel bytesPerRow:4];
}

// ---------------------------------------------------------------------------
// Pipeline creation
// ---------------------------------------------------------------------------
bool MetalTextRenderer::Impl::buildPipeline(id<MTLLibrary> library) {
    id<MTLFunction> vertexFunc = [library newFunctionWithName:@"cell_vertex"];
    id<MTLFunction> fragmentFunc = [library newFunctionWithName:@"cell_fragment"];
    if (!vertexFunc || !fragmentFunc) return false;

    MTLRenderPipelineDescriptor* desc =
        [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vertexFunc;
    desc.fragmentFunction = fragmentFunc;
    desc.colorAttachments[0].pixelFormat = layer.pixelFormat;

    // Standard alpha blending (bg pass writes alpha=1 -> overwrites;
    // glyph pass blends via alpha)
    desc.colorAttachments[0].blendingEnabled = YES;
    desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    desc.colorAttachments[0].sourceRGBBlendFactor =
        MTLBlendFactorOne;
    desc.colorAttachments[0].destinationRGBBlendFactor =
        MTLBlendFactorOneMinusSourceAlpha;
    desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    desc.colorAttachments[0].destinationAlphaBlendFactor =
        MTLBlendFactorOneMinusSourceAlpha;

    NSError* error = nil;
    pipelineState = [device newRenderPipelineStateWithDescriptor:desc
                                                           error:&error];
    if (!pipelineState) {
        NSLog(@"MetalTextRenderer: pipeline creation failed: %@", error);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Shader loading (metallib -> default library -> runtime compile)
// ---------------------------------------------------------------------------
bool MetalTextRenderer::Impl::loadShaderLibrary() {
    id<MTLLibrary> library = nil;

    // Try 1: pre-compiled .metallib from bundle
    NSBundle* bundle = [NSBundle mainBundle];
    NSString* libPath =
        [bundle pathForResource:@"default" ofType:@"metallib"];
    if (libPath) {
        NSError* error = nil;
        NSURL* libURL = [NSURL fileURLWithPath:libPath];
        library = [device newLibraryWithURL:libURL error:&error];
    }

    // Try 2: default library
    if (!library) library = [device newDefaultLibrary];

    // Try 3: runtime compile
    if (!library) library = compileShaderSource();

    if (!library) {
        NSLog(@"MetalTextRenderer: could not load shader library");
        return false;
    }
    return buildPipeline(library);
}

// ---------------------------------------------------------------------------
// Runtime shader compilation (fallback)
// ---------------------------------------------------------------------------
id<MTLLibrary> MetalTextRenderer::Impl::compileShaderSource() {
    NSString* source = @R"(
#include <metal_stdlib>
using namespace metal;

struct CellInstance {
    ushort2 grid_pos;
    ushort2 glyph_uv;
    ushort2 glyph_size;
    short2  offset;
    uchar4  fg_color;
    uchar4  bg_color;
    uchar   flags;
    uchar3  _pad;
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
    float4 fg_color;
    float4 bg_color;
    uint flags [[flat]];
};

struct Uniforms {
    float2 viewport_size;
    float2 cell_size;
    float2 atlas_size;
    float2 grid_padding;
};

vertex VertexOut cell_vertex(
    uint vertex_id [[vertex_id]],
    uint instance_id [[instance_id]],
    constant CellInstance* cells [[buffer(0)]],
    constant Uniforms& u [[buffer(1)]]
) {
    float2 corners[] = {
        {0,0}, {1,0}, {0,1},
        {1,0}, {1,1}, {0,1}
    };
    CellInstance cell = cells[instance_id];
    float2 corner = corners[vertex_id];
    bool is_bg = (cell.flags & 4) != 0;
    float2 cell_origin = float2(cell.grid_pos) * u.cell_size + u.grid_padding;
    float4 fg = float4(cell.fg_color) / 255.0;
    float4 bg = float4(cell.bg_color) / 255.0;
    float2 pixel_pos;
    float2 tex_coord;
    if (is_bg) {
        pixel_pos = cell_origin + corner * u.cell_size;
        tex_coord = float2(0);
    } else {
        float2 glyph_size = float2(cell.glyph_size);
        float2 bearing = float2(cell.offset);
        float2 glyph_origin = cell_origin + bearing;
        pixel_pos = glyph_origin + corner * glyph_size;
        tex_coord = float2(cell.glyph_uv) + corner * glyph_size;
    }
    float2 ndc = (pixel_pos / u.viewport_size) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    VertexOut out;
    out.position = float4(ndc, 0, 1);
    out.texCoord = tex_coord;
    out.fg_color = fg;
    out.bg_color = bg;
    out.flags = uint(cell.flags);
    return out;
}

fragment float4 cell_fragment(
    VertexOut in [[stage_in]],
    texture2d<float> atlas_gray [[texture(0)]],
    texture2d<float> atlas_color [[texture(1)]]
) {
    if ((in.flags & 4) != 0) {
        float a = in.bg_color.a;
        return float4(in.bg_color.rgb * a, a);
    }
    bool is_color = (in.flags & 2) != 0;
    if (is_color) {
        constexpr sampler emojiSampler(coord::pixel, address::clamp_to_edge, filter::linear);
        return atlas_color.sample(emojiSampler, in.texCoord);
    } else {
        constexpr sampler textSampler(coord::pixel, address::clamp_to_edge, filter::nearest);
        float alpha = atlas_gray.sample(textSampler, in.texCoord).r;
        return float4(in.fg_color.rgb * alpha, alpha);
    }
}
)";
    MTLCompileOptions* opts = [[MTLCompileOptions alloc] init];
    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:source
                                                  options:opts
                                                    error:&error];
    if (!library) {
        NSLog(@"MetalTextRenderer: runtime compile failed: %@", error);
    }
    return library;
}

} // namespace termcore
