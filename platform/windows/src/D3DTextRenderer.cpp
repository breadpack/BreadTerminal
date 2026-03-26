#if defined(_WIN32)

#include "D3DTextRendererImpl.h"

namespace termcore {

// HLSL shader source — two-pass cell rendering (background + glyph + cursor).
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
    uint   flags;        // bit0=has_glyph, bit1=is_color, bit2=is_bg_pass, bit3=is_cursor, bit4=is_underline, bit5=is_rounded_rect_top
    uint   extra_flags;  // bits 0-2: underline_style; bits 16-31: corner_radius * 16 (fixed-point)
};

StructuredBuffer<CellInstance> cells : register(t2);

struct VS_OUTPUT {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
    float4 fg_color : COLOR0;
    float4 bg_color : COLOR1;
    nointerpolation float2 quad_size_px : TEXCOORD1;
    nointerpolation float  corner_radius : TEXCOORD2;
    uint   flags       : BLENDINDICES0;
    uint   extra_flags : BLENDINDICES1;
};

VS_OUTPUT VSMain(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID) {
    VS_OUTPUT output;

    float2 corners[6] = {
        float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0),
        float2(1.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)
    };
    float2 corner = corners[vertex_id];

    CellInstance cell = cells[instance_id];

    bool is_bg = (cell.flags & 4u) != 0u;
    bool is_rounded = (cell.flags & 32u) != 0u;

    float2 quad_size = is_bg ? cell_size : cell.atlas_size_px;
    float2 pixel_pos = cell.position + corner * quad_size;

    float2 ndc = (pixel_pos / viewport_size) * 2.0 - 1.0;
    ndc.y = -ndc.y;

    output.position = float4(ndc, 0.0, 1.0);

    // Decode corner radius from upper 16 bits of extra_flags (fixed-point * 16)
    float radius = float(cell.extra_flags >> 16u) / 16.0;

    if (is_rounded) {
        // For rounded rects, pass normalized quad UV and pixel size
        output.texCoord = corner;
        output.quad_size_px = quad_size;
        output.corner_radius = radius;
    } else {
        output.texCoord = (cell.atlas_uv + corner * cell.atlas_size_px) / atlas_size;
        output.quad_size_px = float2(0.0, 0.0);
        output.corner_radius = 0.0;
    }

    bool is_underline = (cell.flags & 16u) != 0u;
    output.fg_color = is_underline ? float4(corner.x, corner.y, 0.0, 0.0) : cell.fg_color;
    output.bg_color = cell.bg_color;
    output.flags = cell.flags;
    output.extra_flags = cell.extra_flags;

    return output;
}

