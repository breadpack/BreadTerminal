#ifdef _WIN32

#include "HighContrastDetector.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace termcore {

namespace {

/// Convert a COLORREF (0x00BBGGRR) to our uint32_t format (0x00RRGGBB).
uint32_t colorrefToRGB(COLORREF cr) {
    uint32_t r = GetRValue(cr);
    uint32_t g = GetGValue(cr);
    uint32_t b = GetBValue(cr);
    return (r << 16) | (g << 8) | b;
}

/// Blend two colors by a factor t (0.0 = a, 1.0 = b).
uint32_t blendColor(uint32_t a, uint32_t b, float t) {
    auto mix = [&](int shift) -> uint32_t {
        float ca = static_cast<float>((a >> shift) & 0xff);
        float cb = static_cast<float>((b >> shift) & 0xff);
        float result = ca + (cb - ca) * t;
        if (result < 0.0f) result = 0.0f;
        if (result > 255.0f) result = 255.0f;
        return static_cast<uint32_t>(result);
    };
    return (mix(16) << 16) | (mix(8) << 8) | mix(0);
}

} // namespace

bool HighContrastDetector::isHighContrastEnabled() {
    HIGHCONTRASTW hc = {};
    hc.cbSize = sizeof(hc);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0)) {
        return (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
    }
    return false;
}

HighContrastColors HighContrastDetector::getSystemColors() {
    HighContrastColors colors;
    colors.window = colorrefToRGB(GetSysColor(COLOR_WINDOW));
    colors.window_text = colorrefToRGB(GetSysColor(COLOR_WINDOWTEXT));
    colors.highlight = colorrefToRGB(GetSysColor(COLOR_HIGHLIGHT));
    colors.highlight_text = colorrefToRGB(GetSysColor(COLOR_HIGHLIGHTTEXT));
    colors.gray_text = colorrefToRGB(GetSysColor(COLOR_GRAYTEXT));
    return colors;
}

Theme HighContrastDetector::buildThemeFromSystemColors(
    const HighContrastColors& colors) {
    Theme theme;
    theme.name = "System High Contrast";
    theme.background = colors.window;
    theme.foreground = colors.window_text;
    theme.cursor_color = colors.window_text;
    theme.selection_background = colors.highlight;
    theme.selection_foreground = colors.highlight_text;

    // Build a 16-color palette from the limited HC colors.
    // Normal colors (0-7): mix foreground with slight variations
    uint32_t fg = colors.window_text;
    uint32_t bg = colors.window;
    uint32_t dim = colors.gray_text;
    uint32_t hi = colors.highlight;
    uint32_t hiText = colors.highlight_text;

    // Provide distinguishable colors while staying within the HC palette.
    // ANSI black
    theme.palette[0] = bg;
    // ANSI red — blend highlight toward foreground
    theme.palette[1] = blendColor(hi, fg, 0.3f);
    // ANSI green — foreground
    theme.palette[2] = fg;
    // ANSI yellow — blend foreground toward highlight text
    theme.palette[3] = blendColor(fg, hiText, 0.5f);
    // ANSI blue — highlight
    theme.palette[4] = hi;
    // ANSI magenta — blend highlight toward highlight text
    theme.palette[5] = blendColor(hi, hiText, 0.5f);
    // ANSI cyan — blend foreground toward highlight
    theme.palette[6] = blendColor(fg, hi, 0.5f);
    // ANSI white — foreground
    theme.palette[7] = fg;

    // Bright colors (8-15): slightly brighter / more saturated variants
    theme.palette[8] = dim;
    theme.palette[9] = blendColor(hi, fg, 0.5f);
    theme.palette[10] = blendColor(fg, hiText, 0.3f);
    theme.palette[11] = hiText;
    theme.palette[12] = blendColor(hi, hiText, 0.3f);
    theme.palette[13] = blendColor(hi, fg, 0.7f);
    theme.palette[14] = blendColor(fg, hi, 0.3f);
    theme.palette[15] = hiText;

    return theme;
}

bool HighContrastDetector::isReducedMotionEnabled() {
    BOOL animationsEnabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0,
                              &animationsEnabled, 0)) {
        return !animationsEnabled;
    }
    // If we can't query, assume animations are enabled (not reduced).
    return false;
}

} // namespace termcore

#endif // _WIN32
