#ifndef TERMCORE_BOX_DRAWING_UTIL_H
#define TERMCORE_BOX_DRAWING_UTIL_H

#include <algorithm>
#include <cstdint>
#include <vector>

namespace termcore {
namespace detail {

/// Fill a rectangle in a grayscale bitmap. Coordinates are clamped.
inline void fill_rect(std::vector<uint8_t>& bmp, int bw, int bh,
                      int x0, int y0, int x1, int y1, uint8_t val) {
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(bw, x1);
    y1 = std::min(bh, y1);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            bmp[y * bw + x] = val;
}

} // namespace detail
} // namespace termcore

#endif
