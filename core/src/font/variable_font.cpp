#include "termcore/font/variable_font.h"
#include <hb.h>
#include <hb-ot.h>
#include <sstream>

namespace termcore {

uint32_t parseAxisTag(const std::string& tag_str) {
    if (tag_str.size() != 4) return 0;
    return HB_TAG(tag_str[0], tag_str[1], tag_str[2], tag_str[3]);
}

std::string axisTagToString(uint32_t tag) {
    char buf[5] = {
        static_cast<char>((tag >> 24) & 0xFF),
        static_cast<char>((tag >> 16) & 0xFF),
        static_cast<char>((tag >> 8) & 0xFF),
        static_cast<char>(tag & 0xFF),
        0
    };
    return buf;
}

std::vector<FontAxis> queryAxes(const std::string& font_path, int face_index) {
    std::vector<FontAxis> axes;
    hb_blob_t* blob = hb_blob_create_from_file(font_path.c_str());
    if (!blob || hb_blob_get_length(blob) == 0) {
        if (blob) hb_blob_destroy(blob);
        return axes;
    }
    hb_face_t* face = hb_face_create(blob, face_index);

    unsigned count = hb_ot_var_get_axis_count(face);
    if (count > 0) {
        std::vector<hb_ot_var_axis_info_t> infos(count);
        unsigned actual = count;
        hb_ot_var_get_axis_infos(face, 0, &actual, infos.data());

        for (unsigned i = 0; i < actual; i++) {
            FontAxis axis;
            axis.tag = infos[i].tag;
            axis.min_value = infos[i].min_value;
            axis.max_value = infos[i].max_value;
            axis.default_value = infos[i].default_value;

            // Get name
            char name_buf[128] = {};
            unsigned name_len = sizeof(name_buf);
            hb_ot_name_get_utf8(face, infos[i].name_id,
                                HB_LANGUAGE_INVALID, &name_len, name_buf);
            axis.name = std::string(name_buf, name_len);

            axes.push_back(axis);
        }
    }

    hb_face_destroy(face);
    hb_blob_destroy(blob);
    return axes;
}

bool isVariableFont(const std::string& font_path, int face_index) {
    return !queryAxes(font_path, face_index).empty();
}

std::vector<FontVariation> parseVariations(const std::string& config_str) {
    std::vector<FontVariation> result;
    if (config_str.empty()) return result;

    std::istringstream ss(config_str);
    std::string token;
    while (std::getline(ss, token, ',')) {
        auto eq = token.find('=');
        if (eq != std::string::npos && eq >= 4) {
            FontVariation v;
            v.tag = parseAxisTag(token.substr(eq - 4, 4));
            try {
                v.value = std::stof(token.substr(eq + 1));
            } catch (...) {
                continue;
            }
            if (v.tag != 0) {
                result.push_back(v);
            }
        }
    }
    return result;
}

bool applyVariations(void* hb_font_ptr,
                     const std::vector<FontVariation>& variations) {
    auto* font = static_cast<hb_font_t*>(hb_font_ptr);
    if (!font || variations.empty()) return false;

    std::vector<hb_variation_t> hb_vars(variations.size());
    for (size_t i = 0; i < variations.size(); i++) {
        hb_vars[i].tag = variations[i].tag;
        hb_vars[i].value = variations[i].value;
    }
    hb_font_set_variations(font, hb_vars.data(),
                           static_cast<unsigned>(hb_vars.size()));
    return true;
}

} // namespace termcore
