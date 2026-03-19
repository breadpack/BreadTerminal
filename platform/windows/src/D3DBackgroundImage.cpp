#if defined(_WIN32)

#include "D3DBackgroundImage.h"

#include <d3dcompiler.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstring>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;

namespace termcore {

// HLSL shader for background image rendering.
// VS generates a fullscreen quad from SV_VertexID.
// PS samples the texture with opacity and UV transform for scaling modes.
static const char* kBgShaderSource = R"(
cbuffer BgConstants : register(b0) {
    float2 viewport_size;
    float2 image_size;
    float2 uv_offset;
    float2 uv_scale;
    float  opacity;
    float3 _pad;
};

Texture2D bg_tex : register(t0);
SamplerState bg_sampler : register(s0);

struct VS_OUTPUT {
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

VS_OUTPUT VSMain(uint vertex_id : SV_VertexID) {
    VS_OUTPUT output;

    // Two-triangle fullscreen quad
    float2 corners[6] = {
        float2(-1.0, -1.0), float2( 1.0, -1.0), float2(-1.0,  1.0),
        float2( 1.0, -1.0), float2( 1.0,  1.0), float2(-1.0,  1.0)
    };
    float2 uvs[6] = {
        float2(0.0, 0.0), float2(1.0, 0.0), float2(0.0, 1.0),
        float2(1.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)
    };

    output.position = float4(corners[vertex_id], 0.0, 1.0);
    output.texCoord = uvs[vertex_id] * uv_scale + uv_offset;

    return output;
}

float4 PSMain(VS_OUTPUT input) : SV_Target {
    float4 color = bg_tex.Sample(bg_sampler, input.texCoord);
    color.a *= opacity;
    return color;
}
)";

// Constant buffer layout (must be 16-byte aligned, total 48 bytes = 3 x float4)
struct BgConstants {
    float viewport_size[2];
    float image_size[2];
    float uv_offset[2];
    float uv_scale[2];
    float opacity;
    float _pad[3];
};
static_assert(sizeof(BgConstants) == 48, "BgConstants must be 48 bytes");

D3DBackgroundImage::~D3DBackgroundImage() {
    cleanup();
}

void D3DBackgroundImage::initialize(ID3D11Device* device,
                                     ID3D11DeviceContext* context) {
    device_ = device;
    context_ = context;
    buildShaders();
    createResources();
}

bool D3DBackgroundImage::buildShaders() {
    if (!device_) return false;

    ID3DBlob* blob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    // Compile vertex shader
    HRESULT hr = D3DCompile(kBgShaderSource, strlen(kBgShaderSource),
        "bg.hlsl", nullptr, nullptr, "VSMain", "vs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errorBlob);
    if (errorBlob) errorBlob->Release();
    if (FAILED(hr)) return false;

    hr = device_->CreateVertexShader(blob->GetBufferPointer(),
        blob->GetBufferSize(), nullptr, &vs_);
    blob->Release();
    if (FAILED(hr)) return false;

    // Compile pixel shader
    hr = D3DCompile(kBgShaderSource, strlen(kBgShaderSource),
        "bg.hlsl", nullptr, nullptr, "PSMain", "ps_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errorBlob);
    if (errorBlob) errorBlob->Release();
    if (FAILED(hr)) return false;

    hr = device_->CreatePixelShader(blob->GetBufferPointer(),
        blob->GetBufferSize(), nullptr, &ps_);
    blob->Release();
    if (FAILED(hr)) return false;

    return true;
}

void D3DBackgroundImage::createResources() {
    if (!device_) return;

    // Constant buffer
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(BgConstants);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device_->CreateBuffer(&cbDesc, nullptr, &cb_);

    // Linear sampler with wrap addressing (needed for Tile mode)
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    device_->CreateSamplerState(&sampDesc, &sampler_);

    // Blend state for alpha blending
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device_->CreateBlendState(&blendDesc, &blend_);
}

bool D3DBackgroundImage::loadImage(const std::string& path) {
    if (!device_) return false;

    // Release previous image
    if (srv_) { srv_->Release(); srv_ = nullptr; }
    if (texture_) { texture_->Release(); texture_ = nullptr; }
    imageWidth_ = 0;
    imageHeight_ = 0;

    // Convert path to wide string
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(),
                                    static_cast<int>(path.size()), nullptr, 0);
    if (wlen <= 0) return false;
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(),
                        static_cast<int>(path.size()), &wpath[0], wlen);

    // Initialize WIC
    ComPtr<IWICImagingFactory> wicFactory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory));
    if (FAILED(hr)) return false;

    // Decode image file
    ComPtr<IWICBitmapDecoder> decoder;
    hr = wicFactory->CreateDecoderFromFilename(wpath.c_str(), nullptr,
        GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) return false;

    // Convert to RGBA 32bpp
    ComPtr<IWICFormatConverter> converter;
    hr = wicFactory->CreateFormatConverter(&converter);
    if (FAILED(hr)) return false;

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return false;

    UINT width = 0, height = 0;
    hr = converter->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0) return false;

    // Read pixel data
    std::vector<uint8_t> pixels(width * height * 4);
    hr = converter->CopyPixels(nullptr, width * 4,
        static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(hr)) return false;

    // Create D3D11 texture
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = pixels.data();
    initData.SysMemPitch = width * 4;

    hr = device_->CreateTexture2D(&texDesc, &initData, &texture_);
    if (FAILED(hr)) return false;

    // Create shader resource view
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = device_->CreateShaderResourceView(texture_, &srvDesc, &srv_);
    if (FAILED(hr)) {
        texture_->Release();
        texture_ = nullptr;
        return false;
    }

    imageWidth_ = static_cast<int>(width);
    imageHeight_ = static_cast<int>(height);
    return true;
}

