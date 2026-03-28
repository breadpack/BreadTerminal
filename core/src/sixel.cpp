#include "termcore/sixel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

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

// Growable flat pixel buffer that writes directly during parsing.
// Avoids the enormous overhead of nested unordered_map.
struct PixelBuffer {
    std::vector<uint32_t> data;
    int width = 0;   // current allocated width
    int height = 0;  // current allocated height
    int maxX = 0;    // actual used width  (exclusive)
    int maxY = 0;    // actual used height (exclusive)

    // Ensure the buffer can hold pixel (x, y). Grows in chunks to amortize
    // reallocations. When growing, existing pixel rows are re-laid-out.
    void ensureSize(int needW, int needH) {
        if (needW <= width && needH <= height) return;

        int newW = width;
        int newH = height;

        // Grow width: at least double or round up to 256-pixel boundary
        if (needW > newW) {
            newW = std::max(needW, newW * 2);
            newW = (newW + 255) & ~255; // align to 256
        }
        // Grow height: at least double or round up to 64-row boundary
        if (needH > newH) {
            newH = std::max(needH, newH * 2);
            newH = (newH + 63) & ~63; // align to 64
        }

        if (newW == width && newH != height) {
            // Only height grew, rows stay the same width - just resize
            data.resize(static_cast<size_t>(newW) * newH, 0);
        } else if (newW != width) {
            // Width changed: need to re-layout rows
            std::vector<uint32_t> newData(static_cast<size_t>(newW) * newH, 0);
            int copyRows = std::min(height, newH);
            int copyCols = std::min(width, newW);
            for (int row = 0; row < copyRows; ++row) {
                std::memcpy(
                    &newData[static_cast<size_t>(row) * newW],
                    &data[static_cast<size_t>(row) * width],
                    static_cast<size_t>(copyCols) * sizeof(uint32_t));
            }
            data = std::move(newData);
        }

        width = newW;
        height = newH;
    }

    // Set a single pixel. Hot path - keep minimal.
    inline void setPixel(int x, int y, uint32_t color) {
        data[static_cast<size_t>(y) * width + x] = color;
    }

    // Produce the final tightly-packed SixelImage.
    SixelImage toImage() const {
        if (maxX == 0 || maxY == 0) return {};

        SixelImage image;
        image.width = maxX;
        image.height = maxY;
        image.pixels.resize(static_cast<size_t>(maxX) * maxY, 0);

        if (maxX == width) {
            // Rows are already the right width, bulk copy
            std::memcpy(image.pixels.data(), data.data(),
                        static_cast<size_t>(maxX) * maxY * sizeof(uint32_t));
        } else {
            // Copy row by row (buffer is wider than final image)
            for (int row = 0; row < maxY; ++row) {
                std::memcpy(
                    &image.pixels[static_cast<size_t>(row) * maxX],
                    &data[static_cast<size_t>(row) * width],
                    static_cast<size_t>(maxX) * sizeof(uint32_t));
            }
        }
        return image;
    }
};

static constexpr int MAX_COLOR_REGISTERS = 1024;

} // anonymous namespace

