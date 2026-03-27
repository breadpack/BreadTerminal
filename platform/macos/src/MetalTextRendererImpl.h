#ifndef TERMCORE_METAL_TEXT_RENDERER_IMPL_H
#define TERMCORE_METAL_TEXT_RENDERER_IMPL_H

// Private implementation header for MetalTextRenderer.
// Shared between MetalTextRenderer.mm, MetalCellBuilder.mm, and MetalAtlasManager.mm.

#import "MetalTextRenderer.h"
#import "MetalAtlasUploader.h"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <vector>
#import <unordered_map>
#import <cstring>
#import <cmath>

#include "termcore/font/glyph_atlas.h"
#include "termcore/font/glyph_cache.h"
#include "termcore/font/font_collection.h"
#include "termcore/font/i_font_rasterizer.h"
#include "termcore/font/ligature.h"
#include "termcore/screen.h"

namespace termcore {

// WCAG 2.0 minimum contrast helpers
float linearize(float srgb);
float relativeLuminance(uint32_t color);
float contrastRatio(uint32_t fg, uint32_t bg);
uint32_t ensureContrast(uint32_t fg, uint32_t bg, float minRatio);

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

    // Cursor blink state (time-based, not frame-based)
    CFAbsoluteTime lastBlinkToggle = CFAbsoluteTimeGetCurrent();
    bool cursorBlinkOn = true;
    double blinkInterval = 0.5; // seconds, configurable
    bool imeActive = false; // hide cursor during IME composition

    // Reusable instance buffer (CPU side)
    std::vector<CellInstance> cellInstances;

    // Triple-buffered GPU instance buffers
    static constexpr int kBufferCount = 3;
    static constexpr size_t kInitialMaxInstances = 30000; // 200*50*3
    id<MTLBuffer> instanceBuffers[kBufferCount] = {};
    size_t instanceBufferCapacity = 0; // in number of CellInstance entries
    int currentBufferIndex = 0;
    dispatch_semaphore_t frameSemaphore;

    // Cursor blink dirty tracking -- avoid full rebuild on blink toggle
    bool lastBlinkState = true;
    size_t cellCountBeforeCursor = 0; // index where cursor instances begin

    // Selection state
    SelectionState selection;

    // Grid padding (physical pixels, all sides)
    float gridPadding = 0.0f;

    // Minimum contrast ratio (1.0 = disabled)
    float minimumContrast = 1.0f;

    // URL highlight (Cmd+hover underline) -- legacy single highlight
    int urlHighlightRow = -1;
    int urlHighlightStartCol = -1;
    int urlHighlightEndCol = -1;

    // Search highlight state
    std::vector<MetalTextRenderer::SearchHighlight> searchHighlights;
    int searchCurrentIndex = -1;
    // Row-indexed search highlights for O(1) row lookup
    std::unordered_map<int, std::vector<std::pair<int,int>>> searchByRow;
    void rebuildSearchIndex();

    // URL highlight state (vector-based)
    std::vector<MetalTextRenderer::UrlHighlight> urlHighlights;
    // Row-indexed URL highlights for O(1) row lookup
    std::unordered_map<int, std::vector<size_t>> urlByRow;
    void rebuildUrlIndex();

    // Tab bar state
    MetalTextRenderer::TabBarInfo tabBar;
    float gridOffsetY = 0.0f;  // computed from tab bar height

    // Content dirty flag (forces full rebuild)
    bool contentDirty = true;

    // Ghost text (dim suggestion text at cursor position)
    struct GhostText {
        std::string text;  // UTF-8 ghost text to display
        int row = -1;      // row to display at
        int col = -1;      // starting column
    };
    GhostText ghostText;

    // Font ligatures
    bool fontLigatures = true;
    LigatureDetector ligatureDetector;

    // Dummy textures for when atlas pages don't exist yet
    id<MTLTexture> dummyR8;
    id<MTLTexture> dummyBGRA;

    // -----------------------------------------------------------------
    Impl(id<MTLDevice> dev, CAMetalLayer* metalLayer);

    void allocateInstanceBuffers(size_t maxInstances);
    void createDummyTextures();
    bool buildPipeline(id<MTLLibrary> library);
    bool loadShaderLibrary();
    id<MTLLibrary> compileShaderSource();

    // Cell building (implemented in MetalCellBuilder.mm)
    int searchHighlightType(int row, int col) const;
    const MetalTextRenderer::UrlHighlight* urlHighlightAt(int row, int col) const;
    void buildCellBuffer(const Screen& screen);
    void appendCursorInstances(const Screen& screen, float cellW, float cellH,
                               float gridOffsetY);
    void patchCursorOnly(const Screen& screen);

    // Overlay passes (implemented in MetalCellBuilderOverlays.mm)
    void buildOverlayPasses(const Screen& screen, float cellW, float cellH,
                            float ascent, float fontSize);
};

} // namespace termcore

#endif // TERMCORE_METAL_TEXT_RENDERER_IMPL_H
