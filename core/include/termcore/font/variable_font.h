#ifndef TERMCORE_VARIABLE_FONT_H
#define TERMCORE_VARIABLE_FONT_H

#include <cstdint>
#include <string>
#include <vector>

namespace termcore {

/// A font variation axis
struct FontAxis {
    uint32_t tag;           // 4-byte tag (e.g., 'wght', 'wdth', 'slnt', 'opsz')
    std::string name;       // Human-readable name
    float min_value;
    float max_value;
    float default_value;
};

/// A single axis setting
struct FontVariation {
    uint32_t tag;
    float value;
};

/// Parse a 4-character tag string to uint32_t (e.g., "wght" -> 0x77676874)
uint32_t parseAxisTag(const std::string& tag_str);

/// Convert uint32_t tag to string
std::string axisTagToString(uint32_t tag);

/// Common axis tags
namespace AxisTag {
    constexpr uint32_t Weight = 0x77676874;  // 'wght'
    constexpr uint32_t Width  = 0x77647468;  // 'wdth'
    constexpr uint32_t Slant  = 0x736C6E74;  // 'slnt'
    constexpr uint32_t OpticalSize = 0x6F70737A; // 'opsz'
    constexpr uint32_t Italic = 0x6974616C;  // 'ital'
}

/// Query variable font axes from a font file using HarfBuzz.
/// Returns empty vector if font is not variable.
std::vector<FontAxis> queryAxes(const std::string& font_path, int face_index = 0);

/// Check if a font file is a variable font.
bool isVariableFont(const std::string& font_path, int face_index = 0);

/// Parse variation settings from a config string.
/// Format: "wght=700,wdth=75,slnt=-12"
std::vector<FontVariation> parseVariations(const std::string& config_str);

/// Apply variations to a HarfBuzz font (updates the font's variation coordinates).
/// Returns true on success.
bool applyVariations(void* hb_font, const std::vector<FontVariation>& variations);

} // namespace termcore
#endif
