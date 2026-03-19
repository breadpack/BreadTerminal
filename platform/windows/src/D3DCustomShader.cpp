#if defined(_WIN32)

#include "D3DCustomShader.h"

#include <d3dcompiler.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#pragma comment(lib, "d3dcompiler.lib")

namespace fs = std::filesystem;

namespace termcore {

// Built-in fullscreen triangle vertex shader.
// Generates a full-screen triangle from SV_VertexID (3 vertices, no VB).
static const char* kFullscreenVSSource = R"(
struct VS_OUTPUT {
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

VS_OUTPUT VSMain(uint id : SV_VertexID) {
    VS_OUTPUT o;
    // Full-screen triangle: id 0,1,2 -> covers [-1,1] range
    o.uv = float2((id << 1) & 2, id & 2);
    o.position = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}
)";

// Params constant buffer layout (must match the HLSL template)
struct alignas(16) ShaderParams {
    float resolution[2];  // float2
    float time;           // float
    float _pad;           // padding to 16 bytes
};

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

static uint64_t fileLastWriteTime(const std::string& path) {
    std::error_code ec;
    auto ftime = fs::last_write_time(path, ec);
    if (ec) return 0;
    return static_cast<uint64_t>(
        ftime.time_since_epoch().count());
}

static std::string readFileContents(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

// --------------------------------------------------------------------------
// D3DCustomShader
// --------------------------------------------------------------------------

D3DCustomShader::D3DCustomShader() = default;

D3DCustomShader::~D3DCustomShader() {
    cleanup();
}

void D3DCustomShader::initialize(ID3D11Device* device,
                                  ID3D11DeviceContext* context) {
    device_  = device;
    context_ = context;
    buildFullscreenVS();
    createResources();
}

bool D3DCustomShader::isLoaded() const {
    return customPS_ != nullptr;
}

// --------------------------------------------------------------------------
// Shader compilation
// --------------------------------------------------------------------------

bool D3DCustomShader::buildFullscreenVS() {
    if (!device_) return false;

    ID3DBlob* vsBlob    = nullptr;
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = D3DCompile(
        kFullscreenVSSource,
        strlen(kFullscreenVSSource),
        "fullscreen.hlsl", nullptr, nullptr,
        "VSMain", "vs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
        &vsBlob, &errorBlob);

    if (errorBlob) errorBlob->Release();
    if (FAILED(hr)) return false;

    hr = device_->CreateVertexShader(
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        nullptr, &fullscreenVS_);

    vsBlob->Release();
    return SUCCEEDED(hr);
}

bool D3DCustomShader::compilePixelShader(const std::string& source) {
    if (!device_ || source.empty()) return false;

    ID3DBlob* psBlob    = nullptr;
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = D3DCompile(
        source.c_str(),
        source.size(),
        shaderPath_.c_str(), nullptr, nullptr,
        "PSMain", "ps_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
        &psBlob, &errorBlob);

    if (errorBlob) errorBlob->Release();
    if (FAILED(hr)) return false;

    // Release previous PS if any
    if (customPS_) {
        customPS_->Release();
        customPS_ = nullptr;
    }

    hr = device_->CreatePixelShader(
        psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(),
        nullptr, &customPS_);

    psBlob->Release();
    return SUCCEEDED(hr);
}

// --------------------------------------------------------------------------
// Resource creation
// --------------------------------------------------------------------------

void D3DCustomShader::createResources() {
    if (!device_) return;

    // Constant buffer (16 bytes: float2 + float + pad)
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth      = sizeof(ShaderParams);
    cbDesc.Usage           = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
    device_->CreateBuffer(&cbDesc, nullptr, &paramsCB_);

    // Linear-clamp sampler
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device_->CreateSamplerState(&sampDesc, &sampler_);
}

// --------------------------------------------------------------------------
// Load / hot-reload
// --------------------------------------------------------------------------

bool D3DCustomShader::loadShader(const std::string& path) {
    std::string source = readFileContents(path);
    if (source.empty()) return false;

    shaderPath_    = path;
    lastWriteTime_ = fileLastWriteTime(path);
    frameCount_    = 0;

    return compilePixelShader(source);
}

void D3DCustomShader::checkForReload() {
    if (shaderPath_.empty()) return;

    uint64_t currentTime = fileLastWriteTime(shaderPath_);
    if (currentTime == 0 || currentTime == lastWriteTime_) return;

    std::string source = readFileContents(shaderPath_);
    if (source.empty()) return;

    // Attempt recompile; keep old shader on failure
    ID3DBlob* psBlob    = nullptr;
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = D3DCompile(
        source.c_str(), source.size(),
        shaderPath_.c_str(), nullptr, nullptr,
        "PSMain", "ps_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
        &psBlob, &errorBlob);

    if (errorBlob) errorBlob->Release();

    if (SUCCEEDED(hr) && psBlob) {
        ID3D11PixelShader* newPS = nullptr;
        hr = device_->CreatePixelShader(
            psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(),
            nullptr, &newPS);
        psBlob->Release();

        if (SUCCEEDED(hr) && newPS) {
            if (customPS_) customPS_->Release();
            customPS_   = newPS;
            frameCount_ = 0;
        }
    }

    lastWriteTime_ = currentTime;
}

// --------------------------------------------------------------------------
// Render
// --------------------------------------------------------------------------

void D3DCustomShader::render(ID3D11ShaderResourceView* sceneSRV,
                              ID3D11RenderTargetView* outputRTV,
                              float viewportW, float viewportH,
                              float time) {
    if (!context_ || !fullscreenVS_ || !customPS_) return;

    // Update constant buffer
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = context_->Map(paramsCB_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        ShaderParams params;
        params.resolution[0] = viewportW;
        params.resolution[1] = viewportH;
        params.time          = time;
        params._pad          = static_cast<float>(frameCount_);
        memcpy(mapped.pData, &params, sizeof(params));
        context_->Unmap(paramsCB_, 0);
    }

    // Set pipeline state
    context_->OMSetRenderTargets(1, &outputRTV, nullptr);
    context_->VSSetShader(fullscreenVS_, nullptr, 0);
    context_->PSSetShader(customPS_, nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, &paramsCB_);
    context_->PSSetShaderResources(0, 1, &sceneSRV);
    context_->PSSetSamplers(0, 1, &sampler_);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Set viewport
    D3D11_VIEWPORT vp = {};
    vp.Width    = viewportW;
    vp.Height   = viewportH;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &vp);

    // Draw fullscreen triangle (3 vertices, no vertex buffer)
    context_->Draw(3, 0);

    // Unbind SRV so the texture can be used as a render target again
    ID3D11ShaderResourceView* nullSRV = nullptr;
    context_->PSSetShaderResources(0, 1, &nullSRV);

    ++frameCount_;
}

// --------------------------------------------------------------------------
// Cleanup
// --------------------------------------------------------------------------

void D3DCustomShader::cleanup() {
    if (sampler_)      { sampler_->Release();      sampler_      = nullptr; }
    if (paramsCB_)     { paramsCB_->Release();     paramsCB_     = nullptr; }
    if (customPS_)     { customPS_->Release();     customPS_     = nullptr; }
    if (fullscreenVS_) { fullscreenVS_->Release(); fullscreenVS_ = nullptr; }

    shaderPath_.clear();
    lastWriteTime_ = 0;
    frameCount_    = 0;
}

} // namespace termcore

#endif // _WIN32
