#include "termcore/sixel.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace termcore {

namespace {

// Convert HLS (Hue 0-360, Lightness 0-100, Saturation 0-100) to RGB (0-255 each)
void hlsToRgb(int h, int l, int s, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (s == 0) {
        uint8_t v = static_cast<uint8_t>(l * 255 / 100);
        r = g = b = v;
        return;
    }

    double hue = static_cast<double>(h) / 360.0;
    double lightness = static_cast<double>(l) / 100.0;
    double saturation = static_cast<double>(s) / 100.0;

    auto hueToRgb = [](double p, double q, double t) -> double {
        if (t < 0.0) t += 1.0;
        if (t > 1.0) t -= 1.0;
        if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
        if (t < 1.0 / 2.0) return q;
        if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
        return p;
    };

    double q = lightness < 0.5
        ? lightness * (1.0 + saturation)
        : lightness + saturation - lightness * saturation;
    double p = 2.0 * lightness - q;

    r = static_cast<uint8_t>(std::round(hueToRgb(p, q, hue + 1.0 / 3.0) * 255.0));
    g = static_cast<uint8_t>(std::round(hueToRgb(p, q, hue) * 255.0));
    b = static_cast<uint8_t>(std::round(hueToRgb(p, q, hue - 1.0 / 3.0) * 255.0));
}

// Parse an integer from data starting at pos, advancing pos past the digits.
// Returns 0 if no digits found.
int parseInt(const std::string& data, size_t& pos) {
    int value = 0;
    bool found = false;
    while (pos < data.size() && data[pos] >= '0' && data[pos] <= '9') {
        value = value * 10 + (data[pos] - '0');
        found = true;
        ++pos;
    }
    return found ? value : 0;
}

} // anonymous namespace

SixelImage parseSixel(const std::string& data) {
    if (data.empty()) {
        return {};
    }

    // Dynamic row storage: map from (x, band_y) to RGBA color
    // band_y is the six-pixel band index, each band covers 6 vertical pixels
    struct PixelRow {
        std::unordered_map<int, uint32_t> columns; // x -> RGBA
    };
    // rows[band_y][bit_index (0-5)] stores column data
    // Instead, store per-pixel: map<(x, y), color>
    // For efficiency, use a vector of rows, each row is a map of x -> color

    std::unordered_map<int, std::unordered_map<int, uint32_t>> pixelMap;
    // pixelMap[y][x] = RGBA color

    int maxX = 0;
    int maxY = 0;

    // Color registers (up to 256)
    std::unordered_map<int, uint32_t> colorRegisters;
    // Default: register 0 = white
    colorRegisters[0] = 0xFFFFFFFF;

    int currentColor = 0;
    int cursorX = 0;
    int cursorY = 0; // in pixel rows (top of current band)

    size_t pos = 0;

    while (pos < data.size()) {
        char ch = data[pos];

        if (ch >= 0x3F && ch <= 0x7E) {
            // Sixel data character
            int bits = ch - 0x3F;
            uint32_t color = colorRegisters.count(currentColor)
                ? colorRegisters[currentColor]
                : 0xFFFFFFFF;

            for (int bit = 0; bit < 6; ++bit) {
                if (bits & (1 << bit)) {
                    int py = cursorY + bit;
                    pixelMap[py][cursorX] = color;
                    if (py + 1 > maxY) maxY = py + 1;
                }
            }
            if (cursorX + 1 > maxX) maxX = cursorX + 1;
            ++cursorX;
            ++pos;

        } else if (ch == '$') {
            // Carriage return
            cursorX = 0;
            ++pos;

        } else if (ch == '-') {
            // New line
            cursorY += 6;
            cursorX = 0;
            ++pos;

        } else if (ch == '!') {
            // Repeat introducer: !count char
            ++pos;
            int count = parseInt(data, pos);
            if (count <= 0) count = 1;
            if (pos < data.size()) {
                char repCh = data[pos];
                ++pos;
                if (repCh >= 0x3F && repCh <= 0x7E) {
                    int bits = repCh - 0x3F;
                    uint32_t color = colorRegisters.count(currentColor)
                        ? colorRegisters[currentColor]
                        : 0xFFFFFFFF;

                    for (int i = 0; i < count; ++i) {
                        for (int bit = 0; bit < 6; ++bit) {
                            if (bits & (1 << bit)) {
                                int py = cursorY + bit;
                                pixelMap[py][cursorX] = color;
                                if (py + 1 > maxY) maxY = py + 1;
                            }
                        }
                        if (cursorX + 1 > maxX) maxX = cursorX + 1;
                        ++cursorX;
                    }
                }
            }

        } else if (ch == '#') {
            // Color introducer
            ++pos;
            int pc = parseInt(data, pos);

            if (pos < data.size() && data[pos] == ';') {
                // Color definition: #Pc;Pu;Px;Py;Pz
                ++pos;
                int pu = parseInt(data, pos);
                if (pos < data.size() && data[pos] == ';') ++pos;
                int px = parseInt(data, pos);
                if (pos < data.size() && data[pos] == ';') ++pos;
                int py = parseInt(data, pos);
                if (pos < data.size() && data[pos] == ';') ++pos;
                int pz = parseInt(data, pos);

                if (pu == 2) {
                    // RGB: percentages 0-100
                    uint8_t r = static_cast<uint8_t>(px * 255 / 100);
                    uint8_t g = static_cast<uint8_t>(py * 255 / 100);
                    uint8_t b = static_cast<uint8_t>(pz * 255 / 100);
                    colorRegisters[pc] =
                        (static_cast<uint32_t>(r) << 24) |
                        (static_cast<uint32_t>(g) << 16) |
                        (static_cast<uint32_t>(b) << 8) |
                        0xFF;
                } else if (pu == 1) {
                    // HLS
                    uint8_t r, g, b;
                    hlsToRgb(px, py, pz, r, g, b);
                    colorRegisters[pc] =
                        (static_cast<uint32_t>(r) << 24) |
                        (static_cast<uint32_t>(g) << 16) |
                        (static_cast<uint32_t>(b) << 8) |
                        0xFF;
                }
            } else {
                // Color select only
                currentColor = pc;
            }

        } else {
            // Skip unknown characters
            ++pos;
        }
    }

    if (maxX == 0 || maxY == 0) {
        return {};
    }

    SixelImage image;
    image.width = maxX;
    image.height = maxY;
    image.pixels.resize(static_cast<size_t>(maxX) * maxY, 0x00000000);

    for (auto& [y, row] : pixelMap) {
        for (auto& [x, color] : row) {
            if (x < maxX && y < maxY) {
                image.pixels[static_cast<size_t>(y) * maxX + x] = color;
            }
        }
    }

    return image;
}

} // namespace termcore
