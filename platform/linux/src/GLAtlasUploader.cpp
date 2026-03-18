#include "GLAtlasUploader.h"

#include <epoxy/gl.h>
#include <array>

namespace termcore {

struct GLAtlasUploader::Impl {
    static constexpr size_t kMaxFormats = static_cast<size_t>(AtlasFormat::Count);

    std::array<GLuint, kMaxFormats> textures = {};
    std::array<int, kMaxFormats> textureWidths = {};
    std::array<int, kMaxFormats> textureHeights = {};

    Impl() { textures.fill(0); }

    ~Impl() {
        for (auto& tex : textures) {
            if (tex != 0) {
                glDeleteTextures(1, &tex);
                tex = 0;
            }
        }
    }

    static GLenum glInternalFormat(AtlasFormat format) {
        switch (format) {
            case AtlasFormat::R8:   return GL_R8;
            case AtlasFormat::BGRA: return GL_RGBA8;
            case AtlasFormat::RGB:  return GL_RGBA8;
            default:                return GL_R8;
        }
    }

    static GLenum glPixelFormat(AtlasFormat format) {
        switch (format) {
            case AtlasFormat::R8:   return GL_RED;
            case AtlasFormat::BGRA: return GL_BGRA;
            case AtlasFormat::RGB:  return GL_RGBA;
            default:                return GL_RED;
        }
    }

    static int bytesPerPixel(AtlasFormat format) {
        switch (format) {
            case AtlasFormat::R8:   return 1;
            case AtlasFormat::BGRA: return 4;
            case AtlasFormat::RGB:  return 4;
            default:                return 1;
        }
    }

    void uploadPage(AtlasPage& page) {
        auto fmt = page.format();
        auto idx = static_cast<size_t>(fmt);

        int pageW = page.width();
        int pageH = page.height();

        // Recreate texture if dimensions changed
        if (textures[idx] == 0 ||
            textureWidths[idx] != pageW ||
            textureHeights[idx] != pageH) {

            if (textures[idx] != 0) {
                glDeleteTextures(1, &textures[idx]);
            }

            glGenTextures(1, &textures[idx]);
            glBindTexture(GL_TEXTURE_2D, textures[idx]);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            // For single-channel textures, set swizzle so .r works
            if (fmt == AtlasFormat::R8) {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);
            }

            glTexImage2D(GL_TEXTURE_2D, 0, glInternalFormat(fmt),
                         pageW, pageH, 0, glPixelFormat(fmt),
                         GL_UNSIGNED_BYTE, nullptr);

            textureWidths[idx] = pageW;
            textureHeights[idx] = pageH;
        } else {
            glBindTexture(GL_TEXTURE_2D, textures[idx]);
        }

        // Set pixel alignment for single-byte formats
        if (fmt == AtlasFormat::R8) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        } else {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        }

        // Upload pixel data
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pageW, pageH,
                        glPixelFormat(fmt), GL_UNSIGNED_BYTE, page.data());

        page.clearDirty();
    }
};

GLAtlasUploader::GLAtlasUploader()
    : impl_(std::make_unique<Impl>()) {}

GLAtlasUploader::~GLAtlasUploader() = default;

void GLAtlasUploader::upload(GlyphAtlas& atlas) {
    for (size_t i = 0; i < static_cast<size_t>(AtlasFormat::Count); ++i) {
        auto fmt = static_cast<AtlasFormat>(i);
        AtlasPage* page = atlas.getPage(fmt);
        if (page && page->isDirty()) {
            impl_->uploadPage(*page);
        }
    }
}

uint32_t GLAtlasUploader::textureForFormat(AtlasFormat format) const {
    auto idx = static_cast<size_t>(format);
    if (idx < Impl::kMaxFormats) {
        return impl_->textures[idx];
    }
    return 0;
}

} // namespace termcore
