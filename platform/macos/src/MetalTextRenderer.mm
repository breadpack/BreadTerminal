#import "MetalTextRenderer.h"
#import "MetalAtlasUploader.h"
#import "termcore/font/glyph_atlas.h"
#import "termcore/font/glyph_cache.h"
#import "termcore/font/font_collection.h"
#import "termcore/font/i_font_rasterizer.h"
#import "termcore/screen.h"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <vector>

namespace termcore {

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

    // Viewport
    float viewportWidth = 0;
    float viewportHeight = 0;

    // Reusable buffers
    std::vector<CellInstance> cellInstances;

    id<MTLTexture> dummyR8;
    id<MTLTexture> dummyBGRA;

    Impl(id<MTLDevice> dev, CAMetalLayer* metalLayer)
        : device(dev), layer(metalLayer) {
        commandQueue = [device newCommandQueue];
        atlasUploader = std::make_unique<MetalAtlasUploader>(device);

        // Create 1x1 dummy textures for when atlas pages don't exist yet
        MTLTextureDescriptor* r8Desc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:1 height:1 mipmapped:NO];
        r8Desc.usage = MTLTextureUsageShaderRead;
        dummyR8 = [device newTextureWithDescriptor:r8Desc];
        uint8_t zero = 0;
        [dummyR8 replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0 withBytes:&zero bytesPerRow:1];

