#include "termcore/font/box_drawing.h"
#include "box_drawing_util.h"
#include <algorithm>
#include <cmath>
#include <cstring>

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

bool is_powerline_extended(char32_t cp) {
    // All Powerline variants: original + round + extra
    return (cp >= 0xE0B0 && cp <= 0xE0BF);
}

bool is_nerd_font_icon(char32_t cp) {
    // Seti-UI + Custom
    if (cp >= 0xE200 && cp <= 0xE2A9) return true;
    // Custom (devicons)
    if (cp >= 0xE5FA && cp <= 0xE6B5) return true;
    // Dev Icons
    if (cp >= 0xE700 && cp <= 0xE7C5) return true;
    // Codicons
    if (cp >= 0xEA60 && cp <= 0xEC1E) return true;
    // Various Nerd Font ranges
    if (cp >= 0xED00 && cp <= 0xF2FF) return true;
    // Font Awesome
    if (cp >= 0xF000 && cp <= 0xF2E0) return true;
    // Font Logos
    if (cp >= 0xF300 && cp <= 0xF375) return true;
    // Octicons
    if (cp >= 0xF400 && cp <= 0xF532) return true;
    // Material Design
    if (cp >= 0xF500 && cp <= 0xFD46) return true;
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
        std::memset(result.bitmap.data(), 64, w * h);
        break;
    case 0x2592: // ▒ medium shade (50%)
        std::memset(result.bitmap.data(), 128, w * h);
        break;
    case 0x2593: // ▓ dark shade (75%)
        std::memset(result.bitmap.data(), 191, w * h);
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
            // Draw anti-aliased circle for each braille dot
            float cx = dx + dot_w / 2.0f;
            float cy = dy + dot_h / 2.0f;
            float r = std::min(dot_w, dot_h) / 2.0f;
            int x0 = std::max(0, dx);
            int y0 = std::max(0, dy);
            int x1 = std::min(w, dx + dot_w);
            int y1 = std::min(h, dy + dot_h);
            for (int py = y0; py < y1; ++py) {
                for (int px = x0; px < x1; ++px) {
                    float dist = std::sqrt((px + 0.5f - cx) * (px + 0.5f - cx) +
                                           (py + 0.5f - cy) * (py + 0.5f - cy));
                    if (dist <= r - 0.5f) {
                        result.bitmap[py * w + px] = 255;
                    } else if (dist <= r + 0.5f) {
                        uint8_t alpha = static_cast<uint8_t>((r + 0.5f - dist) * 255.0f);
                        result.bitmap[py * w + px] = std::max(result.bitmap[py * w + px], alpha);
                    }
                }
            }
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

    // Triangle vertices
    // Right-pointing: apex at (w, h/2), top-left (0,0), bottom-left (0,h)
    // Left-pointing:  apex at (0, h/2), top-right (w,0), bottom-right (w,h)
    double apex_x, apex_y, top_x, top_y, bot_x, bot_y;
    if (right_pointing) {
        apex_x = w; apex_y = h / 2.0;
        top_x = 0;  top_y = 0;
        bot_x = 0;  bot_y = h;
    } else {
        apex_x = 0; apex_y = h / 2.0;
        top_x = w;  top_y = 0;
        bot_x = w;  bot_y = h;
    }

    // Perpendicular distance from point to infinite line through (ax,ay)-(bx,by)
    auto dist_to_line = [](double px, double py,
                           double ax, double ay, double bx, double by) -> double {
        double dx = bx - ax, dy = by - ay;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-9) return std::sqrt((px - ax) * (px - ax) + (py - ay) * (py - ay));
        return std::abs((px - ax) * (-dy / len) + (py - ay) * (dx / len));
    };

    // Signed distance: positive = left of directed line a->b
    auto signed_dist = [](double px, double py,
                          double ax, double ay, double bx, double by) -> double {
        double dx = bx - ax, dy = by - ay;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-9) return 0.0;
        return (px - ax) * (-dy / len) + (py - ay) * (dx / len);
    };

    double half_thickness = std::max(1.0, static_cast<double>(std::max(1, w / 10))) * 0.75;

    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            double x = px + 0.5;
            double y = py + 0.5;

            double d_top = dist_to_line(x, y, top_x, top_y, apex_x, apex_y);
            double d_bot = dist_to_line(x, y, apex_x, apex_y, bot_x, bot_y);

            if (filled) {
                // Signed distances to determine inside/outside
                double s_top = signed_dist(x, y, top_x, top_y, apex_x, apex_y);
                double s_bot = signed_dist(x, y, apex_x, apex_y, bot_x, bot_y);

                // Determine inside direction based on triangle orientation
                double inside_top, inside_bot;
                if (right_pointing) {
                    inside_top = s_top;
                    inside_bot = s_bot;
                } else {
                    inside_top = -s_top;
                    inside_bot = -s_bot;
                }

                // Anti-aliased coverage with smooth step at edges
                double alpha_top = std::clamp(inside_top + 0.5, 0.0, 1.0);
                double alpha_bot = std::clamp(inside_bot + 0.5, 0.0, 1.0);
                double alpha = alpha_top * alpha_bot * 255.0;

                if (alpha > 0.5) {
                    uint8_t val = static_cast<uint8_t>(std::min(255.0, alpha));
                    result.bitmap[py * w + px] = std::max(result.bitmap[py * w + px], val);
                }
            } else {
                // Outline: anti-aliased lines along both diagonal edges
                double alpha_top = 255.0 * std::max(0.0, std::min(1.0, half_thickness - d_top + 0.5));
                double alpha_bot = 255.0 * std::max(0.0, std::min(1.0, half_thickness - d_bot + 0.5));
                double alpha = std::max(alpha_top, alpha_bot);

                if (alpha > 0.5) {
                    uint8_t val = static_cast<uint8_t>(std::min(255.0, alpha));
                    result.bitmap[py * w + px] = std::max(result.bitmap[py * w + px], val);
                }
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
