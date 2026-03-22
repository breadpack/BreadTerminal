#pragma once
#include <cstdint>
#include <string>

namespace termcore {

/// Color font table types found in OpenType fonts
enum class ColorFontType {
    None    = 0,
    COLR_v0 = 1,   // Color Layers (simple)
    COLR_v1 = 2,   // Color Layers (gradients, compositing)
    SBIX    = 4,    // Apple bitmap color (PNG/JPEG)
    CBDT    = 8,    // Google bitmap color (used in Noto Color Emoji)
    SVG     = 16,   // SVG color glyphs
};

/// Bitwise OR support
inline ColorFontType operator|(ColorFontType a, ColorFontType b) {
    return static_cast<ColorFontType>(static_cast<int>(a) | static_cast<int>(b));
}
inline ColorFontType operator&(ColorFontType a, ColorFontType b) {
    return static_cast<ColorFontType>(static_cast<int>(a) & static_cast<int>(b));
}
inline ColorFontType& operator|=(ColorFontType& a, ColorFontType b) {
    a = a | b;
    return a;
}
inline bool hasFlag(ColorFontType value, ColorFontType flag) {
    return (static_cast<int>(value) & static_cast<int>(flag)) != 0;
}

/// Detect which color font tables are present in a font file.
/// Uses HarfBuzz to check for COLR, CBDT, sbix, SVG tables.
ColorFontType detectColorTables(const std::string& fontPath);

/// Detect color tables from a HarfBuzz face (hb_face_t*).
/// Pass as void* to avoid exposing HarfBuzz in header.
ColorFontType detectColorTablesFromFace(void* hb_face);

/// Check if a specific glyph has color data.
/// glyphId: the glyph index, hb_face: hb_face_t*.
bool isColorGlyph(void* hb_face, uint32_t glyphId);

/// Get a human-readable string for color font type.
std::string colorFontTypeName(ColorFontType type);

} // namespace termcore
