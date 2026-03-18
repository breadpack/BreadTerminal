#ifndef TERMCORE_BOX_DRAWING_H
#define TERMCORE_BOX_DRAWING_H

#include <cstdint>
#include <vector>

namespace termcore {

/// Result of rendering a box drawing glyph
struct BoxGlyphBitmap {
    std::vector<uint8_t> bitmap;  // Grayscale R8 (width * height bytes)
    int width;
    int height;
};

/// Check if a codepoint should be custom-rendered (not from font).
bool is_box_drawing(char32_t cp);

/// Render a box drawing or special glyph to a grayscale bitmap.
/// cell_width/cell_height: terminal cell dimensions in pixels.
/// thickness: line thickness in pixels (typically 1 or 2).
/// Returns empty bitmap if codepoint is not a known special glyph.
BoxGlyphBitmap render_box_glyph(char32_t cp, int cell_width, int cell_height, int thickness = 1);

// Internal: render box drawing lines (U+2500-U+257F)
BoxGlyphBitmap render_box_lines(char32_t cp, int cell_width, int cell_height, int thickness);

} // namespace termcore
#endif
