#ifndef TERMCORE_FONT_METRICS_H
#define TERMCORE_FONT_METRICS_H

#include <cstdint>
#include <string>
#include <functional>

namespace termcore {

/// Unique identifier for a loaded font face
using FontFaceId = uint32_t;
static constexpr FontFaceId kInvalidFontFace = 0;

/// Font style flags
enum class FontStyle : uint8_t {
    Regular = 0,
    Bold = 1,
    Italic = 2,
    BoldItalic = 3,
};

/// Subpixel offset for glyph positioning (quantized to N steps)
struct SubpixelOffset {
    uint8_t x = 0;  // 0..3 (quarter pixel)
    uint8_t y = 0;

    bool operator==(const SubpixelOffset&) const = default;
};

/// Key for looking up a specific glyph variant
struct GlyphKey {
    FontFaceId face_id;
    uint32_t glyph_index;
    SubpixelOffset subpixel;

    bool operator==(const GlyphKey&) const = default;
};

/// Font metrics for a specific face at a specific size
struct FontMetrics {
    float cell_width;       // Width of a single cell (advance of ASCII char)
    float cell_height;      // Total line height
    float ascent;           // Baseline to top
    float descent;          // Baseline to bottom (positive value)
    float underline_position;
    float underline_thickness;
    float strikethrough_position;
    float strikethrough_thickness;
};

/// Descriptor for font discovery results
struct FontDescriptor {
    std::string family;
    std::string postscript_name;
    std::string file_path;
    int face_index = 0;
    FontStyle style = FontStyle::Regular;
    int weight = 400;  // CSS weight: 100-900
};

/// Query for font discovery
struct FontQuery {
    std::string family;
    FontStyle style = FontStyle::Regular;
    int weight = 400;
};

} // namespace termcore

// Hash for GlyphKey (for use in unordered_map)
template<>
struct std::hash<termcore::GlyphKey> {
    size_t operator()(const termcore::GlyphKey& k) const noexcept {
        size_t h = std::hash<uint32_t>{}(k.face_id);
        h ^= std::hash<uint32_t>{}(k.glyph_index) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint8_t>{}(k.subpixel.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<uint8_t>{}(k.subpixel.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

#endif
