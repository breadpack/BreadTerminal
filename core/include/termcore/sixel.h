#ifndef TERMCORE_SIXEL_H
#define TERMCORE_SIXEL_H
#include <cstdint>
#include <string>
#include <vector>
namespace termcore {
struct SixelImage {
    int width = 0, height = 0;
    std::vector<uint32_t> pixels; // RGBA row-major
    bool empty() const { return width == 0 || height == 0; }
};
SixelImage parseSixel(const std::string& data);
}
#endif