SixelImage parseSixel(const std::string& data) {
    if (data.empty()) {
        return {};
    }

    PixelBuffer buf;
    // Pre-allocate a reasonable starting size
    buf.ensureSize(256, 64);

    // Fixed-size color register array (Sixel spec: typically 256, allow up to 1024)
    std::array<uint32_t, MAX_COLOR_REGISTERS> colorRegisters{};
    std::array<bool, MAX_COLOR_REGISTERS> colorDefined{};
    // Default: register 0 = white
    colorRegisters[0] = 0xFFFFFFFF;
    colorDefined[0] = true;

    int currentColor = 0;
    uint32_t currentColorValue = 0xFFFFFFFF; // cached lookup
    int cursorX = 0;
    int cursorY = 0; // in pixel rows (top of current band)

    size_t pos = 0;
    const size_t dataSize = data.size();
    const char* dataPtr = data.data();

    while (pos < dataSize) {
        char ch = dataPtr[pos];

        if (ch >= 0x3F && ch <= 0x7E) {
            // Sixel data character - HOT PATH
            int bits = ch - 0x3F;

            if (bits != 0) {
                // Find the highest set bit to know how tall this column is
                int maxBit = 0;
                for (int bit = 5; bit >= 0; --bit) {
                    if (bits & (1 << bit)) { maxBit = bit; break; }
                }
                int needY = cursorY + maxBit + 1;
                int needX = cursorX + 1;

                // Ensure buffer is large enough
                buf.ensureSize(needX, needY);

                // Write pixels for set bits
                uint32_t color = currentColorValue;
                for (int bit = 0; bit < 6; ++bit) {
                    if (bits & (1 << bit)) {
                        buf.setPixel(cursorX, cursorY + bit, color);
                    }
                }

                if (needY > buf.maxY) buf.maxY = needY;
                if (needX > buf.maxX) buf.maxX = needX;
            }
            // Even if bits==0, cursor advances (but no maxX/maxY update since
            // no pixels are set - matching original behavior where '?' produces empty)
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
            if (pos < dataSize) {
                char repCh = dataPtr[pos];
                ++pos;
                if (repCh >= 0x3F && repCh <= 0x7E) {
                    int bits = repCh - 0x3F;

                    if (bits != 0) {
                        // Find highest set bit
                        int maxBit = 0;
                        for (int bit = 5; bit >= 0; --bit) {
                            if (bits & (1 << bit)) { maxBit = bit; break; }
                        }
                        int needY = cursorY + maxBit + 1;
                        int needX = cursorX + count;

                        buf.ensureSize(needX, needY);

                        uint32_t color = currentColorValue;

                        // Write the repeated columns
                        for (int i = 0; i < count; ++i) {
                            int x = cursorX + i;
                            for (int bit = 0; bit < 6; ++bit) {
                                if (bits & (1 << bit)) {
                                    buf.setPixel(x, cursorY + bit, color);
                                }
                            }
                        }

                        if (needY > buf.maxY) buf.maxY = needY;
                        if (needX > buf.maxX) buf.maxX = needX;
                    }
                    cursorX += count;
                }
            }

        } else if (ch == '#') {
            // Color introducer
            ++pos;
            int pc = parseInt(data, pos);

            if (pos < dataSize && dataPtr[pos] == ';') {
                // Color definition: #Pc;Pu;Px;Py;Pz
                ++pos;
                int pu = parseInt(data, pos);
                if (pos < dataSize && dataPtr[pos] == ';') ++pos;
                int px = parseInt(data, pos);
                if (pos < dataSize && dataPtr[pos] == ';') ++pos;
                int py = parseInt(data, pos);
                if (pos < dataSize && dataPtr[pos] == ';') ++pos;
                int pz = parseInt(data, pos);

                uint32_t color = 0xFFFFFFFF;
                if (pu == 2) {
                    // RGB: percentages 0-100
                    uint8_t r = static_cast<uint8_t>(px * 255 / 100);
                    uint8_t g = static_cast<uint8_t>(py * 255 / 100);
                    uint8_t b = static_cast<uint8_t>(pz * 255 / 100);
                    color =
                        (static_cast<uint32_t>(r) << 24) |
                        (static_cast<uint32_t>(g) << 16) |
                        (static_cast<uint32_t>(b) << 8) |
                        0xFF;
                } else if (pu == 1) {
                    // HLS
                    uint8_t r, g, b;
                    hlsToRgb(px, py, pz, r, g, b);
                    color =
                        (static_cast<uint32_t>(r) << 24) |
                        (static_cast<uint32_t>(g) << 16) |
                        (static_cast<uint32_t>(b) << 8) |
                        0xFF;
                }

                if (pc >= 0 && pc < MAX_COLOR_REGISTERS) {
                    colorRegisters[pc] = color;
                    colorDefined[pc] = true;
                    // Update cached value if this is the currently selected color
                    if (pc == currentColor) {
                        currentColorValue = color;
                    }
                }
            } else {
                // Color select only
                currentColor = pc;
                if (pc >= 0 && pc < MAX_COLOR_REGISTERS && colorDefined[pc]) {
                    currentColorValue = colorRegisters[pc];
                } else {
                    currentColorValue = 0xFFFFFFFF;
                }
            }

        } else {
            // Skip unknown characters
            ++pos;
        }
    }

    return buf.toImage();
}

} // namespace termcore
