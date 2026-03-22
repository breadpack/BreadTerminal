#ifndef TERMCORE_SUBPIXEL_H
#define TERMCORE_SUBPIXEL_H

#include <cstdint>
#include <string>

namespace termcore {

/// Sub-pixel rendering mode
enum class SubpixelMode {
    None,       // Grayscale antialiasing only
    RGB,        // Horizontal RGB (most common LCD layout)
    BGR,        // Horizontal BGR
    VRGB,       // Vertical RGB
    VBGR,       // Vertical BGR
    Auto,       // Detect from system settings
};

/// Sub-pixel hinting strength
enum class HintingMode {
    None,       // No hinting
    Slight,     // Minimal hinting (FreeType autohinter light)
    Medium,     // Medium hinting
    Full,       // Full hinting (sharpest, may distort)
    Auto,       // Use system default
};

/// Convert subpixel mode to/from string
std::string subpixelModeToString(SubpixelMode mode);
SubpixelMode subpixelModeFromString(const std::string& s);

/// Convert hinting mode to/from string
std::string hintingModeToString(HintingMode mode);
HintingMode hintingModeFromString(const std::string& s);

/// Detect system sub-pixel layout
/// Windows: Uses ClearType registry (PixelStructure: 1=RGB, 2=BGR)
/// Linux: Reads fontconfig Xft.rgba / Xft.hintstyle
/// macOS: Always returns None (macOS uses its own AA)
SubpixelMode detectSystemSubpixel();

/// Detect system hinting preference
HintingMode detectSystemHinting();

} // namespace termcore
#endif
