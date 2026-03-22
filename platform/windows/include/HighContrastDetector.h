#ifndef BREAD_HIGH_CONTRAST_DETECTOR_H
#define BREAD_HIGH_CONTRAST_DETECTOR_H

#ifdef _WIN32

#include "termcore/accessibility.h"
#include "termcore/config.h"

#include <cstdint>

namespace termcore {

/// System colors read from the Windows high contrast palette.
struct HighContrastColors {
    uint32_t window = 0x000000;          // COLOR_WINDOW (background)
    uint32_t window_text = 0xffffff;     // COLOR_WINDOWTEXT (foreground)
    uint32_t highlight = 0x000080;       // COLOR_HIGHLIGHT (selection bg)
    uint32_t highlight_text = 0xffffff;  // COLOR_HIGHLIGHTTEXT (selection fg)
    uint32_t gray_text = 0x808080;       // COLOR_GRAYTEXT (dimmed)
};

/// Detects Windows high contrast mode and reduced motion preference.
/// All methods are static; no instance needed.
class HighContrastDetector {
public:
    /// Check whether the OS high contrast mode is currently enabled.
    static bool isHighContrastEnabled();

    /// Read the current system high contrast colors.
    static HighContrastColors getSystemColors();

    /// Build a terminal Theme from the given OS high contrast colors.
    static Theme buildThemeFromSystemColors(const HighContrastColors& colors);

    /// Check whether the OS "reduce animations" / client-area-animation
    /// setting is disabled (i.e. user prefers reduced motion).
    static bool isReducedMotionEnabled();
};

} // namespace termcore

#endif // _WIN32
#endif // BREAD_HIGH_CONTRAST_DETECTOR_H