void D3DBackgroundImage::render(float viewportW, float viewportH,
                                 float opacity, Mode mode,
                                 ID3D11RenderTargetView* rtv) {
    if (!context_ || !vs_ || !ps_ || !srv_) return;
    if (opacity <= 0.0f) return;

    float imgW = static_cast<float>(imageWidth_);
    float imgH = static_cast<float>(imageHeight_);

    // Calculate UV offset and scale based on mode
    float uvOffsetX = 0.0f, uvOffsetY = 0.0f;
    float uvScaleX = 1.0f, uvScaleY = 1.0f;

    switch (mode) {
    case Mode::Stretch:
        // Map UV [0,1] to full image; quad is already fullscreen
        break;

    case Mode::Fill: {
        // Scale image to cover viewport, crop excess (no letterboxing)
        float scaleX = viewportW / imgW;
        float scaleY = viewportH / imgH;
        float scale = (scaleX > scaleY) ? scaleX : scaleY;
        float visW = viewportW / scale;
        float visH = viewportH / scale;
        uvScaleX = visW / imgW;
        uvScaleY = visH / imgH;
        uvOffsetX = (1.0f - uvScaleX) * 0.5f;
        uvOffsetY = (1.0f - uvScaleY) * 0.5f;
        break;
    }

    case Mode::Fit: {
        // Scale image to fit within viewport, letterbox the rest
        float scaleX = viewportW / imgW;
        float scaleY = viewportH / imgH;
        float scale = (scaleX < scaleY) ? scaleX : scaleY;
        float visW = viewportW / scale;
        float visH = viewportH / scale;
        uvScaleX = visW / imgW;
        uvScaleY = visH / imgH;
        uvOffsetX = (1.0f - uvScaleX) * 0.5f;
        uvOffsetY = (1.0f - uvScaleY) * 0.5f;
        break;
    }

    case Mode::Center: {
        // Display at native resolution, centered
        uvScaleX = viewportW / imgW;
        uvScaleY = viewportH / imgH;
        uvOffsetX = (1.0f - uvScaleX) * 0.5f;
        uvOffsetY = (1.0f - uvScaleY) * 0.5f;
        break;
    }

    case Mode::Tile: {
        // Repeat the image across the viewport
        uvScaleX = viewportW / imgW;
        uvScaleY = viewportH / imgH;
        uvOffsetX = 0.0f;
        uvOffsetY = 0.0f;
        break;
    }
    }

    // Update constant buffer
    BgConstants constants;
    constants.viewport_size[0] = viewportW;
    constants.viewport_size[1] = viewportH;
    constants.image_size[0] = imgW;
    constants.image_size[1] = imgH;
    constants.uv_offset[0] = uvOffsetX;
    constants.uv_offset[1] = uvOffsetY;
    constants.uv_scale[0] = uvScaleX;
    constants.uv_scale[1] = uvScaleY;
    constants.opacity = (opacity < 0.0f) ? 0.0f : (opacity > 1.0f) ? 1.0f : opacity;
    constants._pad[0] = 0;
    constants._pad[1] = 0;
    constants._pad[2] = 0;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = context_->Map(cb_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        memcpy(mapped.pData, &constants, sizeof(constants));
        context_->Unmap(cb_, 0);
    }

    // Set pipeline state and draw
    context_->OMSetRenderTargets(1, &rtv, nullptr);
    context_->OMSetBlendState(blend_, nullptr, 0xFFFFFFFF);
    context_->VSSetShader(vs_, nullptr, 0);
    context_->PSSetShader(ps_, nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, &cb_);
    context_->PSSetShaderResources(0, 1, &srv_);
    context_->PSSetSamplers(0, 1, &sampler_);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    context_->Draw(6, 0);
}

void D3DBackgroundImage::cleanup() {
    if (srv_) { srv_->Release(); srv_ = nullptr; }
    if (texture_) { texture_->Release(); texture_ = nullptr; }
    if (blend_) { blend_->Release(); blend_ = nullptr; }
    if (sampler_) { sampler_->Release(); sampler_ = nullptr; }
    if (cb_) { cb_->Release(); cb_ = nullptr; }
    if (ps_) { ps_->Release(); ps_ = nullptr; }
    if (vs_) { vs_->Release(); vs_ = nullptr; }
    imageWidth_ = 0;
    imageHeight_ = 0;
}

} // namespace termcore

#endif // _WIN32