float4 PSMain(VS_OUTPUT input) : SV_Target {
    bool is_bg     = (input.flags & 4u) != 0u;
    bool has_glyph = (input.flags & 1u) != 0u;
    bool is_color  = (input.flags & 2u) != 0u;
    bool is_cursor = (input.flags & 8u) != 0u;
    bool is_rounded = (input.flags & 32u) != 0u;

    if (is_rounded) {
        // SDF-based rounded rectangle with top corners only
        float2 size = input.quad_size_px;
        float radius = input.corner_radius;
        // UV is 0..1 across the quad
        float2 px = input.texCoord * size;

        // Distance from each edge
        float2 halfSize = size * 0.5;
        float2 p = abs(px - halfSize);

        // Only round top corners: apply radius when in top half, 0 for bottom
        float r = (input.texCoord.y < 0.5) ? radius : 0.0;
        float2 q = p - halfSize + float2(r, r);
        float d = length(max(q, float2(0.0, 0.0))) - r;

        // Anti-aliased edge (1px smooth transition)
        float alpha = 1.0 - smoothstep(-0.5, 0.5, d);

        // Apply alpha to premultiplied color
        float4 col = input.bg_color;
        return float4(col.rgb * alpha, col.a * alpha);
    }

    if (is_bg) {
        return input.bg_color;
    }

    if (is_cursor) {
        return input.bg_color;
    }

    bool is_underline = (input.flags & 16u) != 0u;
    if (is_underline) {
        uint ul_style = input.extra_flags & 7u;
        float local_x = input.fg_color.x;  // 0..1 across underline width
        float local_y = input.fg_color.y;  // 0..1 across underline height

        if (ul_style == 3u) { // curly - sine wave
            float wave = sin(local_x * 3.14159 * 2.0) * 0.35 + 0.5;
            float dist = abs(local_y - wave);
            float a = 1.0 - smoothstep(0.0, 0.3, dist);
            return float4(input.bg_color.rgb * a, a);
        }
        if (ul_style == 4u) { // dotted
            float a = step(0.5, frac(local_x * 4.0));
            return float4(input.bg_color.rgb * a, a);
        }
        if (ul_style == 5u) { // dashed
            float a = step(0.33, frac(local_x * 2.0));
            return float4(input.bg_color.rgb * a, a);
        }
        return input.bg_color; // single, double = solid (already premultiplied)
    }

    float4 color = float4(0.0, 0.0, 0.0, 0.0);
    if (has_glyph) {
        if (is_color) {
            // Color emoji: already in BGRA, premultiply
            float4 tex = atlas_bgra.Sample(atlas_sampler, input.texCoord);
            color = float4(tex.rgb * tex.a, tex.a);
        } else {
            // Mono glyph: alpha from R8 atlas, premultiply fg color
            float alpha = atlas_r8.Sample(atlas_sampler, input.texCoord).r;
            color = float4(input.fg_color.rgb * alpha, alpha);
        }
    }
    return color;
}
)";

// --- Impl: shader/resource setup ---

bool D3DTextRenderer::Impl::buildShaders() {
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    HRESULT hr = D3DCompile(
        kCellShaderSource, strlen(kCellShaderSource),
        "cell.hlsl", nullptr, nullptr,
        "VSMain", "vs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
        &vsBlob, &errorBlob);
    if (errorBlob) {
        OutputDebugStringA("[BreadTerminal] VS compile error: ");
        OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
        OutputDebugStringA("\n");
        errorBlob->Release();
    }
    if (FAILED(hr)) return false;

    hr = device->CreateVertexShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
        nullptr, &vertexShader);
    vsBlob->Release();
    if (FAILED(hr)) return false;

    hr = D3DCompile(
        kCellShaderSource, strlen(kCellShaderSource),
        "cell.hlsl", nullptr, nullptr,
        "PSMain", "ps_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
        &vsBlob, &errorBlob);
    if (errorBlob) {
        OutputDebugStringA("[BreadTerminal] PS compile error: ");
        OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
        OutputDebugStringA("\n");
        errorBlob->Release();
    }
    if (FAILED(hr)) return false;

    hr = device->CreatePixelShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
        nullptr, &pixelShader);
    vsBlob->Release();
    if (FAILED(hr)) return false;

    return true;
}

bool D3DTextRenderer::Impl::createResources() {
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(CellConstants);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = device->CreateBuffer(&cbDesc, nullptr, &constantBuffer);
    if (FAILED(hr)) return false;

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

    hr = device->CreateSamplerState(&sampDesc, &sampler);
    if (FAILED(hr)) return false;

    // Premultiplied alpha blending — required for DirectComposition transparency.
    // Source colors are pre-multiplied by their alpha, so SrcBlend = ONE.
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = device->CreateBlendState(&blendDesc, &blendState);
    if (FAILED(hr)) return false;

    return true;
}

