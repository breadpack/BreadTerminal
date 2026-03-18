#if defined(_WIN32)

#include "D3DTextRenderer.h"
#include "D3DAtlasUploader.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstring>
#include <vector>
#include <algorithm>

#pragma comment(lib, "d3dcompiler.lib")

namespace termcore {

namespace {

// Inline HLSL source (matches cell.hlsl)
const char* kCellShaderSource = R"(
cbuffer CellConstants : register(b0) {
    float2 viewport_size;
    float2 cell_size;
    float2 atlas_size;
    float2 _padding;
};

Texture2D atlas_r8   : register(t0);
Texture2D atlas_bgra : register(t1);
SamplerState atlas_sampler : register(s0);

struct CellInstance {
    float2 position;
    float2 atlas_uv;
    float2 atlas_size_px;
    float2 glyph_offset;
    float4 fg_color;
    float4 bg_color;
    uint   flags;
};

StructuredBuffer<CellInstance> cells : register(t2);

struct VS_OUTPUT {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    float4 fg_color : COLOR0;
    float4 bg_color : COLOR1;
    uint   flags    : BLENDINDICES0;
};

VS_OUTPUT VSMain(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID) {
    VS_OUTPUT output;

    float2 corners[6] = {
        float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0),
        float2(1.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)
    };
    float2 corner = corners[vertex_id];

    CellInstance cell = cells[instance_id];

    float2 pixel_pos = cell.position + corner * cell_size;
    float2 ndc = (pixel_pos / viewport_size) * 2.0 - 1.0;
    ndc.y = -ndc.y;

    output.position = float4(ndc, 0.0, 1.0);
    output.texCoord = (cell.atlas_uv + corner * cell.atlas_size_px) / atlas_size;
    output.fg_color = cell.fg_color;
    output.bg_color = cell.bg_color;
    output.flags = cell.flags;

    return output;
}

float4 PSMain(VS_OUTPUT input) : SV_Target {
    float4 color = input.bg_color;

    bool has_glyph = (input.flags & 1u) != 0u;
    bool is_color  = (input.flags & 2u) != 0u;

    if (has_glyph) {
        if (is_color) {
            float4 glyph_color = atlas_bgra.Sample(atlas_sampler, input.texCoord);
            color = lerp(color, glyph_color, glyph_color.a);
        } else {
            float alpha = atlas_r8.Sample(atlas_sampler, input.texCoord).r;
            color = lerp(color, input.fg_color, alpha);
        }
    }

    return color;
}
)";

} // namespace

struct D3DTextRenderer::Impl {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;

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

    bool buildShaders() {
        // Compile vertex shader
        ID3DBlob* vsBlob = nullptr;
        ID3DBlob* errorBlob = nullptr;
        HRESULT hr = D3DCompile(
            kCellShaderSource, strlen(kCellShaderSource),
            "cell.hlsl", nullptr, nullptr,
            "VSMain", "vs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
            &vsBlob, &errorBlob);
        if (errorBlob) errorBlob->Release();
        if (FAILED(hr)) return false;

        hr = device->CreateVertexShader(
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
            nullptr, &vertexShader);
        vsBlob->Release();
        if (FAILED(hr)) return false;

        // Compile pixel shader
        hr = D3DCompile(
            kCellShaderSource, strlen(kCellShaderSource),
            "cell.hlsl", nullptr, nullptr,
            "PSMain", "ps_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
            &vsBlob, &errorBlob);
        if (errorBlob) errorBlob->Release();
        if (FAILED(hr)) return false;

        hr = device->CreatePixelShader(
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
            nullptr, &pixelShader);
        vsBlob->Release();
        if (FAILED(hr)) return false;

        return true;
    }

    bool createResources() {
        // Constant buffer
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = sizeof(CellConstants);
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = device->CreateBuffer(&cbDesc, nullptr, &constantBuffer);
        if (FAILED(hr)) return false;

        // Sampler (nearest neighbor for pixel-perfect text)
        D3D11_SAMPLER_DESC sampDesc = {};
        sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

        hr = device->CreateSamplerState(&sampDesc, &sampler);
        if (FAILED(hr)) return false;

        // Blend state
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask =
            D3D11_COLOR_WRITE_ENABLE_ALL;

        hr = device->CreateBlendState(&blendDesc, &blendState);
        if (FAILED(hr)) return false;

        return true;
    }

