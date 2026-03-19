#pragma once
#if defined(_WIN32)

#include <d3d11.h>
#include <cstdint>
#include <string>

namespace termcore {

/// Applies a user-supplied HLSL post-processing pixel shader to the rendered
/// terminal scene.  The terminal is first rendered to an offscreen texture,
/// then this class draws a fullscreen quad with the custom shader applied.
///
/// Users write an HLSL pixel shader that receives the scene texture plus a
/// small set of built-in uniforms (resolution, time, frame count).
class D3DCustomShader {
public:
    D3DCustomShader();
    ~D3DCustomShader();

    /// Must be called once before any other method.
    void initialize(ID3D11Device* device, ID3D11DeviceContext* context);

    /// Compile and load a custom pixel shader from the given file path.
    /// Returns true on success, false on compile/file error.
    bool loadShader(const std::string& path);

    /// Draw a fullscreen quad applying the custom shader.
    /// @param sceneSRV   SRV of the offscreen scene texture
    /// @param outputRTV  Render target to draw into (usually the swap-chain)
    /// @param viewportW  Viewport width  in pixels
    /// @param viewportH  Viewport height in pixels
    /// @param time       Elapsed time in seconds (for animations)
    void render(ID3D11ShaderResourceView* sceneSRV,
                ID3D11RenderTargetView* outputRTV,
                float viewportW, float viewportH,
                float time);

    /// Release all GPU resources.
    void cleanup();

    /// Returns true if a custom shader has been loaded successfully.
    bool isLoaded() const;

    /// Check the shader file for changes and reload if modified.
    /// Call this periodically (e.g. once per second) for hot-reload.
    void checkForReload();

private:
    bool buildFullscreenVS();
    bool compilePixelShader(const std::string& source);
    void createResources();

    ID3D11Device*           device_   = nullptr;
    ID3D11DeviceContext*    context_  = nullptr;

    // Fullscreen-triangle vertex shader (built-in)
    ID3D11VertexShader*     fullscreenVS_ = nullptr;

    // User pixel shader
    ID3D11PixelShader*      customPS_ = nullptr;

    // Constant buffer matching the Params cbuffer
    ID3D11Buffer*           paramsCB_ = nullptr;

    // Linear-clamp sampler
    ID3D11SamplerState*     sampler_  = nullptr;

    // Hot-reload state
    std::string             shaderPath_;
    uint64_t                lastWriteTime_ = 0;
    uint32_t                frameCount_    = 0;
};

} // namespace termcore

#endif // _WIN32
