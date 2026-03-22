#include "termcore/font/color_font.h"

#include <hb.h>
#include <hb-ot.h>

#include <vector>

namespace termcore {

ColorFontType detectColorTables(const std::string& fontPath) {
    hb_blob_t* blob = hb_blob_create_from_file(fontPath.c_str());
    if (!blob || hb_blob_get_length(blob) == 0) {
        if (blob) hb_blob_destroy(blob);
        return ColorFontType::None;
    }

    hb_face_t* face = hb_face_create(blob, 0);
    if (!face) {
        hb_blob_destroy(blob);
        return ColorFontType::None;
    }

    ColorFontType result = detectColorTablesFromFace(static_cast<void*>(face));

    hb_face_destroy(face);
    hb_blob_destroy(blob);
    return result;
}

ColorFontType detectColorTablesFromFace(void* hb_face_ptr) {
    if (!hb_face_ptr) return ColorFontType::None;

    auto* face = static_cast<hb_face_t*>(hb_face_ptr);
    ColorFontType result = ColorFontType::None;

    // Check COLR table
    hb_blob_t* colr = hb_face_reference_table(face, HB_TAG('C', 'O', 'L', 'R'));
    if (colr && hb_blob_get_length(colr) > 0) {
        // Distinguish v0 vs v1 by checking if hb_ot_color_has_paint is available.
        // For simplicity, check if there are any color layers (v0) first,
        // then check for v1 paint support.
        if (hb_ot_color_has_layers(face)) {
            result |= ColorFontType::COLR_v0;
        }
#if HB_VERSION_ATLEAST(7, 0, 0)
        if (hb_ot_color_has_paint(face)) {
            result |= ColorFontType::COLR_v1;
        }
#endif
        // If the table exists but no layers detected, still mark as COLR_v0
        if (result == ColorFontType::None) {
            result |= ColorFontType::COLR_v0;
        }
    }
    if (colr) hb_blob_destroy(colr);

    // Check sbix table
    hb_blob_t* sbix = hb_face_reference_table(face, HB_TAG('s', 'b', 'i', 'x'));
    if (sbix && hb_blob_get_length(sbix) > 0) {
        result |= ColorFontType::SBIX;
    }
    if (sbix) hb_blob_destroy(sbix);

    // Check CBDT table
    hb_blob_t* cbdt = hb_face_reference_table(face, HB_TAG('C', 'B', 'D', 'T'));
    if (cbdt && hb_blob_get_length(cbdt) > 0) {
        result |= ColorFontType::CBDT;
    }
    if (cbdt) hb_blob_destroy(cbdt);

    // Check SVG table
    hb_blob_t* svg = hb_face_reference_table(face, HB_TAG('S', 'V', 'G', ' '));
    if (svg && hb_blob_get_length(svg) > 0) {
        result |= ColorFontType::SVG;
    }
    if (svg) hb_blob_destroy(svg);

    return result;
}

bool isColorGlyph(void* hb_face_ptr, uint32_t glyphId) {
    if (!hb_face_ptr) return false;

    auto* face = static_cast<hb_face_t*>(hb_face_ptr);

    // Check COLR layers
    if (hb_ot_color_has_layers(face)) {
        unsigned int layer_count = 0;
        hb_ot_color_glyph_get_layers(face, glyphId, 0, &layer_count, nullptr);
        if (layer_count > 0) return true;
    }

#if HB_VERSION_ATLEAST(7, 0, 0)
    // Check COLRv1 paint
    if (hb_ot_color_has_paint(face)) {
        if (hb_ot_color_glyph_has_paint(face, glyphId)) return true;
    }
#endif

    // Check sbix
    hb_blob_t* sbix = hb_face_reference_table(face, HB_TAG('s', 'b', 'i', 'x'));
    bool has_sbix = sbix && hb_blob_get_length(sbix) > 0;
    if (sbix) hb_blob_destroy(sbix);
    if (has_sbix) {
        // sbix fonts typically have color data for all non-.notdef glyphs;
        // a precise per-glyph check would require parsing the sbix table.
        // Return true if the font has sbix and glyph is not 0.
        if (glyphId != 0) return true;
    }

    return false;
}

std::string colorFontTypeName(ColorFontType type) {
    if (type == ColorFontType::None) return "None";

    std::string result;
    auto append = [&](const char* name) {
        if (!result.empty()) result += " | ";
        result += name;
    };

    if (hasFlag(type, ColorFontType::COLR_v0)) append("COLR v0");
    if (hasFlag(type, ColorFontType::COLR_v1)) append("COLR v1");
    if (hasFlag(type, ColorFontType::SBIX))    append("SBIX");
    if (hasFlag(type, ColorFontType::CBDT))    append("CBDT");
    if (hasFlag(type, ColorFontType::SVG))     append("SVG");

    return result.empty() ? "None" : result;
}

} // namespace termcore
