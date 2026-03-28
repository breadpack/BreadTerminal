#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#include "stb_image.h"

#include "termcore/iterm_image.h"
#include "termcore/base64.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace termcore {

namespace {

/// Parse a dimension string like "auto", "80", "100px", "50%".
ITermDimension parseDimension(const std::string& s) {
    ITermDimension dim;
    if (s.empty() || s == "auto") {
        dim.unit = ITermDimension::Unit::Auto;
        return dim;
    }

    if (s.size() > 2 && s.compare(s.size() - 2, 2, "px") == 0) {
        dim.unit = ITermDimension::Unit::Pixels;
        dim.value = std::atoi(s.substr(0, s.size() - 2).c_str());
    } else if (s.back() == '%') {
        dim.unit = ITermDimension::Unit::Percent;
        dim.value = std::atoi(s.substr(0, s.size() - 1).c_str());
    } else {
        // Plain number = cells
        dim.unit = ITermDimension::Unit::Cells;
        dim.value = std::atoi(s.c_str());
    }
    return dim;
}

} // anonymous namespace

bool parseITermImageOsc(const std::string& osc_after_file,
                        ITermImageParams& params,
                        std::string& base64_payload) {
    // Format: key=value;key=value;...:base64data
    // Find the colon that separates params from payload
    auto colon_pos = osc_after_file.find(':');
    if (colon_pos == std::string::npos) {
        return false; // No payload
    }

    std::string param_str = osc_after_file.substr(0, colon_pos);
    base64_payload = osc_after_file.substr(colon_pos + 1);

    if (base64_payload.empty()) {
        return false;
    }

    // Parse semicolon-separated key=value pairs
    size_t start = 0;
    while (start < param_str.size()) {
        size_t semi = param_str.find(';', start);
        if (semi == std::string::npos) semi = param_str.size();

        std::string token = param_str.substr(start, semi - start);
        size_t eq = token.find('=');
        if (eq != std::string::npos) {
            std::string key = token.substr(0, eq);
            std::string value = token.substr(eq + 1);

            if (key == "name") {
                // name is base64-encoded
                auto decoded = termcore::base64Decode(value);
                params.name = std::string(decoded.begin(), decoded.end());
            } else if (key == "size") {
                params.size = std::atoi(value.c_str());
            } else if (key == "width") {
                params.width = parseDimension(value);
            } else if (key == "height") {
                params.height = parseDimension(value);
            } else if (key == "preserveAspectRatio") {
                params.preserve_aspect_ratio = (value != "0");
            } else if (key == "inline") {
                params.inline_display = (value == "1");
            }
        }
        start = semi + 1;
    }

    return true;
}

std::vector<uint8_t> iTermBase64Decode(const std::string& input) {
    return termcore::base64Decode(input);
}

bool decodeImageData(const std::vector<uint8_t>& encoded_data,
                     int& width, int& height,
                     std::vector<uint8_t>& rgba_pixels) {
    if (encoded_data.empty()) return false;

    int w = 0, h = 0, channels = 0;
    unsigned char* data = stbi_load_from_memory(
        encoded_data.data(),
        static_cast<int>(encoded_data.size()),
        &w, &h, &channels,
        4 /* request RGBA */);

    if (!data) return false;

    width = w;
    height = h;
    size_t pixel_count = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    rgba_pixels.assign(data, data + pixel_count);

    stbi_image_free(data);
    return true;
}

void calculateDisplayCells(const ITermImageParams& params,
                           int cell_width_px, int cell_height_px,
                           int terminal_cols, int terminal_rows,
                           int image_width_px, int image_height_px,
                           int& out_cols, int& out_rows) {
    if (cell_width_px <= 0) cell_width_px = 8;
    if (cell_height_px <= 0) cell_height_px = 16;
    if (image_width_px <= 0 || image_height_px <= 0) {
        out_cols = 1;
        out_rows = 1;
        return;
    }

    // Resolve width dimension to pixels
    auto resolveWidth = [&](const ITermDimension& dim) -> int {
        switch (dim.unit) {
        case ITermDimension::Unit::Cells:
            return dim.value * cell_width_px;
        case ITermDimension::Unit::Pixels:
            return dim.value;
        case ITermDimension::Unit::Percent:
            return terminal_cols * cell_width_px * dim.value / 100;
        case ITermDimension::Unit::Auto:
        default:
            return image_width_px;
        }
    };

    auto resolveHeight = [&](const ITermDimension& dim) -> int {
        switch (dim.unit) {
        case ITermDimension::Unit::Cells:
            return dim.value * cell_height_px;
        case ITermDimension::Unit::Pixels:
            return dim.value;
        case ITermDimension::Unit::Percent:
            return terminal_rows * cell_height_px * dim.value / 100;
        case ITermDimension::Unit::Auto:
        default:
            return image_height_px;
        }
    };

    int target_w = resolveWidth(params.width);
    int target_h = resolveHeight(params.height);

    // Apply aspect ratio preservation
    if (params.preserve_aspect_ratio) {
        bool w_auto = (params.width.unit == ITermDimension::Unit::Auto);
        bool h_auto = (params.height.unit == ITermDimension::Unit::Auto);

        if (w_auto && h_auto) {
            // Both auto: use natural size
            target_w = image_width_px;
            target_h = image_height_px;
        } else if (w_auto) {
            // Width auto, height specified: scale width to match
            double aspect = static_cast<double>(image_width_px) / image_height_px;
            target_w = static_cast<int>(std::round(target_h * aspect));
        } else if (h_auto) {
            // Height auto, width specified: scale height to match
            double aspect = static_cast<double>(image_height_px) / image_width_px;
            target_h = static_cast<int>(std::round(target_w * aspect));
        } else {
            // Both specified: fit within the box, preserving aspect ratio
            double aspect = static_cast<double>(image_width_px) / image_height_px;
            double box_aspect = static_cast<double>(target_w) / target_h;
            if (aspect > box_aspect) {
                // Image is wider than box: constrain by width
                target_h = static_cast<int>(std::round(target_w / aspect));
            } else {
                // Image is taller than box: constrain by height
                target_w = static_cast<int>(std::round(target_h * aspect));
            }
        }
    }

    // Convert pixel dimensions to cell counts (ceiling division)
    out_cols = std::max(1, (target_w + cell_width_px - 1) / cell_width_px);
    out_rows = std::max(1, (target_h + cell_height_px - 1) / cell_height_px);

    // Clamp to terminal dimensions
    out_cols = std::min(out_cols, terminal_cols);
    out_rows = std::min(out_rows, terminal_rows);
}

} // namespace termcore
