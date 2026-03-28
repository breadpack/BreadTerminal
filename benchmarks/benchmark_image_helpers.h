#ifndef BENCHMARK_IMAGE_HELPERS_H
#define BENCHMARK_IMAGE_HELPERS_H

#include <cstdint>
#include <string>
#include <vector>

#include "termcore/base64.h"

namespace bench {

// --- Sixel data generators ---

/// Generate a simple single-color sixel image (width x height_bands*6 pixels).
/// Produces a solid filled rectangle using one color register.
inline std::string generateSimpleSixel(int width, int height_bands) {
    std::string data;
    data.reserve(width * height_bands * 2 + 64);

    // Define color 1 as bright red (RGB percentages)
    data += "#1;2;100;0;0";
    // Select color 1
    data += "#1";

    // All 6 bits set = character 0x7E = '~'
    for (int band = 0; band < height_bands; ++band) {
        if (band > 0) {
            data += '-'; // New line (next band)
        }
        // Use repeat syntax for efficiency: !count char
        data += "!" + std::to_string(width) + "~";
    }

    return data;
}

/// Generate a multi-color sixel image with color definitions and repeat sequences.
/// Simulates a 256-color image with horizontal color stripes.
inline std::string generateComplexSixel(int width, int height_bands, int num_colors) {
    std::string data;
    data.reserve(width * height_bands * 4 + num_colors * 20);

    // Define color registers with varied RGB values
    for (int c = 0; c < num_colors; ++c) {
        int r = (c * 7) % 101;
        int g = (c * 13 + 30) % 101;
        int b = (c * 19 + 60) % 101;
        data += "#" + std::to_string(c) + ";2;"
              + std::to_string(r) + ";"
              + std::to_string(g) + ";"
              + std::to_string(b);
    }

    for (int band = 0; band < height_bands; ++band) {
        if (band > 0) {
            data += '-';
        }
        // Each band uses multiple colors with carriage return overlays
        int colors_per_band = std::min(num_colors, 4);
        for (int pass = 0; pass < colors_per_band; ++pass) {
            if (pass > 0) {
                data += '$'; // Carriage return within band
            }
            int color_idx = (band * colors_per_band + pass) % num_colors;
            data += "#" + std::to_string(color_idx);

            // Alternate between repeat runs and individual characters
            int col = 0;
            while (col < width) {
                int run_len = 8 + (col % 16);
                if (col + run_len > width) run_len = width - col;

                // Pick a sixel pattern based on pass to create visual variety
                char sixel_char = static_cast<char>(0x3F + ((pass * 17 + band * 3 + col) % 63));
                if (run_len > 3) {
                    data += "!" + std::to_string(run_len) + sixel_char;
                } else {
                    for (int i = 0; i < run_len; ++i) {
                        data += sixel_char;
                    }
                }
                col += run_len;
            }
        }
    }

    return data;
}

/// Generate a large sixel payload for throughput testing.
/// Produces many bands with dense pixel data.
inline std::string generateLargeSixel(size_t target_bytes) {
    std::string data;
    data.reserve(target_bytes + 256);

    // Define 16 colors
    for (int c = 0; c < 16; ++c) {
        data += "#" + std::to_string(c) + ";2;"
              + std::to_string(c * 6) + ";"
              + std::to_string(50) + ";"
              + std::to_string(100 - c * 6);
    }

    int band = 0;
    int color = 0;
    while (data.size() < target_bytes) {
        if (band > 0) {
            data += '-';
        }
        data += "#" + std::to_string(color % 16);

        // Fill a wide row using repeat sequences
        int remaining = static_cast<int>(target_bytes - data.size());
        int width = std::min(1024, remaining / 2);
        if (width <= 0) break;

        data += "!" + std::to_string(width) + "~";

        ++band;
        ++color;
    }

    return data;
}

// --- Base64 encoding helper (delegates to termcore::base64Encode) ---

inline std::string base64Encode(const std::vector<uint8_t>& input) {
    return termcore::base64Encode(input);
}

/// Generate a base64-encoded payload of approximately target_bytes raw size.
inline std::string generateBase64Payload(size_t raw_bytes) {
    std::vector<uint8_t> raw;
    raw.reserve(raw_bytes);
    for (size_t i = 0; i < raw_bytes; ++i) {
        raw.push_back(static_cast<uint8_t>((i * 37 + 13) & 0xFF));
    }
    return base64Encode(raw);
}

/// Generate a Kitty graphics control string for a single-chunk transmit.
/// format: 32 = RGBA raw, width x height
inline std::string generateKittyControl(uint32_t id, int width, int height, int format,
                                         bool more_chunks = false) {
    std::string ctrl = "a=t,f=" + std::to_string(format)
                     + ",s=" + std::to_string(width)
                     + ",v=" + std::to_string(height)
                     + ",i=" + std::to_string(id);
    if (more_chunks) {
        ctrl += ",m=1";
    } else {
        ctrl += ",m=0";
    }
    return ctrl;
}

/// Generate an iTerm2 OSC 1337 parameter string with base64 payload.
inline std::string generateITermOsc(const std::string& name_b64, int size,
                                     const std::string& width_spec,
                                     const std::string& height_spec,
                                     bool preserve_aspect, bool inline_display,
                                     const std::string& payload_b64) {
    std::string osc;
    if (!name_b64.empty()) {
        osc += "name=" + name_b64 + ";";
    }
    if (size > 0) {
        osc += "size=" + std::to_string(size) + ";";
    }
    if (!width_spec.empty()) {
        osc += "width=" + width_spec + ";";
    }
    if (!height_spec.empty()) {
        osc += "height=" + height_spec + ";";
    }
    osc += "preserveAspectRatio=" + std::string(preserve_aspect ? "1" : "0") + ";";
    osc += "inline=" + std::string(inline_display ? "1" : "0");
    osc += ":" + payload_b64;
    return osc;
}

} // namespace bench

#endif // BENCHMARK_IMAGE_HELPERS_H
