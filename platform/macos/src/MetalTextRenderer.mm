#import "MetalTextRendererImpl.h"
#import <algorithm>

namespace termcore {

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

void MetalTextRenderer::setSelection(const SelectionState& sel) {
    impl_->selection = sel;
}

void MetalTextRenderer::render(const Screen& screen) {
    if (!impl_->pipelineState) return;

    // Determine if only cursor blink changed (no content rebuild needed)
    bool blinkChanged = (impl_->cursorBlinkOn != impl_->lastBlinkState);

    // Check needsRender from TerminalView -- always rebuild on content change.
    // We always call buildCellBuffer on first frame or when content changes.
    // For blink-only changes, just patch cursor instances in-place.
    if (blinkChanged && impl_->cellCountBeforeCursor > 0
        && !impl_->cellInstances.empty()) {
        // Only cursor blink toggled -- patch cursor without full rebuild
        impl_->patchCursorOnly(screen);
    } else {
        impl_->buildCellBuffer(screen);
    }
    impl_->lastBlinkState = impl_->cursorBlinkOn;

    if (impl_->cellInstances.empty()) return;

    // Upload dirty atlas textures
    if (impl_->glyphAtlas) {
        impl_->atlasUploader->upload(*impl_->glyphAtlas);
    }

    // Wait for a free buffer slot (triple buffering)
    dispatch_semaphore_wait(impl_->frameSemaphore, DISPATCH_TIME_FOREVER);

    @autoreleasepool {
        // Force Retina: set both contentsScale AND drawableSize before nextDrawable
        {
            CGFloat scale = 2.0; // Retina Mac
            impl_->layer.contentsScale = scale;
            CGSize bounds = impl_->layer.bounds.size;
            if (bounds.width > 0 && bounds.height > 0) {
                float w = bounds.width * scale;
                float h = bounds.height * scale;
                impl_->layer.drawableSize = CGSizeMake(w, h);
                // If viewport changed, update and force full cell rebuild
                if (fabs(impl_->viewportWidth - w) > 1 || fabs(impl_->viewportHeight - h) > 1) {
                    impl_->viewportWidth = w;
                    impl_->viewportHeight = h;
                    impl_->buildCellBuffer(screen);
                }
            }
        }
        id<CAMetalDrawable> drawable = [impl_->layer nextDrawable];
        if (!drawable) {
            dispatch_semaphore_signal(impl_->frameSemaphore);
            return;
        }

        // Resize instance buffers if needed
        size_t instanceCount = impl_->cellInstances.size();
        if (instanceCount > impl_->instanceBufferCapacity) {
            size_t newCapacity = instanceCount * 2;
            impl_->allocateInstanceBuffers(newCapacity);
        }

        // Copy cell instances into the current triple-buffered slot
        int bufIdx = impl_->currentBufferIndex;
        id<MTLBuffer> cellBuf = impl_->instanceBuffers[bufIdx];
        std::memcpy(cellBuf.contents, impl_->cellInstances.data(),
                    instanceCount * sizeof(CellInstance));
        impl_->currentBufferIndex = (bufIdx + 1) % Impl::kBufferCount;

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

        // Grid padding (same on all sides, plus tab bar offset on Y)
        uniforms.grid_padding[0] = impl_->gridPadding;
        uniforms.grid_padding[1] = impl_->gridPadding + impl_->gridOffsetY;

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

        // Signal semaphore when GPU finishes with this buffer
        dispatch_semaphore_t sema = impl_->frameSemaphore;
        [cmd addCompletedHandler:^(id<MTLCommandBuffer>) {
            dispatch_semaphore_signal(sema);
        }];

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
              instanceCount:instanceCount];

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

void MetalTextRenderer::setCursorBlinkInterval(float seconds) {
    impl_->blinkInterval = std::clamp(static_cast<double>(seconds), 0.1, 2.0);
}

void MetalTextRenderer::setIMEActive(bool active) {
    impl_->imeActive = active;
}

void MetalTextRenderer::setGridPadding(float padding) {
    impl_->gridPadding = std::max(0.0f, padding);
}

void MetalTextRenderer::setMinimumContrast(float ratio) {
    impl_->minimumContrast = std::clamp(ratio, 1.0f, 21.0f);
}

void MetalTextRenderer::setUrlHighlight(int row, int startCol, int endCol) {
    impl_->urlHighlightRow = row;
    impl_->urlHighlightStartCol = startCol;
    impl_->urlHighlightEndCol = endCol;
}

void MetalTextRenderer::setSearchHighlights(
        const std::vector<SearchHighlight>& highlights, int currentIndex) {
    impl_->searchHighlights = highlights;
    impl_->searchCurrentIndex = currentIndex;
    impl_->rebuildSearchIndex();
}

void MetalTextRenderer::setUrlHighlights(const std::vector<UrlHighlight>& highlights) {
    impl_->urlHighlights = highlights;
    impl_->rebuildUrlIndex();
}

void MetalTextRenderer::setTabBar(const TabBarInfo& info) {
    impl_->tabBar = info;
    impl_->contentDirty = true;
}

MetalTextRenderer::TabBarInfo MetalTextRenderer::getTabBar() const {
    return impl_->tabBar;
}

void MetalTextRenderer::markContentDirty() {
    impl_->contentDirty = true;
}

IAtlasUploader* MetalTextRenderer::atlasUploader() {
    return impl_->atlasUploader.get();
}

} // namespace termcore