        MTLTextureDescriptor* bgraDesc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm_sRGB width:1 height:1 mipmapped:NO];
        bgraDesc.usage = MTLTextureUsageShaderRead;
        dummyBGRA = [device newTextureWithDescriptor:bgraDesc];
        uint32_t zeroPixel = 0;
        [dummyBGRA replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0 withBytes:&zeroPixel bytesPerRow:4];
    }

    bool buildPipeline(id<MTLLibrary> library) {
        id<MTLFunction> vertexFunc = [library newFunctionWithName:@"cell_vertex"];
        id<MTLFunction> fragmentFunc = [library newFunctionWithName:@"cell_fragment"];
        if (!vertexFunc || !fragmentFunc) {
            return false;
        }

        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = vertexFunc;
        desc.fragmentFunction = fragmentFunc;
        desc.colorAttachments[0].pixelFormat = layer.pixelFormat;

        // Enable alpha blending
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;

        NSError* error = nil;
        pipelineState = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!pipelineState) {
            NSLog(@"MetalTextRenderer: Failed to create pipeline: %@", error);
            return false;
        }
        return true;
    }

    bool loadShaderLibrary() {
        id<MTLLibrary> library = nil;

        // Try 1: Load from pre-compiled default.metallib
        NSBundle* bundle = [NSBundle mainBundle];
        NSString* libPath = [bundle pathForResource:@"default" ofType:@"metallib"];
        if (libPath) {
            NSError* error = nil;
            NSURL* libURL = [NSURL fileURLWithPath:libPath];
            library = [device newLibraryWithURL:libURL error:&error];
        }

        // Try 2: Default library (works when shader is in app bundle)
        if (!library) {
            library = [device newDefaultLibrary];
        }

        // Try 3: Compile from source at runtime
        if (!library) {
            library = compileShaderSource();
        }

        if (!library) {
            NSLog(@"MetalTextRenderer: Could not load Metal shader library");
            return false;
        }

        return buildPipeline(library);
    }

    id<MTLLibrary> compileShaderSource() {
        NSString* source = @R"(
#include <metal_stdlib>
using namespace metal;

struct CellInstance {
    float2 position;
    float2 atlas_uv;
    float2 atlas_size;
    float2 glyph_offset;
    float4 fg_color;
    float4 bg_color;
    uint flags;
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
};

vertex VertexOut cell_vertex(
    uint vertex_id [[vertex_id]],
    uint instance_id [[instance_id]],
    constant CellInstance* cells [[buffer(0)]],
    constant Uniforms& uniforms [[buffer(1)]]
) {
    float2 corners[] = {
        {0, 0}, {1, 0}, {0, 1},
        {1, 0}, {1, 1}, {0, 1}
    };
    CellInstance cell = cells[instance_id];
    float2 corner = corners[vertex_id];

    // Cell quad position
    float2 pixel_pos = cell.position + corner * uniforms.cell_size;
    float2 ndc = (pixel_pos / uniforms.viewport_size) * 2.0 - 1.0;
    ndc.y = -ndc.y;

    // Texture coordinate: map glyph region from atlas onto cell
    float2 atlas_sz = max(uniforms.atlas_size, float2(1.0, 1.0));
    float2 tex_coord = (cell.atlas_uv + corner * cell.atlas_size) / atlas_sz;
    VertexOut out;
    out.position = float4(ndc, 0, 1);
    out.texCoord = tex_coord;
    out.fg_color = cell.fg_color;
    out.bg_color = cell.bg_color;
    out.flags = cell.flags;
    return out;
}

fragment float4 cell_fragment(
    VertexOut in [[stage_in]],
    texture2d<float> atlas_r8 [[texture(0)]],
    texture2d<float> atlas_bgra [[texture(1)]]
) {
    constexpr sampler s(mag_filter::nearest, min_filter::nearest);
    float4 color = in.bg_color;
    bool has_glyph = (in.flags & 1) != 0;
    bool is_color = (in.flags & 2) != 0;
    if (has_glyph) {
        if (is_color) {
            float4 glyph_color = atlas_bgra.sample(s, in.texCoord);
            color = mix(color, glyph_color, glyph_color.a);
        } else {
            float alpha = atlas_r8.sample(s, in.texCoord).r;
            color = mix(color, in.fg_color, alpha);
        }
    }
    return color;
}
)";
        MTLCompileOptions* opts = [[MTLCompileOptions alloc] init];
        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source
                                                      options:opts
                                                        error:&error];
        if (!library) {
            NSLog(@"MetalTextRenderer: Runtime shader compile failed: %@", error);
        }
        return library;
    }

    static void colorFromRGBA(uint32_t rgba, float out[4]) {
        out[0] = static_cast<float>((rgba >> 16) & 0xFF) / 255.0f;
        out[1] = static_cast<float>((rgba >> 8) & 0xFF) / 255.0f;
        out[2] = static_cast<float>(rgba & 0xFF) / 255.0f;
        out[3] = 1.0f;
    }

    int debugFrameCount = 0;

    void buildCellBuffer(const Screen& screen) {
        if (!fontCollection || !glyphCache || !glyphAtlas || !rasterizer) {
            if (debugFrameCount++ < 3) NSLog(@"BreadTerminal: buildCellBuffer - missing font stack (fc=%p gc=%p ga=%p r=%p)", fontCollection, glyphCache, glyphAtlas, rasterizer);
            return;
        }

        FontMetrics metrics = fontCollection->primaryMetrics();
        float cellW = metrics.cell_width;
        float cellH = metrics.cell_height;
        float fontSize = fontCollection->fontSize();

        if (debugFrameCount < 3) {
            NSLog(@"BreadTerminal: buildCellBuffer cellW=%.1f cellH=%.1f fontSize=%.1f rows=%d cols=%d", cellW, cellH, fontSize, screen.rows(), screen.cols());
        }

        int rows = screen.rows();
        int cols = screen.cols();

        cellInstances.clear();
        cellInstances.reserve(rows * cols);

        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                const TermCell& cell = screen.cellAt(row, col);

                CellInstance inst = {};
                inst.position[0] = col * cellW;
                inst.position[1] = row * cellH;

                colorFromRGBA(cell.fg_color, inst.fg_color);
                colorFromRGBA(cell.bg_color, inst.bg_color);

                // Handle inverse attribute
                if (cell.attributes & AttrInverse) {
                    std::swap(inst.fg_color[0], inst.bg_color[0]);
                    std::swap(inst.fg_color[1], inst.bg_color[1]);
                    std::swap(inst.fg_color[2], inst.bg_color[2]);
                    std::swap(inst.fg_color[3], inst.bg_color[3]);
                }

                inst.flags = 0;

                // Only look up glyph for non-space characters
                if (cell.codepoint != ' ' && cell.codepoint != 0) {
                    CollectionFaceId faceId = fontCollection->resolveFace(cell.codepoint);
                    if (debugFrameCount < 3 && row == 0 && col < 5) {
                        NSLog(@"BreadTerminal: cell[%d,%d] cp=U+%04X faceId=%u", row, col, cell.codepoint, faceId);
                    }
                    if (faceId != kInvalidCollectionFace) {
                        FontFaceId rastFace = fontCollection->rasterizerFaceId(faceId);
                        uint32_t glyphIdx = rasterizer->getGlyphIndex(rastFace, cell.codepoint);

                        if (glyphIdx != 0) {
                            GlyphKey key{rastFace, glyphIdx, {0, 0}};

                            // DEBUG: rasterize directly to check bitmap
                            if (debugFrameCount < 5 && row == 0 && col < 6) {
                                auto rawGlyph = rasterizer->rasterize(rastFace, glyphIdx, fontSize, {0,0});
                                int nonZero = 0;
                                for (auto b : rawGlyph.bitmap) if (b > 0) nonZero++;
                                NSLog(@"BreadTerminal: RAW glyph[%d,%d] w=%d h=%d bx=%d by=%d fmt=%d bytes=%zu nonZero=%d",
                                      row, col, rawGlyph.width, rawGlyph.height,
                                      rawGlyph.bearing_x, rawGlyph.bearing_y,
                                      (int)rawGlyph.format, rawGlyph.bitmap.size(), nonZero);
                            }

                            auto info = glyphCache->getOrRasterize(
                                key, fontSize, *rasterizer, *glyphAtlas);
                            if (info) {
                                inst.flags |= 1; // has_glyph
                                if (info->is_color) {
                                    inst.flags |= 2; // is_color
                                }
                                inst.atlas_uv[0] = static_cast<float>(info->region.x);
                                inst.atlas_uv[1] = static_cast<float>(info->region.y);
                                inst.atlas_size[0] = static_cast<float>(info->region.width);
                                inst.atlas_size[1] = static_cast<float>(info->region.height);
                                inst.glyph_offset[0] = static_cast<float>(info->region.bearing_x);
                                inst.glyph_offset[1] = static_cast<float>(info->region.bearing_y);
                            }
                        }
                    }
                }

                cellInstances.push_back(inst);
            }
        }
        debugFrameCount++;
    }
};

