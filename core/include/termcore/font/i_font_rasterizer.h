#ifndef TERMCORE_I_FONT_RASTERIZER_H
#define TERMCORE_I_FONT_RASTERIZER_H

#include "font_metrics.h"
#include <cstdint>
#include <vector>

namespace termcore {

/// Pixel format of a rasterized glyph bitmap
enum class PixelFormat : uint8_t {
    Grayscale,  // R8 — single channel alpha
    RGB,        // 3-channel (ClearType subpixel)
    BGRA,       // Color emoji
};

/// Result of rasterizing a single glyph
struct RasterizedGlyph {
    std::vector<uint8_t> bitmap;
    int32_t width = 0;
    int32_t height = 0;
    int32_t bearing_x = 0;  // Offset from origin to left edge
    int32_t bearing_y = 0;  // Offset from baseline to top edge
    PixelFormat format = PixelFormat::Grayscale;
};

/// Abstract interface for platform-specific glyph rasterization.
/// Implementations: CoreTextRasterizer (macOS), DirectWriteRasterizer (Windows), FreeTypeRasterizer (Linux)
class IFontRasterizer {
public:
    virtual ~IFontRasterizer() = default;

    /// Load a font face from a file path. Returns face ID or kInvalidFontFace on failure.
    virtual FontFaceId loadFont(const std::string& path, int face_index, float size) = 0;

    /// Rasterize a single glyph.
    virtual RasterizedGlyph rasterize(FontFaceId face, uint32_t glyph_index,
                                       float size, SubpixelOffset offset) = 0;

    /// Get font metrics for a loaded face.
    virtual FontMetrics getMetrics(FontFaceId face, float size) = 0;

    /// Check if a glyph is a color glyph (emoji).
    virtual bool isColorGlyph(FontFaceId face, uint32_t glyph_index) = 0;

    /// Get glyph index for a codepoint (for fallback checking).
    virtual uint32_t getGlyphIndex(FontFaceId face, char32_t codepoint) = 0;

    /// Set display scale factor (1.0 for standard, 2.0 for Retina).
    virtual void setScaleFactor(float scale) { (void)scale; }
};

} // namespace termcore

#endif