void D3DTextRenderer::Impl::ensureCellBuffer(UINT requiredCount) {
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

void D3DTextRenderer::Impl::cleanup() {
    if (blendState) { blendState->Release(); blendState = nullptr; }
    if (sampler) { sampler->Release(); sampler = nullptr; }
    if (cellBufferSRV) { cellBufferSRV->Release(); cellBufferSRV = nullptr; }
    if (cellBuffer) { cellBuffer->Release(); cellBuffer = nullptr; }
    if (constantBuffer) { constantBuffer->Release(); constantBuffer = nullptr; }
    if (pixelShader) { pixelShader->Release(); pixelShader = nullptr; }
    if (vertexShader) { vertexShader->Release(); vertexShader = nullptr; }
    imageRenderer.cleanup();
}

// --- Public API ---

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

    impl_->imageRenderer.initialize(device, context);

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
    if (!impl_->context || !impl_->rtv) return;

    // Always clear to opaque background even if shaders failed,
    // so the window is never invisible with DirectComposition.
    if (!impl_->vertexShader) {
        uint32_t bg = screen.dynamicColors().background;
        float fallback[4] = {
            static_cast<float>((bg >> 16) & 0xFF) / 255.0f,
            static_cast<float>((bg >> 8) & 0xFF) / 255.0f,
            static_cast<float>(bg & 0xFF) / 255.0f,
            1.0f
        };
        impl_->context->ClearRenderTargetView(impl_->rtv, fallback);
        return;
    }

    // Determine if only cursor blink changed (no content rebuild needed)
    bool blinkChanged = (impl_->cursorBlinkVisible != impl_->lastBlinkState);

    if (!impl_->contentDirty && blinkChanged
        && impl_->cellCountBeforeCursor > 0
        && !impl_->cellInstances.empty()) {
        // Only cursor blink toggled -- patch cursor without full rebuild
        impl_->patchCursorOnly(screen);
    } else {
        impl_->buildCellBuffer(screen);
        impl_->contentDirty = false;
    }
    impl_->lastBlinkState = impl_->cursorBlinkVisible;

    if (impl_->cellInstances.empty()) return;

    if (impl_->glyphAtlas) {
        impl_->atlasUploader->upload(*impl_->glyphAtlas);
    }

    auto count = static_cast<UINT>(impl_->cellInstances.size());
    impl_->ensureCellBuffer(count);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = impl_->context->Map(
        impl_->cellBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        memcpy(mapped.pData, impl_->cellInstances.data(),
               count * sizeof(D3DCellInstance));
        impl_->context->Unmap(impl_->cellBuffer, 0);
    }

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

    // Clear to transparent — bg cells and margin quads provide all opacity.
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    impl_->context->ClearRenderTargetView(impl_->rtv, clearColor);

    impl_->context->OMSetRenderTargets(1, &impl_->rtv, nullptr);
    impl_->context->OMSetBlendState(impl_->blendState, nullptr, 0xFFFFFFFF);

    D3D11_VIEWPORT vp = {};
    vp.Width = impl_->viewportWidth;
    vp.Height = impl_->viewportHeight;
    vp.MaxDepth = 1.0f;
    impl_->context->RSSetViewports(1, &vp);

    impl_->context->VSSetShader(impl_->vertexShader, nullptr, 0);
    impl_->context->PSSetShader(impl_->pixelShader, nullptr, 0);

    impl_->context->VSSetConstantBuffers(0, 1, &impl_->constantBuffer);
    impl_->context->PSSetConstantBuffers(0, 1, &impl_->constantBuffer);

    ID3D11ShaderResourceView* psSRVs[2] = {
        impl_->atlasUploader->srvForFormat(AtlasFormat::R8),
        impl_->atlasUploader->srvForFormat(AtlasFormat::BGRA)
    };
    impl_->context->PSSetShaderResources(0, 2, psSRVs);

    impl_->context->VSSetShaderResources(2, 1, &impl_->cellBufferSRV);
    impl_->context->PSSetSamplers(0, 1, &impl_->sampler);

    impl_->context->IASetInputLayout(nullptr);
    impl_->context->IASetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    impl_->context->DrawInstanced(6, count, 0, 0);

    // Render Kitty graphics images on top of text cells
    const auto& gfx = screen.kittyGraphics();
    if (!gfx.placements().empty()) {
        impl_->imageRenderer.syncImages(gfx);
        impl_->imageRenderer.renderPlacements(
            gfx, constants.cell_size[0], constants.cell_size[1],
            impl_->viewportWidth, impl_->viewportHeight,
            impl_->rtv);
    }
}

