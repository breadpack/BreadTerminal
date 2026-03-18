#include "FreeTypeRasterizer.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_LCD_FILTER_H
#include FT_GLYPH_H

#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace termcore {

namespace {

struct FontFaceEntry {
    FT_Face face = nullptr;
    float size = 0;
};

} // namespace

class FreeTypeRasterizerImpl : public IFontRasterizer {
public:
    FreeTypeRasterizerImpl() {
        FT_Init_FreeType(&library_);
        FT_Library_SetLcdFilter(library_, FT_LCD_FILTER_DEFAULT);
    }

    ~FreeTypeRasterizerImpl() override {
        for (auto& [id, entry] : fonts_) {
            if (entry.face) FT_Done_Face(entry.face);
        }
        if (library_) FT_Done_FreeType(library_);
    }

    FontFaceId loadFont(const std::string& path, int face_index,
                        float size) override {
        std::lock_guard<std::mutex> lock(mutex_);

        FT_Face face = nullptr;
        FT_Error err = FT_New_Face(library_, path.c_str(), face_index, &face);
        if (err || !face) return kInvalidFontFace;

        // Set char size: FreeType uses 26.6 fixed-point (multiply by 64)
        FT_Set_Char_Size(face, 0, static_cast<FT_F26Dot6>(size * 64.0f), 96,
                         96);

        FontFaceId id = next_id_++;
        fonts_[id] = FontFaceEntry{face, size};
        return id;
    }

    RasterizedGlyph rasterize(FontFaceId face, uint32_t glyph_index,
                               float size, SubpixelOffset offset) override {
        RasterizedGlyph result;
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = fonts_.find(face);
        if (it == fonts_.end()) return result;

        FT_Face ft_face = it->second.face;

        // Re-set size if it differs from the loaded size
        if (std::abs(it->second.size - size) > 0.01f) {
            FT_Set_Char_Size(ft_face, 0,
                             static_cast<FT_F26Dot6>(size * 64.0f), 96, 96);
        }

        bool is_color = FT_HAS_COLOR(ft_face);

        FT_Int32 load_flags = FT_LOAD_DEFAULT;
        if (is_color) {
            load_flags |= FT_LOAD_COLOR;
        } else {
            load_flags |= FT_LOAD_TARGET_LCD;
        }

        FT_Error err = FT_Load_Glyph(ft_face, glyph_index, load_flags);
        if (err) return result;

        FT_Render_Mode render_mode = FT_RENDER_MODE_NORMAL;
        if (!is_color) {
            render_mode = FT_RENDER_MODE_LCD;
        }

        err = FT_Render_Glyph(ft_face->glyph, render_mode);
        if (err) return result;

        FT_Bitmap& bitmap = ft_face->glyph->bitmap;

        if (bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
            // Color emoji
            result.format = PixelFormat::BGRA;
            result.width = static_cast<int32_t>(bitmap.width);
            result.height = static_cast<int32_t>(bitmap.rows);
            result.bearing_x = ft_face->glyph->bitmap_left;
            result.bearing_y = ft_face->glyph->bitmap_top;

            size_t byte_count =
                static_cast<size_t>(result.width) * result.height * 4;
            result.bitmap.resize(byte_count);

            for (int32_t row = 0; row < result.height; ++row) {
                const uint8_t* src =
                    bitmap.buffer + row * bitmap.pitch;
                uint8_t* dst =
                    result.bitmap.data() + row * result.width * 4;
                std::memcpy(dst, src,
                            static_cast<size_t>(result.width) * 4);
            }
        } else if (bitmap.pixel_mode == FT_PIXEL_MODE_LCD) {
            // LCD subpixel: width is 3x actual pixel width
            result.format = PixelFormat::RGB;
            result.width = static_cast<int32_t>(bitmap.width / 3);
            result.height = static_cast<int32_t>(bitmap.rows);
            result.bearing_x = ft_face->glyph->bitmap_left;
            result.bearing_y = ft_face->glyph->bitmap_top;

            size_t byte_count =
                static_cast<size_t>(result.width) * result.height * 3;
            result.bitmap.resize(byte_count);

            for (int32_t row = 0; row < result.height; ++row) {
                const uint8_t* src =
                    bitmap.buffer + row * bitmap.pitch;
                uint8_t* dst =
                    result.bitmap.data() + row * result.width * 3;
                std::memcpy(dst, src,
                            static_cast<size_t>(result.width) * 3);
            }
        } else {
            // Grayscale
            result.format = PixelFormat::Grayscale;
            result.width = static_cast<int32_t>(bitmap.width);
            result.height = static_cast<int32_t>(bitmap.rows);
            result.bearing_x = ft_face->glyph->bitmap_left;
            result.bearing_y = ft_face->glyph->bitmap_top;

            size_t byte_count =
                static_cast<size_t>(result.width) * result.height;
            result.bitmap.resize(byte_count);

            for (int32_t row = 0; row < result.height; ++row) {
                const uint8_t* src =
                    bitmap.buffer + row * bitmap.pitch;
                uint8_t* dst =
                    result.bitmap.data() + row * result.width;
                std::memcpy(dst, src, static_cast<size_t>(result.width));
            }
        }

        return result;
    }

