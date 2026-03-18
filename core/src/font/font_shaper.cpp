#include "termcore/font/font_shaper.h"
#include "termcore/font/unicode_width.h"

#include <hb.h>

#include <algorithm>
#include <cassert>

namespace termcore {

FontShaper::FontShaper() = default;

FontShaper::~FontShaper() {
    // Free HarfBuzz objects in reverse order of creation
    for (auto it = fonts_.rbegin(); it != fonts_.rend(); ++it) {
        if (it->font) hb_font_destroy(it->font);
        if (it->face) hb_face_destroy(it->face);
        if (it->blob) hb_blob_destroy(it->blob);
    }
    fonts_.clear();
}

FontShaper::FontEntry* FontShaper::findFont(FontFaceId id) {
    for (auto& entry : fonts_) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

FontFaceId FontShaper::loadFont(const std::string& font_path, int face_index, float size) {
    hb_blob_t* blob = hb_blob_create_from_file(font_path.c_str());
    if (!blob || hb_blob_get_length(blob) == 0) {
        if (blob) hb_blob_destroy(blob);
        return kInvalidFontFace;
    }

    hb_face_t* face = hb_face_create(blob, static_cast<unsigned>(face_index));
    if (!face) {
        hb_blob_destroy(blob);
        return kInvalidFontFace;
    }

    hb_font_t* font = hb_font_create(face);
    if (!font) {
        hb_face_destroy(face);
        hb_blob_destroy(blob);
        return kInvalidFontFace;
    }

    // Set scale in 26.6 fixed point (multiply by 64)
    int scale = static_cast<int>(size * 64.0f);
    hb_font_set_scale(font, scale, scale);

    FontFaceId id = next_id_++;
    fonts_.push_back(FontEntry{id, blob, face, font, size});
    return id;
}

void FontShaper::setFontSize(FontFaceId face_id, float size) {
    FontEntry* entry = findFont(face_id);
    if (!entry) return;

    entry->size = size;
    int scale = static_cast<int>(size * 64.0f);
    hb_font_set_scale(entry->font, scale, scale);
}

std::vector<ShapedGlyph> FontShaper::shape(FontFaceId face_id,
                                            const std::u32string& codepoints,
                                            const ShaperConfig& config) {
    std::vector<ShapedGlyph> result;

    if (codepoints.empty()) return result;

    FontEntry* entry = findFont(face_id);
    if (!entry) return result;

    // Create buffer
    hb_buffer_t* buf = hb_buffer_create();
    if (!buf) return result;

    hb_buffer_set_cluster_level(buf, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);
    hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
    hb_buffer_set_script(buf, HB_SCRIPT_COMMON);

    // Add codepoints (cast char32_t* to uint32_t*)
    hb_buffer_add_codepoints(buf,
                              reinterpret_cast<const uint32_t*>(codepoints.data()),
                              static_cast<int>(codepoints.size()),
                              0,
                              static_cast<int>(codepoints.size()));

    // Build features
    std::vector<hb_feature_t> features;

    hb_feature_t calt_feature;
    hb_feature_from_string(config.enable_ligatures ? "+calt" : "-calt", -1, &calt_feature);
    features.push_back(calt_feature);

    hb_feature_t liga_feature;
    hb_feature_from_string(config.enable_liga ? "+liga" : "-liga", -1, &liga_feature);
    features.push_back(liga_feature);

    for (const auto& feat_str : config.extra_features) {
        hb_feature_t feat;
        if (hb_feature_from_string(feat_str.c_str(),
                                    static_cast<int>(feat_str.size()), &feat)) {
            features.push_back(feat);
        }
    }

    // Shape
    hb_shape(entry->font, buf, features.data(), static_cast<unsigned>(features.size()));

    // Extract results
    unsigned int glyph_count = 0;
    hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buf, &glyph_count);
    hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buf, &glyph_count);

