#ifndef TERMCORE_DYNAMIC_COLORS_H
#define TERMCORE_DYNAMIC_COLORS_H

#include <cstdint>

namespace termcore {

/// Sentinel value: "use the default color" (not a real RGB).
constexpr uint32_t kColorDefault = 0xFFFFFFFF;

// Forward declaration
struct Config;

/// Dynamic (runtime-mutable) terminal color set.
///
/// palette[0..255]  -- xterm 256-color palette
/// foreground       -- OSC 10  default foreground
/// background       -- OSC 11  default background
/// cursor_color     -- OSC 12  cursor color
/// mouse_fg         -- OSC 13  mouse foreground
/// mouse_bg         -- OSC 14  mouse background
/// tek_fg           -- OSC 15  tektronix foreground
/// tek_bg           -- OSC 16  tektronix background
/// highlight_bg     -- OSC 17  highlight background
/// bold_color       -- OSC 18  (unused in many terminals)  -- not in xterm spec
/// italic_color     -- OSC 19  (unused in many terminals)
struct DynamicColors {
    uint32_t palette[256]{};
    uint32_t foreground   = 0xFFFFFF;
    uint32_t background   = 0x000000;
    uint32_t cursor_color = 0xFFFFFF;
    uint32_t mouse_fg     = 0x000000;
    uint32_t mouse_bg     = 0xFFFFFF;
    uint32_t tek_fg       = 0xFFFFFF;
    uint32_t tek_bg       = 0x000000;
    uint32_t highlight_bg = 0xFFFFFF;
    uint32_t bold_color   = 0xFFFFFF;
    uint32_t italic_color = 0xFFFFFF;

    /// Default constructor fills palette with xterm defaults.
    DynamicColors();

    /// Initialize from Config (palette[0-15] from cfg, 16-255 xterm defaults).
    void initFromConfig(const Config& cfg);

    /// Reset a specific dynamic color (index 0=fg, 1=bg, 2=cursor, ...) to xterm default.
    /// The index maps as: OSC (10+i) -> dynamicSlot(i).
    void resetDynamic(int slot);

    /// Reset a single palette entry [0..255] to the xterm default.
    void resetPaletteEntry(int idx);

    /// Reset entire palette to xterm defaults.
    void resetAllPalette();

    /// Resolve fg_color: if it's kColorDefault, return the dynamic foreground.
    inline uint32_t resolveFg(uint32_t c) const {
        return (c == kColorDefault) ? foreground : c;
    }

    /// Resolve bg_color: if it's kColorDefault, return the dynamic background.
    inline uint32_t resolveBg(uint32_t c) const {
        return (c == kColorDefault) ? background : c;
    }

private:
    /// Fill palette[0..255] with xterm defaults (independent of Config).
    void fillXtermDefaults();

    /// Get a pointer to the dynamic slot by index (0=fg, 1=bg, 2=cursor, ...).
    uint32_t* dynamicSlot(int slot);
    uint32_t  defaultDynamic(int slot) const;
};

} // namespace termcore

#endif // TERMCORE_DYNAMIC_COLORS_H