    FontMetrics getMetrics(FontFaceId face, float size) override {
        FontMetrics metrics{};
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = fonts_.find(face);
        if (it == fonts_.end()) return metrics;

        FT_Face ft_face = it->second.face;

        if (std::abs(it->second.size - size) > 0.01f) {
            FT_Set_Char_Size(ft_face, 0,
                             static_cast<FT_F26Dot6>(size * 64.0f), 96, 96);
        }

        // FreeType metrics are in 26.6 fixed-point
        float scale = 1.0f / 64.0f;
        metrics.ascent =
            static_cast<float>(ft_face->size->metrics.ascender) * scale;
        metrics.descent =
            std::abs(static_cast<float>(ft_face->size->metrics.descender) *
                     scale);
        metrics.cell_height =
            static_cast<float>(ft_face->size->metrics.height) * scale;

        // Cell width: advance of space character
        FT_UInt space_idx = FT_Get_Char_Index(ft_face, ' ');
        if (space_idx != 0 &&
            FT_Load_Glyph(ft_face, space_idx, FT_LOAD_DEFAULT) == 0) {
            metrics.cell_width =
                static_cast<float>(ft_face->glyph->advance.x) * scale;
        } else {
            // Fallback: use max_advance_width
            metrics.cell_width =
                static_cast<float>(ft_face->size->metrics.max_advance) * scale;
        }

        // Underline / strikethrough
        if (ft_face->underline_position != 0) {
            metrics.underline_position =
                -static_cast<float>(ft_face->underline_position) * scale;
            metrics.underline_thickness =
                static_cast<float>(ft_face->underline_thickness) * scale;
        } else {
            metrics.underline_position = metrics.descent * 0.5f;
            metrics.underline_thickness = 1.0f;
        }
        metrics.strikethrough_position = metrics.ascent * 0.3f;
        metrics.strikethrough_thickness = metrics.underline_thickness;

        return metrics;
    }

    bool isColorGlyph(FontFaceId face, uint32_t /*glyph_index*/) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = fonts_.find(face);
        if (it == fonts_.end()) return false;
        return FT_HAS_COLOR(it->second.face);
    }

    uint32_t getGlyphIndex(FontFaceId face, char32_t codepoint) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = fonts_.find(face);
        if (it == fonts_.end()) return 0;
        return FT_Get_Char_Index(it->second.face,
                                 static_cast<FT_ULong>(codepoint));
    }

private:
    FT_Library library_ = nullptr;
    std::mutex mutex_;
    FontFaceId next_id_ = 1;
    std::unordered_map<FontFaceId, FontFaceEntry> fonts_;
};

std::unique_ptr<IFontRasterizer> createFreeTypeRasterizer() {
    return std::make_unique<FreeTypeRasterizerImpl>();
}

} // namespace termcore
