#include "termcore/contrast.h"
#include <algorithm>
#include <cmath>

namespace termcore {

/// Linearize a single sRGB channel value (0.0-1.0) to linear RGB.
static float linearize(float val) {
    if (val <= 0.04045f) {
        return val / 12.92f;
    }
    return std::pow((val + 0.055f) / 1.055f, 2.4f);
}

/// Convert linear RGB back to sRGB (0.0-1.0).
static float toSrgb(float val) {
    if (val <= 0.0031308f) {
        return val * 12.92f;
    }
    return 1.055f * std::pow(val, 1.0f / 2.4f) - 0.055f;
}

float relativeLuminance(uint32_t rgb) {
    float r = linearize(((rgb >> 16) & 0xFF) / 255.0f);
    float g = linearize(((rgb >> 8) & 0xFF) / 255.0f);
    float b = linearize((rgb & 0xFF) / 255.0f);
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

float contrastRatio(uint32_t fg_rgb, uint32_t bg_rgb) {
    float l1 = relativeLuminance(fg_rgb);
    float l2 = relativeLuminance(bg_rgb);
    if (l1 < l2) std::swap(l1, l2);
    return (l1 + 0.05f) / (l2 + 0.05f);
}

uint32_t ensureContrast(uint32_t fg_rgb, uint32_t bg_rgb, float min_ratio) {
    if (contrastRatio(fg_rgb, bg_rgb) >= min_ratio) {
        return fg_rgb;
    }

    float bg_lum = relativeLuminance(bg_rgb);

    // Extract sRGB components and linearize
    float r = linearize(((fg_rgb >> 16) & 0xFF) / 255.0f);
    float g = linearize(((fg_rgb >> 8) & 0xFF) / 255.0f);
    float b = linearize((fg_rgb & 0xFF) / 255.0f);
    float fg_lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;

    // Decide whether to lighten or darken the foreground.
    // If bg is dark (luminance < 0.5), try lightening first;
    // otherwise try darkening first.
    bool lighten = (bg_lum < 0.5f);

    // Binary search for the right adjustment factor.
    // We scale fg toward white (factor -> 1.0 means white) or
    // toward black (factor -> 1.0 means black).
    auto adjust = [&](float factor, bool to_light) -> uint32_t {
        float nr, ng, nb;
        if (to_light) {
            // Blend linear RGB toward 1.0 (white)
            nr = r + (1.0f - r) * factor;
            ng = g + (1.0f - g) * factor;
            nb = b + (1.0f - b) * factor;
        } else {
            // Blend linear RGB toward 0.0 (black)
            nr = r * (1.0f - factor);
            ng = g * (1.0f - factor);
            nb = b * (1.0f - factor);
        }
        int ri = std::clamp(static_cast<int>(toSrgb(nr) * 255.0f + 0.5f), 0, 255);
        int gi = std::clamp(static_cast<int>(toSrgb(ng) * 255.0f + 0.5f), 0, 255);
        int bi = std::clamp(static_cast<int>(toSrgb(nb) * 255.0f + 0.5f), 0, 255);
        return static_cast<uint32_t>((ri << 16) | (gi << 8) | bi);
    };

    // Try the preferred direction first
    for (int pass = 0; pass < 2; ++pass) {
        bool direction = (pass == 0) ? lighten : !lighten;
        float lo = 0.0f, hi = 1.0f;

        // Check if max adjustment in this direction meets the ratio
        uint32_t max_adjusted = adjust(1.0f, direction);
        if (contrastRatio(max_adjusted, bg_rgb) < min_ratio) {
            // This direction can't meet the requirement; try the other
            continue;
        }

        // Binary search
        for (int iter = 0; iter < 32; ++iter) {
            float mid = (lo + hi) * 0.5f;
            uint32_t candidate = adjust(mid, direction);
            if (contrastRatio(candidate, bg_rgb) >= min_ratio) {
                hi = mid;
            } else {
                lo = mid;
            }
        }

        return adjust(hi, direction);
    }

    // Fallback: return black or white, whichever has better contrast
    float white_ratio = contrastRatio(0xFFFFFF, bg_rgb);
    float black_ratio = contrastRatio(0x000000, bg_rgb);
    return (white_ratio >= black_ratio) ? 0xFFFFFFu : 0x000000u;
}

} // namespace termcore
