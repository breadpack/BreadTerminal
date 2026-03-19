#pragma once
#if defined(_WIN32)

#include <d3d11.h>
#include <string>

namespace termcore {

/// Loads and renders a background image behind terminal content using D3D11.
/// Supports PNG/JPG via WIC, with multiple scaling modes and opacity control.
class D3DBackgroundImage {
public:
    enum class Mode { Fill, Fit, Center, Tile, Stretch };

    D3DBackgroundImage() = default;
    ~D3DBackgroundImage();

    /// Initialize with D3D device and context. Must be called before loadImage.
    void initialize(ID3D11Device* device, ID3D11DeviceContext* context);

    /// Load an image from a file path (PNG, JPG, BMP, etc. via WIC).
    /// Returns true on success.
    bool loadImage(const std::string& path);

    /// Render the background image as a fullscreen quad.
    /// @param viewportW  Viewport width in pixels
    /// @param viewportH  Viewport height in pixels
    /// @param opacity    Blend opacity (0.0 = transparent, 1.0 = opaque)
    /// @param mode       Scaling mode
    /// @param rtv        Render target to draw into
    void render(float viewportW, float viewportH, float opacity, Mode mode,
                ID3D11RenderTargetView* rtv);

    /// Release all D3D resources.
    void cleanup();

    /// Returns true if an image is currently loaded.
    bool hasImage() const { return srv_ != nullptr; }

private:
    bool buildShaders();
    void createResources();

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;

    // Image texture
    ID3D11Texture2D* texture_ = nullptr;
    ID3D11ShaderResourceView* srv_ = nullptr;
    int imageWidth_ = 0;
    int imageHeight_ = 0;

    // Pipeline state
    ID3D11VertexShader* vs_ = nullptr;
    ID3D11PixelShader* ps_ = nullptr;
    ID3D11Buffer* cb_ = nullptr;
    ID3D11SamplerState* sampler_ = nullptr;
    ID3D11BlendState* blend_ = nullptr;
};

} // namespace termcore

#endif // _WIN32
