#import "MetalTextRenderer.h"
#import "MetalAtlasUploader.h"
#import <algorithm>
#import "termcore/font/glyph_atlas.h"
#import "termcore/font/glyph_cache.h"
#import "termcore/font/font_collection.h"
#import "termcore/font/i_font_rasterizer.h"
#import "termcore/font/box_drawing.h"
#import "termcore/screen.h"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <vector>

namespace termcore {

// ---------------------------------------------------------------------------
// Impl -- private implementation
// ---------------------------------------------------------------------------
struct MetalTextRenderer::Impl {
    id<MTLDevice> device;
    CAMetalLayer* layer;
    id<MTLCommandQueue> commandQueue;
    id<MTLRenderPipelineState> pipelineState;

    // Font stack (not owned)
    FontCollection* fontCollection = nullptr;
    GlyphCache* glyphCache = nullptr;
    GlyphAtlas* glyphAtlas = nullptr;
    IFontRasterizer* rasterizer = nullptr;

    // Atlas uploader
    std::unique_ptr<MetalAtlasUploader> atlasUploader;

    // Viewport (physical pixels)
    float viewportWidth = 0;
    float viewportHeight = 0;

    // Background opacity (0.0 = fully transparent, 1.0 = opaque)
    float backgroundOpacity = 1.0f;

    // Reusable instance buffer
    std::vector<CellInstance> cellInstances;

    // Dummy textures for when atlas pages don't exist yet
    id<MTLTexture> dummyR8;
    id<MTLTexture> dummyBGRA;

    // -----------------------------------------------------------------
    Impl(id<MTLDevice> dev, CAMetalLayer* metalLayer)
        : device(dev), layer(metalLayer)
    {
        commandQueue = [device newCommandQueue];
        atlasUploader = std::make_unique<MetalAtlasUploader>(device);
        createDummyTextures();
    }

