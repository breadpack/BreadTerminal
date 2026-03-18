#include "termcore/font/box_drawing.h"
#include "box_drawing_util.h"
#include <algorithm>
#include <cmath>

using termcore::detail::fill_rect;

namespace termcore {

bool is_box_drawing(char32_t cp) {
    // Box Drawing: U+2500-U+257F
    if (cp >= 0x2500 && cp <= 0x257F) return true;
    // Block Elements: U+2580-U+259F
    if (cp >= 0x2580 && cp <= 0x259F) return true;
    // Braille Patterns: U+2800-U+28FF
    if (cp >= 0x2800 && cp <= 0x28FF) return true;
    // Powerline Glyphs: U+E0B0-U+E0B3
    if (cp >= 0xE0B0 && cp <= 0xE0B3) return true;
    return false;
}

// --- Block Elements (U+2580-U+259F) ---

static BoxGlyphBitmap render_block_element(char32_t cp, int w, int h) {
    BoxGlyphBitmap result;
    result.width = w;
    result.height = h;
    result.bitmap.assign(w * h, 0);

    auto fill = [&](int x0, int y0, int x1, int y1, uint8_t v = 255) {
        fill_rect(result.bitmap, w, h, x0, y0, x1, y1, v);
    };

    switch (cp) {
    case 0x2580: // ▀ upper half
        fill(0, 0, w, h / 2); break;
    case 0x2581: // ▁ lower 1/8
        fill(0, h * 7 / 8, w, h); break;
    case 0x2582: // ▂ lower 1/4
        fill(0, h * 3 / 4, w, h); break;
    case 0x2583: // ▃ lower 3/8
        fill(0, h * 5 / 8, w, h); break;
    case 0x2584: // ▄ lower half
        fill(0, h / 2, w, h); break;
    case 0x2585: // ▅ lower 5/8
        fill(0, h * 3 / 8, w, h); break;
    case 0x2586: // ▆ lower 3/4
        fill(0, h / 4, w, h); break;
    case 0x2587: // ▇ lower 7/8
        fill(0, h / 8, w, h); break;
    case 0x2588: // █ full block
        fill(0, 0, w, h); break;
    case 0x2589: // ▉ left 7/8
        fill(0, 0, w * 7 / 8, h); break;
    case 0x258A: // ▊ left 3/4
        fill(0, 0, w * 3 / 4, h); break;
    case 0x258B: // ▋ left 5/8
        fill(0, 0, w * 5 / 8, h); break;
    case 0x258C: // ▌ left half
        fill(0, 0, w / 2, h); break;
    case 0x258D: // ▍ left 3/8
        fill(0, 0, w * 3 / 8, h); break;
    case 0x258E: // ▎ left 1/4
        fill(0, 0, w / 4, h); break;
    case 0x258F: // ▏ left 1/8
        fill(0, 0, w / 8, h); break;
    case 0x2590: // ▐ right half
        fill(w / 2, 0, w, h); break;
    case 0x2591: // ░ light shade (25%)
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if ((x + y) % 4 == 0)
                    result.bitmap[y * w + x] = 255;
        break;
    case 0x2592: // ▒ medium shade (50%)
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if ((x + y) % 2 == 0)
                    result.bitmap[y * w + x] = 255;
        break;
    case 0x2593: // ▓ dark shade (75%)
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if ((x + y) % 4 != 0)
                    result.bitmap[y * w + x] = 255;
        break;
    case 0x2594: // ▔ upper 1/8
        fill(0, 0, w, h / 8); break;
    case 0x2595: // ▕ right 1/8
        fill(w * 7 / 8, 0, w, h); break;
    // Quadrants U+2596-U+259F
    case 0x2596: // ▖ lower left
        fill(0, h / 2, w / 2, h); break;
    case 0x2597: // ▗ lower right
        fill(w / 2, h / 2, w, h); break;
    case 0x2598: // ▘ upper left
        fill(0, 0, w / 2, h / 2); break;
    case 0x2599: // ▙ upper left + lower left + lower right
        fill(0, 0, w / 2, h / 2);
        fill(0, h / 2, w, h);
        break;
    case 0x259A: // ▚ upper left + lower right
        fill(0, 0, w / 2, h / 2);
        fill(w / 2, h / 2, w, h);
        break;
    case 0x259B: // ▛ upper left + upper right + lower left
        fill(0, 0, w, h / 2);
        fill(0, h / 2, w / 2, h);
        break;
    case 0x259C: // ▜ upper left + upper right + lower right
        fill(0, 0, w, h / 2);
        fill(w / 2, h / 2, w, h);
        break;
    case 0x259D: // ▝ upper right
        fill(w / 2, 0, w, h / 2); break;
    case 0x259E: // ▞ upper right + lower left
        fill(w / 2, 0, w, h / 2);
        fill(0, h / 2, w / 2, h);
        break;
    case 0x259F: // ▟ upper right + lower left + lower right
        fill(w / 2, 0, w, h / 2);
        fill(0, h / 2, w, h);
        break;
    default:
        break;
    }

    return result;
}

// --- Braille Patterns (U+2800-U+28FF) ---

static BoxGlyphBitmap render_braille(char32_t cp, int w, int h) {
    BoxGlyphBitmap result;
    result.width = w;
    result.height = h;
    result.bitmap.assign(w * h, 0);

    uint8_t pattern = static_cast<uint8_t>(cp - 0x2800);

    // Braille dot layout in a 2x4 grid:
    // Left col:  dot1(bit0), dot2(bit1), dot3(bit2), dot7(bit6)
    // Right col: dot4(bit3), dot5(bit4), dot6(bit5), dot8(bit7)
    // Map bits to (col, row):
    static const int dot_col[8] = {0, 0, 0, 1, 1, 1, 0, 1};
    static const int dot_row[8] = {0, 1, 2, 0, 1, 2, 3, 3};

    // Dot dimensions
    int dot_w = std::max(1, w / 5);
    int dot_h = std::max(1, h / 9);
    int margin_x = w / 5;
    int margin_y = h / 9;
    int spacing_x = (w - 2 * margin_x - dot_w);
    int spacing_y = (h - 2 * margin_y - dot_h) / 3;

    for (int bit = 0; bit < 8; ++bit) {
        if (pattern & (1 << bit)) {
            int col = dot_col[bit];
            int row = dot_row[bit];
            int dx = margin_x + col * spacing_x;
            int dy = margin_y + row * spacing_y;
            fill_rect(result.bitmap, w, h, dx, dy, dx + dot_w, dy + dot_h, 255);
        }
    }

    return result;
}

// --- Powerline Glyphs (U+E0B0-U+E0B3) ---

static BoxGlyphBitmap render_powerline(char32_t cp, int w, int h) {
    BoxGlyphBitmap result;
    result.width = w;
    result.height = h;
    result.bitmap.assign(w * h, 0);

    bool filled = (cp == 0xE0B0 || cp == 0xE0B2);
    bool right_pointing = (cp == 0xE0B0 || cp == 0xE0B1);

    for (int y = 0; y < h; ++y) {
        // Triangle scanline
        float progress = static_cast<float>(y) / static_cast<float>(h);
        int edge_x;
        if (progress <= 0.5f)
            edge_x = static_cast<int>(progress * 2.0f * w);
        else
            edge_x = static_cast<int>((1.0f - progress) * 2.0f * w);

        if (right_pointing) {
            if (filled) {
                for (int x = 0; x < edge_x && x < w; ++x)
                    result.bitmap[y * w + x] = 255;
            } else {
                // outline only
                if (edge_x > 0 && edge_x <= w)
                    result.bitmap[y * w + std::min(edge_x - 1, w - 1)] = 255;
            }
        } else {
            // left-pointing
            if (filled) {
                for (int x = w - edge_x; x < w; ++x)
                    if (x >= 0)
                        result.bitmap[y * w + x] = 255;
            } else {
                int ox = w - edge_x;
                if (ox >= 0 && ox < w)
                    result.bitmap[y * w + ox] = 255;
            }
        }
    }

    return result;
}

// --- Main dispatch ---

BoxGlyphBitmap render_box_glyph(char32_t cp, int cell_width, int cell_height, int thickness) {
    if (cell_width <= 0 || cell_height <= 0)
        return {{}, 0, 0};

    // Box Drawing Lines: U+2500-U+257F
    if (cp >= 0x2500 && cp <= 0x257F)
        return render_box_lines(cp, cell_width, cell_height, thickness);

    // Block Elements: U+2580-U+259F
    if (cp >= 0x2580 && cp <= 0x259F)
        return render_block_element(cp, cell_width, cell_height);

    // Braille Patterns: U+2800-U+28FF
    if (cp >= 0x2800 && cp <= 0x28FF)
        return render_braille(cp, cell_width, cell_height);

    // Powerline Glyphs: U+E0B0-U+E0B3
    if (cp >= 0xE0B0 && cp <= 0xE0B3)
        return render_powerline(cp, cell_width, cell_height);

    return {{}, 0, 0};
}

} // namespace termcore
