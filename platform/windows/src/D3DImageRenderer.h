#pragma once
#if defined(_WIN32)

#include <d3d11.h>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace termcore {

class KittyGraphicsManager;
struct KittyPlacement;

/// Manages D3D11 textures for inline images (Kitty graphics protocol)
class D3DImageRenderer {
public:
    D3DImageRenderer();
    ~D3DImageRenderer();

    /// Initialize with D3D device and context
    void initialize(ID3D11Device* device, ID3D11DeviceContext* context);

    /// Upload/update image textures from the graphics manager.
    /// Call this each frame before rendering.
    void syncImages(const KittyGraphicsManager& gfx);

    /// Render all placements at their cell positions.
    /// cell_width/cell_height: terminal cell size in pixels
    /// viewport_width/viewport_height: viewport size
    void renderPlacements(const KittyGraphicsManager& gfx,
                          float cell_width, float cell_height,
                          float viewport_width, float viewport_height,
                          ID3D11RenderTargetView* rtv);

    /// Clean up all textures
    void cleanup();

private:
    struct ImageTexture {
        ID3D11Texture2D* texture = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        int width = 0;
        int height = 0;
    };

    void createImageTexture(uint32_t image_id, const uint8_t* data,
                            int width, int height);
    void destroyImageTexture(uint32_t image_id);

    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;

    // Shaders for image rendering
    ID3D11VertexShader* imageVS_ = nullptr;
    ID3D11PixelShader* imagePS_ = nullptr;
    ID3D11Buffer* imageCB_ = nullptr;
    ID3D11SamplerState* imageSampler_ = nullptr;
    ID3D11BlendState* imageBlend_ = nullptr;

    std::unordered_map<uint32_t, ImageTexture> textures_;

    bool buildImageShaders();
    void createImageResources();
};

} // namespace termcore

#endif // _WIN32