    void ensureCellBuffer(UINT requiredCount) {
        if (cellBufferCapacity >= requiredCount) return;

        if (cellBufferSRV) { cellBufferSRV->Release(); cellBufferSRV = nullptr; }
        if (cellBuffer) { cellBuffer->Release(); cellBuffer = nullptr; }

        cellBufferCapacity = requiredCount;

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = requiredCount * sizeof(D3DCellInstance);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(D3DCellInstance);

        device->CreateBuffer(&desc, nullptr, &cellBuffer);

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = requiredCount;

        device->CreateShaderResourceView(
            cellBuffer, &srvDesc, &cellBufferSRV);
    }

    static void colorFromRGBA(uint32_t rgba, float out[4]) {
        out[0] = static_cast<float>((rgba >> 16) & 0xFF) / 255.0f;
        out[1] = static_cast<float>((rgba >> 8) & 0xFF) / 255.0f;
        out[2] = static_cast<float>(rgba & 0xFF) / 255.0f;
        out[3] = 1.0f;
    }

    void buildCellBuffer(const Screen& screen) {
        if (!fontCollection || !glyphCache || !glyphAtlas || !rasterizer) {
            return;
        }

        FontMetrics metrics = fontCollection->primaryMetrics();
        float cellW = metrics.cell_width;
        float cellH = metrics.cell_height;
        float fontSize = fontCollection->fontSize();

        int rows = screen.rows();
        int cols = screen.cols();

        cellInstances.clear();
        cellInstances.reserve(rows * cols);

        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                const TermCell& cell = screen.cellAt(row, col);

                D3DCellInstance inst = {};
                inst.position[0] = col * cellW;
                inst.position[1] = row * cellH;

                colorFromRGBA(cell.fg_color, inst.fg_color);
                colorFromRGBA(cell.bg_color, inst.bg_color);

                if (cell.attributes & AttrInverse) {
                    std::swap(inst.fg_color[0], inst.bg_color[0]);
                    std::swap(inst.fg_color[1], inst.bg_color[1]);
                    std::swap(inst.fg_color[2], inst.bg_color[2]);
                    std::swap(inst.fg_color[3], inst.bg_color[3]);
                }

                inst.flags = 0;

                if (cell.codepoint != ' ' && cell.codepoint != 0) {
                    CollectionFaceId faceId =
                        fontCollection->resolveFace(cell.codepoint);
                    if (faceId != kInvalidCollectionFace) {
                        FontFaceId rastFace =
                            fontCollection->rasterizerFaceId(faceId);
                        uint32_t glyphIdx =
                            rasterizer->getGlyphIndex(rastFace,
                                                      cell.codepoint);
                        if (glyphIdx != 0) {
                            GlyphKey key{rastFace, glyphIdx, {0, 0}};
                            auto info = glyphCache->getOrRasterize(
                                key, fontSize, *rasterizer, *glyphAtlas);
                            if (info) {
                                inst.flags |= 1;
                                if (info->is_color) inst.flags |= 2;
                                inst.atlas_uv[0] =
                                    static_cast<float>(info->region.x);
                                inst.atlas_uv[1] =
                                    static_cast<float>(info->region.y);
                                inst.atlas_size[0] =
                                    static_cast<float>(info->region.width);
                                inst.atlas_size[1] =
                                    static_cast<float>(info->region.height);
                                inst.glyph_offset[0] =
                                    static_cast<float>(
                                        info->region.bearing_x);
                                inst.glyph_offset[1] =
                                    static_cast<float>(
                                        info->region.bearing_y);
                            }
                        }
                    }
                }

                cellInstances.push_back(inst);
            }
        }
    }

    void cleanup() {
        if (blendState) { blendState->Release(); blendState = nullptr; }
        if (sampler) { sampler->Release(); sampler = nullptr; }
        if (cellBufferSRV) {
            cellBufferSRV->Release();
            cellBufferSRV = nullptr;
        }
        if (cellBuffer) { cellBuffer->Release(); cellBuffer = nullptr; }
        if (constantBuffer) {
            constantBuffer->Release();
            constantBuffer = nullptr;
        }
        if (pixelShader) { pixelShader->Release(); pixelShader = nullptr; }
        if (vertexShader) {
            vertexShader->Release();
            vertexShader = nullptr;
        }
    }
};

D3DTextRenderer::D3DTextRenderer()
    : impl_(std::make_unique<Impl>()) {}

D3DTextRenderer::~D3DTextRenderer() {
    impl_->cleanup();
}

bool D3DTextRenderer::initialize(ID3D11Device* device,
                                  ID3D11DeviceContext* context) {
    impl_->device = device;
    impl_->context = context;

    impl_->atlasUploader = std::make_unique<D3DAtlasUploader>();
    impl_->atlasUploader->setDevice(device, context);

    if (!impl_->buildShaders()) return false;
    if (!impl_->createResources()) return false;
    return true;
}

