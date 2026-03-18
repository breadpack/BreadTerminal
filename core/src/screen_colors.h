#ifndef TERMCORE_SCREEN_COLORS_H
#define TERMCORE_SCREEN_COLORS_H

#include <cstdint>

namespace termcore {

// Standard 8-color + bright 8-color palette
static constexpr uint32_t kColorTable[16] = {
    0x000000, 0xAA0000, 0x00AA00, 0xAA5500,
    0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
    0x555555, 0xFF5555, 0x55FF55, 0xFFFF55,
    0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF,
};

// xterm 6x6x6 color cube mapping (indexes 0-5 -> RGB values)
static constexpr uint8_t kCubeValues[6] = { 0, 95, 135, 175, 215, 255 };

} // namespace termcore

#endif // TERMCORE_SCREEN_COLORS_H
