#if defined(_WIN32)

#include "D3DImageRenderer.h"
#include "termcore/kitty_graphics.h"

#include <d3dcompiler.h>
#include <cstring>

#pragma comment(lib, "d3dcompiler.lib")

namespace termcore {

// Simple image rendering shader
const char* kImageShaderSource = R"(
cbuffer ImageConstants : register(b0) {
    float2 viewport_size;
    float2 quad_pos;     // pixel position
    float2 quad_size;    // pixel size
    float2 _pad;
};

Texture2D image_tex : register(t0);
SamplerState image_sampler : register(s0);

struct VS_OUTPUT {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

VS_OUTPUT VSMain(uint vertex_id : SV_VertexID) {
    VS_OUTPUT output;

    float2 corners[6] = {
        float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0),
        float2(1.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)
    };
    float2 corner = corners[vertex_id];

    float2 pixel_pos = quad_pos + corner * quad_size;
    float2 ndc = (pixel_pos / viewport_size) * 2.0 - 1.0;
    ndc.y = -ndc.y;

    output.position = float4(ndc, 0.0, 1.0);
    output.texCoord = corner;

    return output;
}

float4 PSMain(VS_OUTPUT input) : SV_Target {
    return image_tex.Sample(image_sampler, input.texCoord);
}
)";

D3DImageRenderer::D3DImageRenderer() = default;

D3DImageRenderer::~D3DImageRenderer() {
    cleanup();
}

void D3DImageRenderer::initialize(ID3D11Device* device, ID3D11DeviceContext* context) {
    device_ = device;
    context_ = context;
    buildImageShaders();
    createImageResources();
}

bool D3DImageRenderer::buildImageShaders() {
    if (!device_) return false;

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = D3DCompile(kImageShaderSource, strlen(kImageShaderSource),
        "image.hlsl", nullptr, nullptr, "VSMain", "vs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errorBlob);
    if (errorBlob) errorBlob->Release();
    if (FAILED(hr)) return false;

    hr = device_->CreateVertexShader(vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(), nullptr, &imageVS_);
    vsBlob->Release();
    if (FAILED(hr)) return false;

    hr = D3DCompile(kImageShaderSource, strlen(kImageShaderSource),
        "image.hlsl", nullptr, nullptr, "PSMain", "ps_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errorBlob);
    if (errorBlob) errorBlob->Release();
    if (FAILED(hr)) return false;

    hr = device_->CreatePixelShader(vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(), nullptr, &imagePS_);
    vsBlob->Release();
    if (FAILED(hr)) return false;

    return true;
}

void D3DImageRenderer::createImageResources() {
    if (!device_) return;

    // Constant buffer
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = 32;  // 4 float2 = 32 bytes
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device_->CreateBuffer(&cbDesc, nullptr, &imageCB_);

    // Sampler
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device_->CreateSamplerState(&sampDesc, &imageSampler_);

    // Blend state (standard alpha blending)
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device_->CreateBlendState(&blendDesc, &imageBlend_);
}

void D3DImageRenderer::createImageTexture(uint32_t image_id,
                                            const uint8_t* data,
                                            int width, int height) {
    if (!device_ || width <= 0 || height <= 0) return;

    ImageTexture tex;
    tex.width = width;
    tex.height = height;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = data;
    initData.SysMemPitch = width * 4;

    HRESULT hr = device_->CreateTexture2D(&desc, &initData, &tex.texture);
    if (FAILED(hr)) return;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = device_->CreateShaderResourceView(tex.texture, &srvDesc, &tex.srv);
    if (FAILED(hr)) {
        tex.texture->Release();
        return;
    }

    textures_[image_id] = tex;
}

void D3DImageRenderer::destroyImageTexture(uint32_t image_id) {
    auto it = textures_.find(image_id);
    if (it == textures_.end()) return;
    if (it->second.srv) it->second.srv->Release();
    if (it->second.texture) it->second.texture->Release();
    textures_.erase(it);
}

void D3DImageRenderer::syncImages(const KittyGraphicsManager& gfx) {
    // Upload any images that don't have textures yet
    // Note: This is a simplified sync - a production version would track changes
    for (const auto& placement : gfx.placements()) {
        if (textures_.find(placement.image_id) != textures_.end()) continue;

        const KittyImage* img = gfx.getImage(placement.image_id);
        if (!img || !img->complete || img->data.empty()) continue;

        createImageTexture(placement.image_id, img->data.data(),
                           img->width, img->height);
    }
}

void D3DImageRenderer::renderPlacements(const KittyGraphicsManager& gfx,
                                          float cell_width, float cell_height,
                                          float viewport_width, float viewport_height,
                                          ID3D11RenderTargetView* rtv) {
    if (!context_ || !imageVS_ || !imagePS_ || gfx.placements().empty()) return;

    context_->OMSetRenderTargets(1, &rtv, nullptr);
    context_->OMSetBlendState(imageBlend_, nullptr, 0xFFFFFFFF);
    context_->VSSetShader(imageVS_, nullptr, 0);
    context_->PSSetShader(imagePS_, nullptr, 0);
    context_->PSSetSamplers(0, 1, &imageSampler_);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for (const auto& placement : gfx.placements()) {
        auto it = textures_.find(placement.image_id);
        if (it == textures_.end()) continue;

        const auto& tex = it->second;

        // Calculate quad position and size
        float px = placement.x * cell_width;
        float py = placement.y * cell_height;
        float pw, ph;

        if (placement.cols > 0 && placement.rows > 0) {
            pw = placement.cols * cell_width;
            ph = placement.rows * cell_height;
        } else {
            pw = static_cast<float>(tex.width);
            ph = static_cast<float>(tex.height);
        }

        // Update constant buffer
        struct {
            float viewport_size[2];
            float quad_pos[2];
            float quad_size[2];
            float _pad[2];
        } constants;

        constants.viewport_size[0] = viewport_width;
        constants.viewport_size[1] = viewport_height;
        constants.quad_pos[0] = px;
        constants.quad_pos[1] = py;
        constants.quad_size[0] = pw;
        constants.quad_size[1] = ph;
        constants._pad[0] = 0;
        constants._pad[1] = 0;

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = context_->Map(imageCB_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            memcpy(mapped.pData, &constants, sizeof(constants));
            context_->Unmap(imageCB_, 0);
        }

        context_->VSSetConstantBuffers(0, 1, &imageCB_);
        context_->PSSetShaderResources(0, 1, &tex.srv);

        context_->Draw(6, 0);
    }
}

void D3DImageRenderer::cleanup() {
    for (auto& [id, tex] : textures_) {
        if (tex.srv) tex.srv->Release();
        if (tex.texture) tex.texture->Release();
    }
    textures_.clear();

    if (imageBlend_) { imageBlend_->Release(); imageBlend_ = nullptr; }
    if (imageSampler_) { imageSampler_->Release(); imageSampler_ = nullptr; }
    if (imageCB_) { imageCB_->Release(); imageCB_ = nullptr; }
    if (imagePS_) { imagePS_->Release(); imagePS_ = nullptr; }
    if (imageVS_) { imageVS_->Release(); imageVS_ = nullptr; }
}

} // namespace termcore

#endif // _WIN32