void D3DTextRenderer::setRenderTarget(ID3D11RenderTargetView* rtv) {
    impl_->rtv = rtv;
}

void D3DTextRenderer::setFontStack(FontCollection* collection,
                                    GlyphCache* cache,
                                    GlyphAtlas* atlas,
                                    IFontRasterizer* rasterizer) {
    impl_->fontCollection = collection;
    impl_->glyphCache = cache;
    impl_->glyphAtlas = atlas;
    impl_->rasterizer = rasterizer;
}

void D3DTextRenderer::render(const Screen& screen) {
    if (!impl_->vertexShader || !impl_->context || !impl_->rtv) return;

    impl_->buildCellBuffer(screen);
    if (impl_->cellInstances.empty()) return;

    // Upload dirty atlas textures
    if (impl_->glyphAtlas) {
        impl_->atlasUploader->upload(*impl_->glyphAtlas);
    }

    auto count = static_cast<UINT>(impl_->cellInstances.size());

    // Ensure structured buffer is large enough
    impl_->ensureCellBuffer(count);

    // Map and upload cell instance data
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = impl_->context->Map(
        impl_->cellBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        memcpy(mapped.pData, impl_->cellInstances.data(),
               count * sizeof(D3DCellInstance));
        impl_->context->Unmap(impl_->cellBuffer, 0);
    }

    // Update constant buffer
    Impl::CellConstants constants = {};
    constants.viewport_size[0] = impl_->viewportWidth;
    constants.viewport_size[1] = impl_->viewportHeight;

    if (impl_->fontCollection) {
        FontMetrics m = impl_->fontCollection->primaryMetrics();
        constants.cell_size[0] = m.cell_width;
        constants.cell_size[1] = m.cell_height;
    }

    if (impl_->glyphAtlas) {
        const AtlasPage* r8Page =
            impl_->glyphAtlas->getPage(AtlasFormat::R8);
        if (r8Page) {
            constants.atlas_size[0] =
                static_cast<float>(r8Page->width());
            constants.atlas_size[1] =
                static_cast<float>(r8Page->height());
        }
    }

    D3D11_MAPPED_SUBRESOURCE cbMapped = {};
    hr = impl_->context->Map(
        impl_->constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &cbMapped);
    if (SUCCEEDED(hr)) {
        memcpy(cbMapped.pData, &constants, sizeof(constants));
        impl_->context->Unmap(impl_->constantBuffer, 0);
    }

    // Clear render target
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    impl_->context->ClearRenderTargetView(impl_->rtv, clearColor);

    // Set render state
    impl_->context->OMSetRenderTargets(1, &impl_->rtv, nullptr);
    impl_->context->OMSetBlendState(impl_->blendState, nullptr, 0xFFFFFFFF);

    // Viewport
    D3D11_VIEWPORT vp = {};
    vp.Width = impl_->viewportWidth;
    vp.Height = impl_->viewportHeight;
    vp.MaxDepth = 1.0f;
    impl_->context->RSSetViewports(1, &vp);

    // Shaders
    impl_->context->VSSetShader(impl_->vertexShader, nullptr, 0);
    impl_->context->PSSetShader(impl_->pixelShader, nullptr, 0);

    // Constant buffer to both stages
    impl_->context->VSSetConstantBuffers(0, 1, &impl_->constantBuffer);
    impl_->context->PSSetConstantBuffers(0, 1, &impl_->constantBuffer);

    // Bind atlas SRVs to pixel shader (t0=R8, t1=BGRA)
    ID3D11ShaderResourceView* psSRVs[2] = {
        impl_->atlasUploader->srvForFormat(AtlasFormat::R8),
        impl_->atlasUploader->srvForFormat(AtlasFormat::BGRA)
    };
    impl_->context->PSSetShaderResources(0, 2, psSRVs);

    // Bind cell buffer SRV to vertex shader (t2)
    impl_->context->VSSetShaderResources(2, 1, &impl_->cellBufferSRV);

    // Bind sampler to pixel shader
    impl_->context->PSSetSamplers(0, 1, &impl_->sampler);

    // No input layout needed: vertices generated from SV_VertexID
    impl_->context->IASetInputLayout(nullptr);
    impl_->context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Instanced draw: 6 vertices per quad, N instances
    impl_->context->DrawInstanced(6, count, 0, 0);
}

void D3DTextRenderer::resize(float width, float height) {
    impl_->viewportWidth = width;
    impl_->viewportHeight = height;
}

} // namespace termcore

#endif // _WIN32