MetalTextRenderer::MetalTextRenderer(id<MTLDevice> device, CAMetalLayer* layer)
    : impl_(std::make_unique<Impl>(device, layer)) {
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
}

void MetalTextRenderer::render(const Screen& screen) {
    if (!impl_->pipelineState) {
        return;
    }

    // Build cell instance data from screen
    impl_->buildCellBuffer(screen);
    if (impl_->cellInstances.empty()) {
        return;
    }

    // Upload dirty atlas textures
    if (impl_->glyphAtlas) {
        impl_->atlasUploader->upload(*impl_->glyphAtlas);

        if (impl_->debugFrameCount < 5) {
            const AtlasPage* page = impl_->glyphAtlas->getPage(AtlasFormat::R8);
            if (page && page->width() > 0) {
                int nonZero = 0;
                const uint8_t* data = page->data();
                int total = page->width() * page->height();
                for (int i = 0; i < total; i++) if (data[i] > 0) nonZero++;
                NSLog(@"BreadTerminal: atlas R8 %dx%d nonZeroPixels=%d/%d",
                      page->width(), page->height(), nonZero, total);
            }
        }
    }

    // Get drawable
    @autoreleasepool {
        id<CAMetalDrawable> drawable = [impl_->layer nextDrawable];
        if (!drawable) {
            return;
        }

        // Create cell instance buffer
        size_t bufferSize = impl_->cellInstances.size() * sizeof(CellInstance);
        id<MTLBuffer> cellBuffer = [impl_->device
            newBufferWithBytes:impl_->cellInstances.data()
                        length:bufferSize
                       options:MTLResourceStorageModeShared];

        // Create uniforms
        CellUniforms uniforms = {};
        uniforms.viewport_size[0] = impl_->viewportWidth;
        uniforms.viewport_size[1] = impl_->viewportHeight;

        if (impl_->fontCollection) {
            FontMetrics m = impl_->fontCollection->primaryMetrics();
            uniforms.cell_size[0] = m.cell_width;
            uniforms.cell_size[1] = m.cell_height;
        }

        // Atlas size (use R8 page as reference; BGRA may differ)
        if (impl_->glyphAtlas) {
            const AtlasPage* r8Page = impl_->glyphAtlas->getPage(AtlasFormat::R8);
            if (r8Page) {
                uniforms.atlas_size[0] = static_cast<float>(r8Page->width());
                uniforms.atlas_size[1] = static_cast<float>(r8Page->height());
            }
            if (impl_->debugFrameCount < 5) {
                NSLog(@"BreadTerminal: render viewport=%.0fx%.0f cell=%.1fx%.1f atlas=%.0fx%.0f r8page=%p instances=%zu",
                      uniforms.viewport_size[0], uniforms.viewport_size[1],
                      uniforms.cell_size[0], uniforms.cell_size[1],
                      uniforms.atlas_size[0], uniforms.atlas_size[1],
                      (void*)r8Page, impl_->cellInstances.size());
            }
        }

        id<MTLBuffer> uniformBuffer = [impl_->device
            newBufferWithBytes:&uniforms
                        length:sizeof(CellUniforms)
                       options:MTLResourceStorageModeShared];

        // Render pass
        MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor renderPassDescriptor];
        passDesc.colorAttachments[0].texture = drawable.texture;
        passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
        passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
        passDesc.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);

        id<MTLCommandBuffer> cmdBuffer = [impl_->commandQueue commandBuffer];
        id<MTLRenderCommandEncoder> encoder =
            [cmdBuffer renderCommandEncoderWithDescriptor:passDesc];

        [encoder setRenderPipelineState:impl_->pipelineState];
        [encoder setVertexBuffer:cellBuffer offset:0 atIndex:0];
        [encoder setVertexBuffer:uniformBuffer offset:0 atIndex:1];

        // Bind atlas textures (always bind — use dummy if atlas not ready)
        id<MTLTexture> r8Tex =
            impl_->atlasUploader->textureForFormat(AtlasFormat::R8) ?: impl_->dummyR8;
        id<MTLTexture> bgraTex =
            impl_->atlasUploader->textureForFormat(AtlasFormat::BGRA) ?: impl_->dummyBGRA;

        [encoder setFragmentTexture:r8Tex atIndex:0];
        [encoder setFragmentTexture:bgraTex atIndex:1];

        // Instanced draw: 6 vertices per quad, N instances
        NSUInteger instanceCount = impl_->cellInstances.size();
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:6
                  instanceCount:instanceCount];

        [encoder endEncoding];
        [cmdBuffer presentDrawable:drawable];
        [cmdBuffer commit];
    }
}

void MetalTextRenderer::resize(float width, float height) {
    impl_->viewportWidth = width;
    impl_->viewportHeight = height;
}

} // namespace termcore