    // -----------------------------------------------------------------
    // Dummy 1x1 textures
    // -----------------------------------------------------------------
    void createDummyTextures() {
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

    // -----------------------------------------------------------------
    // Pipeline creation
    // -----------------------------------------------------------------
    bool buildPipeline(id<MTLLibrary> library) {
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

    // -----------------------------------------------------------------
    // Shader loading (metallib -> default library -> runtime compile)
    // -----------------------------------------------------------------
    bool loadShaderLibrary() {
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

    // -----------------------------------------------------------------
    // Runtime shader compilation (fallback)
    // -----------------------------------------------------------------
    id<MTLLibrary> compileShaderSource() {
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

    // -----------------------------------------------------------------
    // buildCellBuffer -- clean 2-pass (backgrounds then glyphs)
    // -----------------------------------------------------------------
    void buildCellBuffer(const Screen& screen) {
        if (!fontCollection || !glyphCache || !glyphAtlas || !rasterizer)
            return;

        FontMetrics metrics = fontCollection->primaryMetrics();
        float cellW = metrics.cell_width;
        float cellH = metrics.cell_height;
        float ascent = metrics.ascent;
        float fontSize = fontCollection->fontSize();
        int rows = screen.rows();
        int cols = screen.cols();

        cellInstances.clear();
        cellInstances.reserve(static_cast<size_t>(rows) * cols * 2);

        const auto& dc = screen.dynamicColors();

        uint8_t bgAlpha = static_cast<uint8_t>(255.0f * backgroundOpacity);

        // Pass 1: Background quads
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                const TermCell& cell = screen.cellAt(row, col);

                uint32_t fg = dc.resolveFg(cell.fg_color);
                uint32_t bg = dc.resolveBg(cell.bg_color);
                if (cell.attributes & AttrInverse) std::swap(fg, bg);

                CellInstance inst = {};
                inst.grid_col = static_cast<uint16_t>(col);
                inst.grid_row = static_cast<uint16_t>(row);
                inst.fg_r = (fg >> 16) & 0xFF;
                inst.fg_g = (fg >> 8) & 0xFF;
                inst.fg_b = fg & 0xFF;
                inst.fg_a = 255;
                inst.bg_r = (bg >> 16) & 0xFF;
                inst.bg_g = (bg >> 8) & 0xFF;
                inst.bg_b = bg & 0xFF;
                inst.bg_a = bgAlpha;
                inst.flags = 4; // bg pass
                cellInstances.push_back(inst);
            }
        }

        // Pass 2: Glyph quads
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                const TermCell& cell = screen.cellAt(row, col);
                if (cell.codepoint <= ' ') continue;

                // Skip continuation cells (second cell of a wide character,
                // marked with width=0 and codepoint=0)
                if (cell.width == 0) continue;

                char32_t cp = cell.codepoint;

                // Check for procedural box drawing
                bool is_box_drawing =
                    (cp >= 0x2500 && cp <= 0x259F) ||
                    (cp >= 0x2800 && cp <= 0x28FF) ||
                    (cp >= 0xE0B0 && cp <= 0xE0B3);

                if (is_box_drawing) {
                    // Use a special GlyphKey with kInvalidFontFace to distinguish from font glyphs
                    GlyphKey boxKey{kInvalidFontFace, static_cast<uint32_t>(cp), {0, 0}};
                    auto boxInfo = glyphCache->get(boxKey);
                    if (!boxInfo) {
                        BoxGlyphBitmap boxBitmap = render_box_glyph(
                            cp,
                            static_cast<int>(cellW),
                            static_cast<int>(cellH));
                        if (!boxBitmap.bitmap.empty()) {
                            RasterizedGlyph rg;
                            rg.bitmap = std::move(boxBitmap.bitmap);
                            rg.width = boxBitmap.width;
                            rg.height = boxBitmap.height;
                            rg.bearing_x = 0;
                            rg.bearing_y = static_cast<int32_t>(ascent);
                            rg.format = PixelFormat::Grayscale;
                            auto region = glyphAtlas->pack(rg);
                            if (region) {
                                GlyphInfo gi;
                                gi.region = *region;
                                gi.advance_x = cellW;
                                gi.advance_y = 0;
                                gi.is_color = false;
                                glyphCache->put(boxKey, gi);
                                boxInfo = gi;
                            }
                        }
                    }
                    if (boxInfo) {
                        uint32_t fg = dc.resolveFg(cell.fg_color);
                        uint32_t bg = dc.resolveBg(cell.bg_color);
                        if (cell.attributes & AttrInverse) std::swap(fg, bg);

                        CellInstance inst = {};
                        inst.grid_col = static_cast<uint16_t>(col);
                        inst.grid_row = static_cast<uint16_t>(row);
                        inst.glyph_x = static_cast<uint16_t>(boxInfo->region.x);
                        inst.glyph_y = static_cast<uint16_t>(boxInfo->region.y);
                        inst.glyph_width = static_cast<uint16_t>(boxInfo->region.width);
                        inst.glyph_height = static_cast<uint16_t>(boxInfo->region.height);
                        // Box drawing fills cell from top-left
                        inst.offset_x = 0;
                        inst.offset_y = 0;
                        inst.fg_r = (fg >> 16) & 0xFF;
                        inst.fg_g = (fg >> 8) & 0xFF;
                        inst.fg_b = fg & 0xFF;
                        inst.fg_a = 255;
                        inst.bg_r = (bg >> 16) & 0xFF;
                        inst.bg_g = (bg >> 8) & 0xFF;
                        inst.bg_b = bg & 0xFF;
                        inst.bg_a = 255;
                        inst.flags = 1; // has_glyph
                        cellInstances.push_back(inst);
                    }
                    continue;  // Skip normal font rendering for this cell
                }

                CollectionFaceId faceId =
                    fontCollection->resolveFace(cell.codepoint);
                if (faceId == kInvalidCollectionFace) continue;

                FontFaceId rastFace =
                    fontCollection->rasterizerFaceId(faceId);
                uint32_t glyphIdx =
                    rasterizer->getGlyphIndex(rastFace, cell.codepoint);
                if (glyphIdx == 0) continue;

                GlyphKey key{rastFace, glyphIdx, {0, 0}};
                auto info = glyphCache->getOrRasterize(
                    key, fontSize, *rasterizer, *glyphAtlas);
                if (!info) continue;

                uint32_t fg = dc.resolveFg(cell.fg_color);
                uint32_t bg = dc.resolveBg(cell.bg_color);
                if (cell.attributes & AttrInverse) std::swap(fg, bg);

                bool is_wide = (cell.width == 2);

                CellInstance inst = {};
                inst.grid_col = static_cast<uint16_t>(col);
                inst.grid_row = static_cast<uint16_t>(row);
                inst.glyph_x =
                    static_cast<uint16_t>(info->region.x);
                inst.glyph_y =
                    static_cast<uint16_t>(info->region.y);
                inst.glyph_width =
                    static_cast<uint16_t>(info->region.width);
                inst.glyph_height =
                    static_cast<uint16_t>(info->region.height);

                // offset_y = ascent - bearing_y (distance from cell top
                // to glyph top)
                inst.offset_y = static_cast<int16_t>(
                    static_cast<int>(ascent) - info->region.bearing_y);

                if (is_wide) {
                    // Center glyph horizontally across the 2-cell span
                    float spanWidth = cellW * 2.0f;
                    if (info->region.width < static_cast<int>(spanWidth)) {
                        int16_t center_offset = static_cast<int16_t>(
                            (spanWidth - info->region.width) / 2.0f);
                        inst.offset_x = static_cast<int16_t>(
                            center_offset + info->region.bearing_x);
                    } else {
                        inst.offset_x =
                            static_cast<int16_t>(info->region.bearing_x);
                    }
                } else {
                    // offset_x = bearing_x (signed, from cell left edge)
                    inst.offset_x =
                        static_cast<int16_t>(info->region.bearing_x);
                }

                inst.fg_r = (fg >> 16) & 0xFF;
                inst.fg_g = (fg >> 8) & 0xFF;
                inst.fg_b = fg & 0xFF;
                inst.fg_a = 255;
                inst.bg_r = (bg >> 16) & 0xFF;
                inst.bg_g = (bg >> 8) & 0xFF;
                inst.bg_b = bg & 0xFF;
                inst.bg_a = 255;

                inst.flags = 1; // has_glyph
                if (info->is_color) inst.flags |= 2;
                cellInstances.push_back(inst);
            }
        }
    }
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

MetalTextRenderer::MetalTextRenderer(id<MTLDevice> device, CAMetalLayer* layer)
    : impl_(std::make_unique<Impl>(device, layer))
{
    impl_->loadShaderLibrary();
}

MetalTextRenderer::~MetalTextRenderer() = default;

void MetalTextRenderer::setFontStack(FontCollection* collection,
                                      GlyphCache* cache,
                                      GlyphAtlas* atlas,
                                      IFontRasterizer* rasterizer) {
    impl_->fontCollection = collection;
    impl_->glyphCache = cache;
    impl_->glyphAtlas = atlas;
    impl_->rasterizer = rasterizer;

    // Pre-cache ASCII glyphs for instant first-frame display
    if (collection && cache && atlas && rasterizer) {
        CollectionFaceId cface = collection->resolveFace('A');
        if (cface != kInvalidCollectionFace) {
            FontFaceId primaryFace = collection->rasterizerFaceId(cface);
            if (primaryFace != kInvalidFontFace) {
                cache->precacheAscii(primaryFace, collection->fontSize(),
                                      *rasterizer, *atlas);
            }
        }
    }
}

void MetalTextRenderer::render(const Screen& screen) {
    if (!impl_->pipelineState) return;

    impl_->buildCellBuffer(screen);
    if (impl_->cellInstances.empty()) return;

    // Upload dirty atlas textures
    if (impl_->glyphAtlas) {
        impl_->atlasUploader->upload(*impl_->glyphAtlas);
    }

    @autoreleasepool {
        id<CAMetalDrawable> drawable = [impl_->layer nextDrawable];
        if (!drawable) return;

        // Cell instance buffer
        size_t bufSize =
            impl_->cellInstances.size() * sizeof(CellInstance);
        id<MTLBuffer> cellBuf = [impl_->device
            newBufferWithBytes:impl_->cellInstances.data()
                        length:bufSize
                       options:MTLResourceStorageModeShared];

        // Uniforms
        CellUniforms uniforms = {};
        uniforms.viewport_size[0] = impl_->viewportWidth;
        uniforms.viewport_size[1] = impl_->viewportHeight;

        if (impl_->fontCollection) {
            FontMetrics m = impl_->fontCollection->primaryMetrics();
            uniforms.cell_size[0] = m.cell_width;
            uniforms.cell_size[1] = m.cell_height;
        }

        if (impl_->glyphAtlas) {
            const AtlasPage* p =
                impl_->glyphAtlas->getPage(AtlasFormat::R8);
            if (p) {
                uniforms.atlas_size[0] =
                    static_cast<float>(p->width());
                uniforms.atlas_size[1] =
                    static_cast<float>(p->height());
            }
        }

        // grid_padding left at 0,0

        id<MTLBuffer> uniformBuf = [impl_->device
            newBufferWithBytes:&uniforms
                        length:sizeof(CellUniforms)
                       options:MTLResourceStorageModeShared];

        // Use dynamic background color for clear
        const auto& dc = screen.dynamicColors();
        uint32_t bgc = dc.background;
        double clearR = ((bgc >> 16) & 0xFF) / 255.0;
        double clearG = ((bgc >> 8) & 0xFF) / 255.0;
        double clearB = (bgc & 0xFF) / 255.0;

        // Render pass
        MTLRenderPassDescriptor* pass =
            [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        double clearA = static_cast<double>(impl_->backgroundOpacity);
        pass.colorAttachments[0].clearColor =
            MTLClearColorMake(clearR * clearA, clearG * clearA, clearB * clearA, clearA);

        id<MTLCommandBuffer> cmd =
            [impl_->commandQueue commandBuffer];
        id<MTLRenderCommandEncoder> enc =
            [cmd renderCommandEncoderWithDescriptor:pass];

        [enc setRenderPipelineState:impl_->pipelineState];
        [enc setVertexBuffer:cellBuf offset:0 atIndex:0];
        [enc setVertexBuffer:uniformBuf offset:0 atIndex:1];

        // Bind atlas textures (dummy if not ready)
        id<MTLTexture> grayTex =
            impl_->atlasUploader->textureForFormat(AtlasFormat::R8)
                ?: impl_->dummyR8;
        id<MTLTexture> colorTex =
            impl_->atlasUploader->textureForFormat(AtlasFormat::BGRA)
                ?: impl_->dummyBGRA;
        [enc setFragmentTexture:grayTex atIndex:0];
        [enc setFragmentTexture:colorTex atIndex:1];

        // Instanced draw: 6 vertices per quad, N instances
        [enc drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:6
              instanceCount:impl_->cellInstances.size()];

        [enc endEncoding];
        [cmd presentDrawable:drawable];
        [cmd commit];
    }
}

void MetalTextRenderer::resize(float width, float height) {
    impl_->viewportWidth = width;
    impl_->viewportHeight = height;
}

void MetalTextRenderer::setBackgroundOpacity(float opacity) {
    impl_->backgroundOpacity = std::clamp(opacity, 0.0f, 1.0f);
}

} // namespace termcore
