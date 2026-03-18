#if defined(_WIN32)

#include "D3DAtlasUploader.h"

#include <d3d11.h>
#include <array>

namespace termcore {

struct D3DAtlasUploader::Impl {
    static constexpr size_t kMaxFormats =
        static_cast<size_t>(AtlasFormat::Count);

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;

    std::array<ID3D11Texture2D*, kMaxFormats> textures = {};
    std::array<ID3D11ShaderResourceView*, kMaxFormats> srvs = {};
    std::array<int, kMaxFormats> textureWidths = {};
    std::array<int, kMaxFormats> textureHeights = {};

    Impl() {
        textures.fill(nullptr);
        srvs.fill(nullptr);
        textureWidths.fill(0);
        textureHeights.fill(0);
    }

    ~Impl() {
        for (auto& srv : srvs) {
            if (srv) { srv->Release(); srv = nullptr; }
        }
        for (auto& tex : textures) {
            if (tex) { tex->Release(); tex = nullptr; }
        }
    }

    static DXGI_FORMAT dxgiFormat(AtlasFormat format) {
        switch (format) {
            case AtlasFormat::R8:   return DXGI_FORMAT_R8_UNORM;
            case AtlasFormat::BGRA: return DXGI_FORMAT_B8G8R8A8_UNORM;
            case AtlasFormat::RGB:  return DXGI_FORMAT_R8G8B8A8_UNORM;
            default:                return DXGI_FORMAT_R8_UNORM;
        }
    }

    static UINT bytesPerPixel(AtlasFormat format) {
        switch (format) {
            case AtlasFormat::R8:   return 1;
            case AtlasFormat::BGRA: return 4;
            case AtlasFormat::RGB:  return 4;
            default:                return 1;
        }
    }

    void uploadPage(AtlasPage& page) {
        if (!device || !context) return;

        auto fmt = page.format();
        auto idx = static_cast<size_t>(fmt);

        int pageW = page.width();
        int pageH = page.height();

        // Recreate texture if dimensions changed
        if (!textures[idx] ||
            textureWidths[idx] != pageW ||
            textureHeights[idx] != pageH) {

            if (srvs[idx]) { srvs[idx]->Release(); srvs[idx] = nullptr; }
            if (textures[idx]) {
                textures[idx]->Release();
                textures[idx] = nullptr;
            }

            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = static_cast<UINT>(pageW);
            desc.Height = static_cast<UINT>(pageH);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = dxgiFormat(fmt);
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            HRESULT hr = device->CreateTexture2D(
                &desc, nullptr, &textures[idx]);
            if (FAILED(hr)) return;

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = desc.Format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;

            hr = device->CreateShaderResourceView(
                textures[idx], &srvDesc, &srvs[idx]);
            if (FAILED(hr)) {
                textures[idx]->Release();
                textures[idx] = nullptr;
                return;
            }

            textureWidths[idx] = pageW;
            textureHeights[idx] = pageH;
        }

        // Upload pixel data
        UINT rowPitch = static_cast<UINT>(pageW) * bytesPerPixel(fmt);
        D3D11_BOX destBox = {};
        destBox.left = 0;
        destBox.top = 0;
        destBox.front = 0;
        destBox.right = static_cast<UINT>(pageW);
        destBox.bottom = static_cast<UINT>(pageH);
        destBox.back = 1;

        context->UpdateSubresource(
            textures[idx], 0, &destBox,
            page.data(), rowPitch, 0);

        page.clearDirty();
    }
};

D3DAtlasUploader::D3DAtlasUploader()
    : impl_(std::make_unique<Impl>()) {}

D3DAtlasUploader::~D3DAtlasUploader() = default;

void D3DAtlasUploader::setDevice(ID3D11Device* device,
                                  ID3D11DeviceContext* context) {
    impl_->device = device;
    impl_->context = context;
}

void D3DAtlasUploader::upload(GlyphAtlas& atlas) {
    for (size_t i = 0; i < static_cast<size_t>(AtlasFormat::Count); ++i) {
        auto fmt = static_cast<AtlasFormat>(i);
        AtlasPage* page = atlas.getPage(fmt);
        if (page && page->isDirty()) {
            impl_->uploadPage(*page);
        }
    }
}

ID3D11ShaderResourceView* D3DAtlasUploader::srvForFormat(
    AtlasFormat format) const {
    auto idx = static_cast<size_t>(format);
    if (idx < Impl::kMaxFormats) {
        return impl_->srvs[idx];
    }
    return nullptr;
}

} // namespace termcore

#endif // _WIN32
