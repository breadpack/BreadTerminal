#ifndef D3D_TEXT_RENDERER_IMPL_H
#define D3D_TEXT_RENDERER_IMPL_H

#if defined(_WIN32)

#include "D3DTextRenderer.h"
#include "D3DAtlasUploader.h"
#include "termcore/dynamic_colors.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <algorithm>

#pragma comment(lib, "d3dcompiler.lib")

namespace termcore {

// HLSL shader source for two-pass cell rendering (background + glyph + cursor).
extern const char* kCellShaderSource;

struct D3DTextRenderer::Impl {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;

    // Selection state
    D3DTextRenderer::Selection selection;

    // Search highlight state
    std::vector<D3DTextRenderer::SearchHighlight> searchHighlights;
    int searchCurrentIndex = -1;

    // Cursor blink state
    bool cursorBlinkVisible = true;
    bool lastBlinkState = true;
    bool contentDirty = true;  // forces full rebuild on first frame

    // Index in cellInstances where cursor instances begin
    // (everything from this index onward is cursor + overlays)
    size_t cellCountBeforeCursor = 0;

    // Status bar state
    D3DTextRenderer::StatusBarInfo statusBar;

    // Tab bar state
    D3DTextRenderer::TabBarInfo tabBar;

    // Pane border state
    D3DTextRenderer::PaneBorderInfo paneBorders;

    // Per-pane progress bars
    std::unordered_map<PaneId, D3DTextRenderer::PaneProgressInfo> paneProgress;

    // Per-pane status pills
    std::unordered_map<PaneId, std::vector<D3DTextRenderer::StatusPillInfo>> paneStatusPills;

    // Resize overlay state
    bool resizeOverlayVisible = false;
    int resizeOverlayCols = 0;
    int resizeOverlayRows = 0;

    // Shaders
    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;

    // Constant buffer
    ID3D11Buffer* constantBuffer = nullptr;

    // Structured buffer for cell instances
    ID3D11Buffer* cellBuffer = nullptr;
    ID3D11ShaderResourceView* cellBufferSRV = nullptr;
    UINT cellBufferCapacity = 0;

    // Sampler
    ID3D11SamplerState* sampler = nullptr;

    // Blend state
    ID3D11BlendState* blendState = nullptr;

    // Font stack (not owned)
    FontCollection* fontCollection = nullptr;
    GlyphCache* glyphCache = nullptr;
    GlyphAtlas* glyphAtlas = nullptr;
    IFontRasterizer* rasterizer = nullptr;

    // Atlas uploader
    std::unique_ptr<D3DAtlasUploader> atlasUploader;

    // Viewport
    float viewportWidth = 0;
    float viewportHeight = 0;

    // Reusable buffer
    std::vector<D3DCellInstance> cellInstances;

    struct CellConstants {
        float viewport_size[2];
        float cell_size[2];
        float atlas_size[2];
        float _padding[2];
    };

    bool buildShaders();
    bool createResources();
    void ensureCellBuffer(UINT requiredCount);
    void cleanup();

    // Cell buffer construction (implemented in D3DCellBuilder.cpp)
    static void colorFromRGBA(uint32_t rgba, float out[4]);
    bool isCellSelected(int row, int col) const;
    int searchHighlightType(int row, int col) const;
    void buildCellBuffer(const Screen& screen);
    void appendCursorInstances(const Screen& screen, float cellW, float cellH,
                               float gridOffsetY);
    void patchCursorOnly(const Screen& screen);

    // Overlay passes (implemented in D3DCellBuilderOverlays.cpp)
    void buildOverlayPasses(const Screen& screen, float cellW, float cellH,
                            float ascent, float fontSize);

    // Pane status overlays (implemented in D3DCellBuilderPaneStatus.cpp)
    void buildPaneStatusOverlays(float cellW, float cellH,
                                 float ascent, float fontSize);
};

} // namespace termcore

#endif // _WIN32
#endif // D3D_TEXT_RENDERER_IMPL_H
