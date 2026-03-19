#pragma once
#include <cstdint>

namespace termcore {

/// Calculate relative luminance of an RGB color (0.0 to 1.0)
float relativeLuminance(uint32_t rgb);

/// Calculate WCAG contrast ratio between two colors (1.0 to 21.0)
float contrastRatio(uint32_t fg_rgb, uint32_t bg_rgb);

/// Adjust foreground color to meet minimum contrast ratio against background.
/// Returns adjusted foreground color, or original if already meets minimum.
uint32_t ensureContrast(uint32_t fg_rgb, uint32_t bg_rgb, float min_ratio = 4.5f);

} // namespace termcore
