#if defined(_WIN32)

#include "DirectWriteRasterizer.h"

#include <dwrite.h>
#include <d2d1.h>
#include <wrl/client.h>

#include <cmath>
#include <mutex>
#include <unordered_map>

using Microsoft::WRL::ComPtr;

namespace termcore {

namespace {

struct FontFaceEntry {
    ComPtr<IDWriteFontFace> fontFace;
    float emSize = 0;
    DWRITE_FONT_METRICS dwMetrics = {};
};

} // namespace

class DirectWriteRasterizerImpl : public IFontRasterizer {
public:
    DirectWriteRasterizerImpl() {
        HRESULT hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(factory_.GetAddressOf()));
        if (FAILED(hr)) {
            factory_ = nullptr;
        }
    }

    ~DirectWriteRasterizerImpl() override = default;

    FontFaceId loadFont(const std::string& path, int face_index,
                        float size) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!factory_) return kInvalidFontFace;

        // Convert path to wide string
        int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(),
                                       static_cast<int>(path.size()),
                                       nullptr, 0);
        std::wstring wpath(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(),
                            static_cast<int>(path.size()),
                            wpath.data(), wlen);

        // Create font file reference
        ComPtr<IDWriteFontFile> fontFile;
        HRESULT hr = factory_->CreateFontFileReference(
            wpath.c_str(), nullptr, fontFile.GetAddressOf());
        if (FAILED(hr)) return kInvalidFontFace;

        // Analyze the font file
        BOOL isSupported = FALSE;
        DWRITE_FONT_FILE_TYPE fileType;
        DWRITE_FONT_FACE_TYPE faceType;
        UINT32 numFaces = 0;
        hr = fontFile->Analyze(&isSupported, &fileType, &faceType, &numFaces);
        if (FAILED(hr) || !isSupported) return kInvalidFontFace;

        if (static_cast<UINT32>(face_index) >= numFaces) {
            return kInvalidFontFace;
        }

        // Create font face
        IDWriteFontFile* fileArray[] = { fontFile.Get() };
        ComPtr<IDWriteFontFace> fontFace;
        hr = factory_->CreateFontFace(
            faceType,
            1,
            fileArray,
            static_cast<UINT32>(face_index),
            DWRITE_FONT_SIMULATIONS_NONE,
            fontFace.GetAddressOf());
        if (FAILED(hr)) return kInvalidFontFace;

        // Get design metrics
        DWRITE_FONT_METRICS dwMetrics;
        fontFace->GetMetrics(&dwMetrics);

        // Convert point size to em size (DIP = 1/96 inch, 1pt = 1/72 inch)
        float emSize = size * (96.0f / 72.0f);

        FontFaceId id = next_id_++;
        fonts_[id] = FontFaceEntry{fontFace, emSize, dwMetrics};
        return id;
    }

    RasterizedGlyph rasterize(FontFaceId face, uint32_t glyph_index,
                               float size, SubpixelOffset /*offset*/) override {
        RasterizedGlyph result;
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = fonts_.find(face);
        if (it == fonts_.end()) return result;

        FontFaceEntry& entry = it->second;
        float emSize = size * (96.0f / 72.0f);

        // Set up glyph run
        DWRITE_GLYPH_RUN glyphRun = {};
        glyphRun.fontFace = entry.fontFace.Get();
        glyphRun.fontEmSize = emSize;
        glyphRun.glyphCount = 1;

        UINT16 glyphIdx = static_cast<UINT16>(glyph_index);
        FLOAT glyphAdvance = 0;
        DWRITE_GLYPH_OFFSET glyphOffset = {0, 0};

        glyphRun.glyphIndices = &glyphIdx;
        glyphRun.glyphAdvances = &glyphAdvance;
        glyphRun.glyphOffsets = &glyphOffset;

        // Create glyph run analysis for ClearType rendering
        ComPtr<IDWriteGlyphRunAnalysis> analysis;
        HRESULT hr = factory_->CreateGlyphRunAnalysis(
            &glyphRun,
            1.0f,          // pixels per DIP
            nullptr,       // transform
            DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC,
            DWRITE_MEASURING_MODE_NATURAL,
            0.0f, 0.0f,   // baseline origin
            analysis.GetAddressOf());
        if (FAILED(hr)) return result;

        // Get texture bounds
        RECT bounds = {};
        hr = analysis->GetAlphaTextureBounds(
            DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds);
        if (FAILED(hr) || (bounds.right <= bounds.left) ||
            (bounds.bottom <= bounds.top)) {
            // Try aliased (for symbols that don't have ClearType)
            hr = analysis->GetAlphaTextureBounds(
                DWRITE_TEXTURE_ALIASED_1x1, &bounds);
            if (FAILED(hr) || (bounds.right <= bounds.left) ||
                (bounds.bottom <= bounds.top)) {
                return result;
            }

            // Aliased: single-channel grayscale
            int32_t width = bounds.right - bounds.left;
            int32_t height = bounds.bottom - bounds.top;
            size_t byteCount = static_cast<size_t>(width) * height;
            std::vector<uint8_t> alphaValues(byteCount);

            hr = analysis->CreateAlphaTexture(
                DWRITE_TEXTURE_ALIASED_1x1,
                &bounds,
                alphaValues.data(),
                static_cast<UINT32>(byteCount));
            if (FAILED(hr)) return result;

            result.format = PixelFormat::Grayscale;
            result.width = width;
            result.height = height;
            result.bearing_x = bounds.left;
            result.bearing_y = -bounds.top;
            result.bitmap = std::move(alphaValues);
            return result;
        }

        // ClearType: 3-channel RGB subpixel
        int32_t width = bounds.right - bounds.left;
        int32_t height = bounds.bottom - bounds.top;
        size_t byteCount = static_cast<size_t>(width) * height * 3;
        std::vector<uint8_t> alphaValues(byteCount);

        hr = analysis->CreateAlphaTexture(
            DWRITE_TEXTURE_CLEARTYPE_3x1,
            &bounds,
            alphaValues.data(),
            static_cast<UINT32>(byteCount));
        if (FAILED(hr)) return result;

        result.format = PixelFormat::RGB;
        result.width = width;
        result.height = height;
        result.bearing_x = bounds.left;
        result.bearing_y = -bounds.top;
        result.bitmap = std::move(alphaValues);

        return result;
    }

    FontMetrics getMetrics(FontFaceId face, float size) override {
        FontMetrics metrics{};
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = fonts_.find(face);
        if (it == fonts_.end()) return metrics;

        const FontFaceEntry& entry = it->second;
        const DWRITE_FONT_METRICS& dm = entry.dwMetrics;
        float emSize = size * (96.0f / 72.0f);

        // Convert from design units to pixels
        float scale = emSize / static_cast<float>(dm.designUnitsPerEm);

        metrics.ascent = static_cast<float>(dm.ascent) * scale;
        metrics.descent = static_cast<float>(dm.descent) * scale;
        metrics.cell_height =
            (static_cast<float>(dm.ascent) +
             static_cast<float>(dm.descent) +
             static_cast<float>(dm.lineGap)) * scale;

        // Cell width: use advance of space glyph
        UINT16 spaceIdx = 0;
        UINT32 codepoint = ' ';
        entry.fontFace->GetGlyphIndices(&codepoint, 1, &spaceIdx);

        if (spaceIdx != 0) {
            DWRITE_GLYPH_METRICS glyphMetrics;
            HRESULT hr = entry.fontFace->GetDesignGlyphMetrics(
                &spaceIdx, 1, &glyphMetrics, FALSE);
            if (SUCCEEDED(hr)) {
                metrics.cell_width =
                    static_cast<float>(glyphMetrics.advanceWidth) * scale;
            }
        }

        if (metrics.cell_width <= 0) {
            // Fallback: estimate from em size
            metrics.cell_width = emSize * 0.6f;
        }

        // Underline / strikethrough
        metrics.underline_position =
            static_cast<float>(dm.underlinePosition) * scale;
        metrics.underline_thickness =
            static_cast<float>(dm.underlineThickness) * scale;
        metrics.strikethrough_position =
            static_cast<float>(dm.strikethroughPosition) * scale;
        metrics.strikethrough_thickness =
            static_cast<float>(dm.strikethroughThickness) * scale;

        return metrics;
    }

    bool isColorGlyph(FontFaceId face, uint32_t /*glyph_index*/) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = fonts_.find(face);
        if (it == fonts_.end()) return false;

        // Check if font has COLR or CBDT/CBLC tables (color emoji)
        const void* tableData = nullptr;
        UINT32 tableSize = 0;
        void* tableContext = nullptr;
        BOOL exists = FALSE;

        it->second.fontFace->TryGetFontTable(
            DWRITE_MAKE_OPENTYPE_TAG('C', 'O', 'L', 'R'),
            &tableData, &tableSize, &tableContext, &exists);
        if (tableContext) {
            it->second.fontFace->ReleaseFontTable(tableContext);
        }

        return exists != FALSE;
    }

    uint32_t getGlyphIndex(FontFaceId face, char32_t codepoint) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = fonts_.find(face);
        if (it == fonts_.end()) return 0;

        UINT16 glyphIndex = 0;
        UINT32 cp = static_cast<UINT32>(codepoint);
        it->second.fontFace->GetGlyphIndices(&cp, 1, &glyphIndex);
        return static_cast<uint32_t>(glyphIndex);
    }

private:
    ComPtr<IDWriteFactory> factory_;
    std::mutex mutex_;
    FontFaceId next_id_ = 1;
    std::unordered_map<FontFaceId, FontFaceEntry> fonts_;
};

std::unique_ptr<IFontRasterizer> createDirectWriteRasterizer() {
    return std::make_unique<DirectWriteRasterizerImpl>();
}

} // namespace termcore

#endif // _WIN32
