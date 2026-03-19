#ifndef D3D_TEXT_RENDERER_IMPL_H
#define D3D_TEXT_RENDERER_IMPL_H

#if defined(_WIN32)

#include "D3DTextRenderer.h"
#include "D3DAtlasUploader.h"
#include "termcore/dynamic_colors.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstring>
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
};

} // namespace termcore

#endif // _WIN32
#endif // D3D_TEXT_RENDERER_IMPL_H