void D3DTextRenderer::resize(float width, float height) {
    impl_->viewportWidth = width;
    impl_->viewportHeight = height;
    impl_->contentDirty = true;
}

void D3DTextRenderer::setSelection(const Selection& sel) {
    impl_->selection = sel;
    impl_->contentDirty = true;
}

void D3DTextRenderer::setCursorBlink(bool visible) {
    impl_->cursorBlinkVisible = visible;
}

void D3DTextRenderer::setIMEActive(bool active) {
    if (impl_->imeActive != active) {
        impl_->imeActive = active;
        impl_->contentDirty = true;
    }
}

void D3DTextRenderer::markContentDirty() {
    impl_->contentDirty = true;
}

IAtlasUploader* D3DTextRenderer::atlasUploader() {
    return impl_->atlasUploader.get();
}

void D3DTextRenderer::setSearchHighlights(
        const std::vector<SearchHighlight>& highlights, int currentIndex) {
    impl_->searchHighlights = highlights;
    impl_->searchCurrentIndex = currentIndex;
    impl_->rebuildSearchIndex();
    impl_->contentDirty = true;
}

void D3DTextRenderer::setUrlHighlights(const std::vector<UrlHighlight>& highlights) {
    impl_->urlHighlights = highlights;
    impl_->rebuildUrlIndex();
    impl_->contentDirty = true;
}

void D3DTextRenderer::setBackgroundOpacity(float opacity) {
    impl_->backgroundOpacity = (std::max)(0.0f, (std::min)(1.0f, opacity));
    impl_->contentDirty = true;
}

void D3DTextRenderer::setGhostText(const std::string& text, int row, int col) {
    impl_->ghostText.text = text;
    impl_->ghostText.row = row;
    impl_->ghostText.col = col;
    impl_->contentDirty = true;
}

void D3DTextRenderer::setStatusBar(const StatusBarInfo& info) {
    impl_->statusBar = info;
    impl_->contentDirty = true;
}

void D3DTextRenderer::setResizeOverlay(bool visible, int cols, int rows) {
    impl_->resizeOverlayVisible = visible;
    impl_->resizeOverlayCols = cols;
    impl_->resizeOverlayRows = rows;
    impl_->contentDirty = true;
}

void D3DTextRenderer::setTabBar(const TabBarInfo& info) {
    impl_->tabBar = info;
    impl_->contentDirty = true;
}

D3DTextRenderer::TabBarInfo D3DTextRenderer::getTabBar() const {
    return impl_->tabBar;
}

void D3DTextRenderer::setPaneBorders(const PaneBorderInfo& info) {
    impl_->paneBorders = info;
    impl_->contentDirty = true;
}

void D3DTextRenderer::setPaneProgress(PaneId pane_id, const PaneProgressInfo& info) {
    if (info.progress < 0.0f) {
        impl_->paneProgress.erase(pane_id);
    } else {
        impl_->paneProgress[pane_id] = info;
    }
    impl_->contentDirty = true;
}

void D3DTextRenderer::setPaneStatusPills(PaneId pane_id,
                                          const std::vector<StatusPillInfo>& pills) {
    if (pills.empty()) {
        impl_->paneStatusPills.erase(pane_id);
    } else {
        impl_->paneStatusPills[pane_id] = pills;
    }
    impl_->contentDirty = true;
}

void D3DTextRenderer::setCommandPalette(const CommandPaletteInfo& info) {
    impl_->commandPalette = info;
}

void D3DTextRenderer::setProfileDropdown(const ProfileDropdownInfo& info) {
    impl_->profileDropdown = info;
}

void D3DTextRenderer::setSidebar(const SidebarRenderInfo& info) {
    impl_->sidebar = info;
    impl_->contentDirty = true;
}

} // namespace termcore

#endif // _WIN32
