#include "termcore/dynamic_colors.h"
#include "termcore/config.h"

namespace termcore {

// xterm 6x6x6 color cube mapping (indexes 0-5 -> RGB values)
static constexpr uint8_t kCubeValues[6] = { 0x00, 0x5f, 0x87, 0xaf, 0xd7, 0xff };

// Standard xterm 16-color palette
static constexpr uint32_t kXtermBase16[16] = {
    0x000000, 0xAA0000, 0x00AA00, 0xAA5500,
    0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
    0x555555, 0xFF5555, 0x55FF55, 0xFFFF55,
    0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF,
};

static uint32_t cubeColor(int idx) {
    int c = idx - 16;
    int r = c / 36;
    int g = (c / 6) % 6;
    int b = c % 6;
    return (static_cast<uint32_t>(kCubeValues[r]) << 16) |
           (static_cast<uint32_t>(kCubeValues[g]) << 8)  |
            static_cast<uint32_t>(kCubeValues[b]);
}

static uint32_t grayscaleColor(int idx) {
    int gray = 8 + (idx - 232) * 10;
    return (static_cast<uint32_t>(gray) << 16) |
           (static_cast<uint32_t>(gray) << 8)  |
            static_cast<uint32_t>(gray);
}

DynamicColors::DynamicColors() {
    fillXtermDefaults();
}

void DynamicColors::fillXtermDefaults() {
    for (int i = 0; i < 16; ++i)
        palette[i] = kXtermBase16[i];
    for (int i = 16; i < 232; ++i)
        palette[i] = cubeColor(i);
    for (int i = 232; i < 256; ++i)
        palette[i] = grayscaleColor(i);
}

void DynamicColors::initFromConfig(const Config& cfg) {
    fillXtermDefaults();
    // Override palette[0..15] from config
    for (int i = 0; i < 16; ++i)
        palette[i] = cfg.palette[i];
    foreground   = cfg.foreground;
    background   = cfg.background;
    cursor_color = cfg.cursor_color;
}

uint32_t* DynamicColors::dynamicSlot(int slot) {
    switch (slot) {
    case 0:  return &foreground;
    case 1:  return &background;
    case 2:  return &cursor_color;
    case 3:  return &mouse_fg;
    case 4:  return &mouse_bg;
    case 5:  return &tek_fg;
    case 6:  return &tek_bg;
    case 7:  return &highlight_bg;
    case 8:  return &bold_color;
    case 9:  return &italic_color;
    default: return nullptr;
    }
}

uint32_t DynamicColors::defaultDynamic(int slot) const {
    switch (slot) {
    case 0:  return 0xFFFFFF;  // foreground
    case 1:  return 0x000000;  // background
    case 2:  return 0xFFFFFF;  // cursor
    case 3:  return 0x000000;  // mouse fg
    case 4:  return 0xFFFFFF;  // mouse bg
    case 5:  return 0xFFFFFF;  // tek fg
    case 6:  return 0x000000;  // tek bg
    case 7:  return 0xFFFFFF;  // highlight bg
    case 8:  return 0xFFFFFF;  // bold color
    case 9:  return 0xFFFFFF;  // italic color
    default: return 0xFFFFFF;
    }
}

void DynamicColors::resetDynamic(int slot) {
    uint32_t* p = dynamicSlot(slot);
    if (p) *p = defaultDynamic(slot);
}

void DynamicColors::resetPaletteEntry(int idx) {
    if (idx < 0 || idx > 255) return;
    if (idx < 16)
        palette[idx] = kXtermBase16[idx];
    else if (idx < 232)
        palette[idx] = cubeColor(idx);
    else
        palette[idx] = grayscaleColor(idx);
}

void DynamicColors::resetAllPalette() {
    fillXtermDefaults();
}

} // namespace termcore