    result.reserve(glyph_count);
    for (unsigned int i = 0; i < glyph_count; ++i) {
        ShapedGlyph g;
        g.glyph_index = infos[i].codepoint;
        g.face_id = face_id;
        g.x_advance = positions[i].x_advance;
        g.y_advance = positions[i].y_advance;
        g.x_offset = positions[i].x_offset;
        g.y_offset = positions[i].y_offset;
        g.cluster = infos[i].cluster;
        result.push_back(g);
    }

    hb_buffer_destroy(buf);
    return result;
}

std::vector<ShapedRun> FontShaper::shapeForGrid(FontFaceId face_id,
                                                  const std::u32string& codepoints,
                                                  float cell_width,
                                                  const ShaperConfig& config) {
    std::vector<ShapedRun> runs;

    if (codepoints.empty() || cell_width <= 0) return runs;

    // Shape first
    auto glyphs = shape(face_id, codepoints, config);
    if (glyphs.empty()) return runs;

    // Build a map from codepoint index to cell position
    // Each codepoint occupies 1 or 2 cells based on unicode display width
    std::vector<int> cp_to_cell(codepoints.size());
    int cell_pos = 0;
    for (size_t i = 0; i < codepoints.size(); ++i) {
        cp_to_cell[i] = cell_pos;
        int w = codepoint_width(codepoints[i]);
        cell_pos += (w > 0) ? w : 1;  // At least 1 cell per codepoint for grid purposes
    }
    int total_cells = cell_pos;

    // Group glyphs into runs based on contiguous cell ranges
    // A run is a group of glyphs whose clusters map to a contiguous range of cells
    if (glyphs.empty()) return runs;

    ShapedRun current_run;
    // Determine the cell range for the first glyph
    uint32_t first_cluster = glyphs[0].cluster;
    int first_cell = (first_cluster < codepoints.size()) ? cp_to_cell[first_cluster] : 0;
    current_run.start_cell = first_cell;
    current_run.glyphs.push_back(glyphs[0]);

    // Track the max cell reached by the current run
    auto cellEnd = [&](uint32_t cluster) -> int {
        if (cluster >= codepoints.size()) return 0;
        int w = codepoint_width(codepoints[cluster]);
        return cp_to_cell[cluster] + ((w > 0) ? w : 1);
    };

    int run_end_cell = cellEnd(first_cluster);

    for (size_t i = 1; i < glyphs.size(); ++i) {
        uint32_t cluster = glyphs[i].cluster;
        int glyph_cell = (cluster < codepoints.size()) ? cp_to_cell[cluster] : run_end_cell;

        // Check if this glyph is contiguous with current run
        if (glyph_cell <= run_end_cell) {
            // Still part of current run (same cluster or adjacent)
            current_run.glyphs.push_back(glyphs[i]);
            int glyph_end = cellEnd(cluster);
            if (glyph_end > run_end_cell) run_end_cell = glyph_end;
        } else {
            // Finalize current run
            current_run.cell_count = run_end_cell - current_run.start_cell;
            runs.push_back(std::move(current_run));

            // Start new run
            current_run = ShapedRun{};
            current_run.start_cell = glyph_cell;
            current_run.glyphs.push_back(glyphs[i]);
            run_end_cell = cellEnd(cluster);
        }
    }

    // Finalize last run
    current_run.cell_count = run_end_cell - current_run.start_cell;
    runs.push_back(std::move(current_run));

    return runs;
}

uint32_t FontShaper::getGlyphIndex(FontFaceId face_id, char32_t codepoint) {
    FontEntry* entry = findFont(face_id);
    if (!entry) return 0;

    hb_codepoint_t glyph_id = 0;
    hb_font_get_nominal_glyph(entry->font, static_cast<hb_codepoint_t>(codepoint), &glyph_id);
    return glyph_id;
}

bool FontShaper::hasGlyph(FontFaceId face_id, char32_t codepoint) {
    return getGlyphIndex(face_id, codepoint) != 0;
}

} // namespace termcore
