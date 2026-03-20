#pragma once
#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cstdint>

namespace termcore {

/// UI chrome colors derived from the active terminal theme.
/// Used by popup windows (Theme Hub, Font Hub, Settings) and the tab bar.
struct ChromeColors {
    COLORREF background;       // main window background
    COLORREF titleBar;         // title bar (darker)
    COLORREF cardBg;           // card / panel background
    COLORREF fieldBg;          // input field / search background
    COLORREF accent;           // accent (buttons, highlights)
    COLORREF textColor;        // primary text
    COLORREF dimText;          // secondary / muted text
    COLORREF btnInactive;      // inactive button background
    COLORREF previewBg;        // font preview background (slightly darker)
    COLORREF activeGreen;      // "Active" badge / installed indicator
    COLORREF badgeSecondary;   // secondary badge (purple-ish)
};

/// Helper: clamp a byte
inline BYTE clampByte(int v) {
    return static_cast<BYTE>((std::min)(255, (std::max)(0, v)));
}

/// Darken a COLORREF by a factor (0.0 = black, 1.0 = unchanged)
inline COLORREF darkenCR(COLORREF c, float factor) {
    return RGB(
        clampByte(static_cast<int>(GetRValue(c) * factor)),
        clampByte(static_cast<int>(GetGValue(c) * factor)),
        clampByte(static_cast<int>(GetBValue(c) * factor)));
}

/// Lighten a COLORREF toward white by a factor (1.0 = unchanged, 2.0 = much lighter)
inline COLORREF lightenCR(COLORREF c, float factor) {
    int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
    return RGB(
        clampByte(r + static_cast<int>((255 - r) * (factor - 1.0f))),
        clampByte(g + static_cast<int>((255 - g) * (factor - 1.0f))),
        clampByte(b + static_cast<int>((255 - b) * (factor - 1.0f))));
}

/// Blend two COLORREFs (0.0 = a, 1.0 = b)
inline COLORREF blendCR(COLORREF a, COLORREF b, float t) {
    return RGB(
        clampByte(static_cast<int>(GetRValue(a) * (1.f - t) + GetRValue(b) * t)),
        clampByte(static_cast<int>(GetGValue(a) * (1.f - t) + GetGValue(b) * t)),
        clampByte(static_cast<int>(GetBValue(a) * (1.f - t) + GetBValue(b) * t)));
}

/// Convert a 0xRRGGBB uint32 to COLORREF (0x00BBGGRR)
inline COLORREF rgbToCR(uint32_t rgb) {
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

/// Convert COLORREF to 0xRRGGBB uint32
inline uint32_t crToRgb(COLORREF cr) {
    return (static_cast<uint32_t>(GetRValue(cr)) << 16)
         | (static_cast<uint32_t>(GetGValue(cr)) << 8)
         |  static_cast<uint32_t>(GetBValue(cr));
}

/// Derive UI chrome colors from the terminal theme.
/// bg/fg are 0xRRGGBB, palette is the 16-color ANSI palette (0xRRGGBB each).
inline ChromeColors deriveChrome(uint32_t bg, uint32_t fg,
                                  const uint32_t palette[16]) {
    ChromeColors c;
    COLORREF bgCR = rgbToCR(bg);
    COLORREF fgCR = rgbToCR(fg);

    c.background   = bgCR;
    c.titleBar     = darkenCR(bgCR, 0.72f);
    c.cardBg       = lightenCR(bgCR, 1.08f);
    c.fieldBg      = lightenCR(bgCR, 1.15f);
    c.btnInactive  = lightenCR(bgCR, 1.30f);
    c.textColor    = fgCR;
    c.dimText      = blendCR(fgCR, bgCR, 0.50f);
    c.previewBg    = darkenCR(bgCR, 0.85f);

    // Accent: use bright blue from palette (index 12), fallback to palette[4]
    c.accent = palette ? rgbToCR(palette[12]) : RGB(122, 162, 247);

    // Active green: use bright green from palette (index 10), fallback
    c.activeGreen = palette ? rgbToCR(palette[10]) : RGB(46, 160, 67);

    // Badge secondary: use bright magenta (index 13)
    c.badgeSecondary = palette ? rgbToCR(palette[13]) : RGB(180, 142, 173);

    return c;
}

} // namespace termcore

#endif
