#define _USE_MATH_DEFINES
#include <cmath>
#include "termcore/font/box_drawing.h"
#include "box_drawing_util.h"
#include <algorithm>

using termcore::detail::fill_rect;

namespace termcore {

// Edge style: 0=none, 1=light, 2=heavy, 3=double
struct BoxEdges {
    uint8_t right;
    uint8_t up;
    uint8_t left;
    uint8_t down;
};

// Lookup table for U+2500 to U+257F (128 entries)
// Each entry: {right, up, left, down}
static const BoxEdges box_edges_table[128] = {
    // 2500-250F
    {1,0,1,0}, // ─  2500
    {2,0,2,0}, // ━  2501
    {0,1,0,1}, // │  2502
    {0,2,0,2}, // ┃  2503
    {1,0,1,0}, // ┄  2504 (triple dash horizontal light)
    {2,0,2,0}, // ┅  2505 (triple dash horizontal heavy)
    {0,1,0,1}, // ┆  2506 (triple dash vertical light)
    {0,2,0,2}, // ┇  2507 (triple dash vertical heavy)
    {1,0,1,0}, // ┈  2508 (quadruple dash horizontal light)
    {2,0,2,0}, // ┉  2509 (quadruple dash horizontal heavy)
    {0,1,0,1}, // ┊  250A (quadruple dash vertical light)
    {0,2,0,2}, // ┋  250B (quadruple dash vertical heavy)
    {1,0,0,1}, // ┌  250C
    {2,0,0,1}, // ┍  250D
    {1,0,0,2}, // ┎  250E
    {2,0,0,2}, // ┏  250F

    // 2510-251F
    {0,0,1,1}, // ┐  2510
    {0,0,2,1}, // ┑  2511
    {0,0,1,2}, // ┒  2512
    {0,0,2,2}, // ┓  2513
    {1,1,0,0}, // └  2514
    {2,1,0,0}, // ┕  2515
    {1,2,0,0}, // ┖  2516
    {2,2,0,0}, // ┗  2517
    {0,1,1,0}, // ┘  2518
    {0,1,2,0}, // ┙  2519
    {0,2,1,0}, // ┚  251A
    {0,2,2,0}, // ┛  251B
    {1,1,0,1}, // ├  251C
    {2,1,0,1}, // ┝  251D
    {1,2,0,1}, // ┞  251E
    {1,1,0,2}, // ┟  251F

    // 2520-252F
    {1,2,0,2}, // ┠  2520
    {2,2,0,1}, // ┡  2521
    {2,1,0,2}, // ┢  2522
    {2,2,0,2}, // ┣  2523
    {0,1,1,1}, // ┤  2524
    {0,1,2,1}, // ┥  2525
    {0,2,1,1}, // ┦  2526
    {0,1,1,2}, // ┧  2527
    {0,2,1,2}, // ┨  2528
    {0,2,2,1}, // ┩  2529
    {0,1,2,2}, // ┪  252A
    {0,2,2,2}, // ┫  252B
    {1,0,1,1}, // ┬  252C
    {2,0,1,1}, // ┭  252D
    {1,0,2,1}, // ┮  252E
    {2,0,2,1}, // ┯  252F

    // 2530-253F
    {1,0,1,2}, // ┰  2530
    {2,0,1,2}, // ┱  2531
    {1,0,2,2}, // ┲  2532
    {2,0,2,2}, // ┳  2533
    {1,1,1,0}, // ┴  2534
    {2,1,1,0}, // ┵  2535
    {1,1,2,0}, // ┶  2536
    {2,1,2,0}, // ┷  2537
    {1,2,1,0}, // ┸  2538
    {2,2,1,0}, // ┹  2539
    {1,2,2,0}, // ┺  253A
    {2,2,2,0}, // ┻  253B
    {1,1,1,1}, // ┼  253C
    {2,1,1,1}, // ┽  253D
    {1,1,2,1}, // ┾  253E
    {2,1,2,1}, // ┿  253F

    // 2540-254F
    {1,2,1,1}, // ╀  2540
    {1,1,1,2}, // ╁  2541
    {1,2,1,2}, // ╂  2542
    {2,2,1,1}, // ╃  2543
    {1,2,2,1}, // ╄  2544
    {2,1,1,2}, // ╅  2545
    {1,1,2,2}, // ╆  2546
    {2,2,2,1}, // ╇  2547
    {2,1,2,2}, // ╈  2548
    {1,2,2,2}, // ╉  2549
    {2,2,1,2}, // ╊  254A
    {2,2,2,2}, // ╋  254B
    {1,0,1,0}, // ╌  254C (double dash horizontal light)
    {2,0,2,0}, // ╍  254D (double dash horizontal heavy)
    {0,1,0,1}, // ╎  254E (double dash vertical light)
    {0,2,0,2}, // ╏  254F (double dash vertical heavy)

    // 2550-255F
    {3,0,3,0}, // ═  2550
    {0,3,0,3}, // ║  2551
    {3,0,0,1}, // ╒  2552
    {1,0,0,3}, // ╓  2553
    {3,0,0,3}, // ╔  2554
    {0,0,3,1}, // ╕  2555
    {0,0,1,3}, // ╖  2556
    {0,0,3,3}, // ╗  2557
    {3,1,0,0}, // ╘  2558
    {1,3,0,0}, // ╙  2559
    {3,3,0,0}, // ╚  255A
    {0,1,3,0}, // ╛  255B
    {0,3,1,0}, // ╜  255C
    {0,3,3,0}, // ╝  255D
    {3,1,0,1}, // ╞  255E
    {1,3,0,3}, // ╟  255F

    // 2560-256F
    {3,3,0,3}, // ╠  2560
    {0,1,3,1}, // ╡  2561
    {0,3,1,3}, // ╢  2562
    {0,3,3,3}, // ╣  2563
    {3,0,3,1}, // ╤  2564
    {1,0,1,3}, // ╥  2565
    {3,0,3,3}, // ╦  2566
    {3,1,3,0}, // ╧  2567
    {1,3,1,0}, // ╨  2568
    {3,3,3,0}, // ╩  2569
    {3,1,3,1}, // ╪  256A
    {1,3,1,3}, // ╫  256B
    {3,3,3,3}, // ╬  256C
    {0,0,0,0}, // ╭  256D (rounded corner - special)
    {0,0,0,0}, // ╮  256E (rounded corner - special)
    {0,0,0,0}, // ╯  256F (rounded corner - special)

    // 2570-257F
    {0,0,0,0}, // ╰  2570 (rounded corner - special)
    {0,0,0,0}, // ╱  2571 (diagonal - special)
    {0,0,0,0}, // ╲  2572 (diagonal - special)
    {0,0,0,0}, // ╳  2573 (diagonal cross - special)
    {1,0,0,0}, // ╴  2574 (light left)
    {0,1,0,0}, // ╵  2575 (light up)
    {0,0,1,0}, // ╶  2576 (light right)
    {0,0,0,1}, // ╷  2577 (light down)
    {2,0,0,0}, // ╸  2578 (heavy left)
    {0,2,0,0}, // ╹  2579 (heavy up)
    {0,0,2,0}, // ╺  257A (heavy right)
    {0,0,0,2}, // ╻  257B (heavy down)
    {1,0,2,0}, // ╼  257C (light left heavy right)
    {0,1,0,2}, // ╽  257D (light up heavy down)
    {2,0,1,0}, // ╾  257E (heavy left light right)
    {0,2,0,1}, // ╿  257F (heavy up light down)
};


// Helper: set pixel with alpha blending (max)
static void set_pixel_max(std::vector<uint8_t>& bmp, int bw, int bh,
                           int x, int y, uint8_t val) {
    if (x >= 0 && x < bw && y >= 0 && y < bh)
        bmp[y * bw + x] = std::max(bmp[y * bw + x], val);
}

// Render rounded corners (╭╮╯╰) using the Windows Terminal approach:
// Draw a rounded rectangle occupying half the cell, showing only the
// quarter-arc corner. Uses parametric sampling of the arc portion for
// uniform thickness, with the corner radius matching WT's formula.
static void render_rounded_corner(std::vector<uint8_t>& bmp, int w, int h,
                                   char32_t cp, int thickness) {
    int cx = w / 2;
    int cy = h / 2;
    int t = thickness;
    double half_t = t / 2.0;

    // Line spine: center of the thickness band, matching standard box drawing
    // Standard │ fills [cx, cx+t), so its center is at cx + t/2.
    // Standard ─ fills [cy, cy+t), so its center is at cy + t/2.
    double lx = cx + half_t; // vertical line spine x
    double ly = cy + half_t; // horizontal line spine y

    // WT formula: cornerRadius = min(lineWidth * 5, min(rectW, rectH) * 0.5)
    double rectW = static_cast<double>(w - cx);
    double rectH = static_cast<double>(h - cy);
    double cr = std::min(t * 5.0, std::min(rectW, rectH) * 0.5);
    if (cr < 1.0) cr = 1.0;

    // Arc center: offset from line spine by corner radius
    double acx, acy;
    switch (cp) {
    case 0x256D: acx = lx + cr; acy = ly + cr; break;
    case 0x256E: acx = lx - cr; acy = ly + cr; break;
    case 0x256F: acx = lx - cr; acy = ly - cr; break;
    case 0x2570: acx = lx + cr; acy = ly - cr; break;
    default: return;
    }

    // 1) Draw straight segments with fill_rect (matches standard box lines exactly)
    int acy_i = static_cast<int>(std::round(acy));
    int acx_i = static_cast<int>(std::round(acx));
    switch (cp) {
    case 0x256D: // ╭ down + right
        fill_rect(bmp, w, h, cx, acy_i, cx + t, h, 255);
        fill_rect(bmp, w, h, acx_i, cy, w, cy + t, 255);
        break;
    case 0x256E: // ╮ down + left
        fill_rect(bmp, w, h, cx, acy_i, cx + t, h, 255);
        fill_rect(bmp, w, h, 0, cy, acx_i + t, cy + t, 255);
        break;
    case 0x256F: // ╯ up + left
        fill_rect(bmp, w, h, cx, 0, cx + t, acy_i + t, 255);
        fill_rect(bmp, w, h, 0, cy, acx_i + t, cy + t, 255);
        break;
    case 0x2570: // ╰ up + right
        fill_rect(bmp, w, h, cx, 0, cx + t, acy_i + t, 255);
        fill_rect(bmp, w, h, acx_i, cy, w, cy + t, 255);
        break;
    }

    // 2) Draw arc using exact circular distance (no sampling needed)
    //    Distance to circular arc = |dist_to_center - cr|
    //    Use minimal AA (0.7px) to match fill_rect's hard edges
    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            double x = px + 0.5;
            double y = py + 0.5;

            // Check if pixel is in the arc quadrant
            bool in_arc_zone = false;
            switch (cp) {
            case 0x256D: in_arc_zone = (x <= acx && y <= acy); break;
            case 0x256E: in_arc_zone = (x >= acx && y <= acy); break;
            case 0x256F: in_arc_zone = (x >= acx && y >= acy); break;
            case 0x2570: in_arc_zone = (x <= acx && y >= acy); break;
            }
            if (!in_arc_zone) continue;

            double dx = x - acx;
            double dy = y - acy;
            double dist_to_center = std::sqrt(dx * dx + dy * dy);
            double dist_to_arc = std::abs(dist_to_center - cr);

            double edge_dist = dist_to_arc - half_t;
            uint8_t alpha;
            if (edge_dist <= 0.0) {
                alpha = 255;
            } else if (edge_dist < 0.7) {
                alpha = static_cast<uint8_t>(255.0 * (1.0 - edge_dist / 0.7));
            } else {
                continue;
            }

            set_pixel_max(bmp, w, h, px, py, alpha);
        }
    }
}

// Render diagonal lines (╱╲╳) with anti-aliasing
static void render_diagonal(std::vector<uint8_t>& bmp, int w, int h,
                              char32_t cp, int thickness) {
    auto draw_aa_line = [&](double x0, double y0, double x1, double y1) {
        double dx = x1 - x0;
        double dy = y1 - y0;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-9) return;
        // Normal vector (perpendicular to line direction)
        double nx = -dy / len;
        double ny = dx / len;
        double half_t = thickness / 2.0;
        for (int py = 0; py < h; ++py) {
            for (int px = 0; px < w; ++px) {
                // Perpendicular distance from pixel center to the ideal line
                double dist = std::abs((px - x0) * nx + (py - y0) * ny);
                // Anti-aliased alpha: smooth falloff over 0.5px at the edge
                double alpha = 255.0 * std::max(0.0, std::min(1.0, half_t - dist + 0.5));
                if (alpha > 0.0) {
                    set_pixel_max(bmp, w, h, px, py, static_cast<uint8_t>(alpha));
                }
            }
        }
    };

    if (cp == 0x2571 || cp == 0x2573) // ╱ or ╳
        draw_aa_line(w - 1, 0, 0, h - 1);
    if (cp == 0x2572 || cp == 0x2573) // ╲ or ╳
        draw_aa_line(0, 0, w - 1, h - 1);
}

// Render dashed lines
static void render_dashed(std::vector<uint8_t>& bmp, int w, int h,
                           char32_t cp, int thickness) {
    int cx = w / 2;
    int cy = h / 2;
    bool horizontal = false;
    int style = 1; // light
    int dashes = 3;

    switch (cp) {
    case 0x2504: horizontal = true;  style = 1; dashes = 3; break;
    case 0x2505: horizontal = true;  style = 2; dashes = 3; break;
    case 0x2506: horizontal = false; style = 1; dashes = 3; break;
    case 0x2507: horizontal = false; style = 2; dashes = 3; break;
    case 0x2508: horizontal = true;  style = 1; dashes = 4; break;
    case 0x2509: horizontal = true;  style = 2; dashes = 4; break;
    case 0x250A: horizontal = false; style = 1; dashes = 4; break;
    case 0x250B: horizontal = false; style = 2; dashes = 4; break;
    case 0x254C: horizontal = true;  style = 1; dashes = 2; break;
    case 0x254D: horizontal = true;  style = 2; dashes = 2; break;
    case 0x254E: horizontal = false; style = 1; dashes = 2; break;
    case 0x254F: horizontal = false; style = 2; dashes = 2; break;
    default: return;
    }

    int t = (style == 2) ? std::max(2, thickness * 2) : thickness;

    if (horizontal) {
        float slot = (float)w / dashes;
        for (int d = 0; d < dashes; ++d) {
            int slot_start = (int)(d * slot);
            int slot_end = (int)((d + 1) * slot);
            int slot_len = slot_end - slot_start;
            int seg = std::max(1, slot_len / 2);
            fill_rect(bmp, w, h, slot_start, cy - t / 2, slot_start + seg, cy - t / 2 + t, 255);
        }
    } else {
        float slot = (float)h / dashes;
        for (int d = 0; d < dashes; ++d) {
            int slot_start = (int)(d * slot);
            int slot_end = (int)((d + 1) * slot);
            int slot_len = slot_end - slot_start;
            int seg = std::max(1, slot_len / 2);
            fill_rect(bmp, w, h, cx - t / 2, slot_start, cx - t / 2 + t, slot_start + seg, 255);
        }
    }
}

// Check if codepoint is a dashed line
static bool is_dashed(char32_t cp) {
    return (cp >= 0x2504 && cp <= 0x250B) ||
           (cp >= 0x254C && cp <= 0x254F);
}

BoxGlyphBitmap render_box_lines(char32_t cp, int cell_width, int cell_height, int thickness) {
    if (cp < 0x2500 || cp > 0x257F)
        return {{}, 0, 0};

    int w = cell_width;
    int h = cell_height;
    BoxGlyphBitmap result;
    result.width = w;
    result.height = h;
    result.bitmap.assign(w * h, 0);

    // Special cases: rounded corners
    if (cp >= 0x256D && cp <= 0x2570) {
        render_rounded_corner(result.bitmap, w, h, cp, thickness);
        return result;
    }

    // Special cases: diagonals
    if (cp >= 0x2571 && cp <= 0x2573) {
        render_diagonal(result.bitmap, w, h, cp, thickness);
        return result;
    }

    // Special cases: dashed lines
    if (is_dashed(cp)) {
        render_dashed(result.bitmap, w, h, cp, thickness);
        return result;
    }

    // Standard box drawing via edge table
    const BoxEdges& e = box_edges_table[cp - 0x2500];
    int cx = w / 2;
    int cy = h / 2;
    int t = thickness;
    int t2 = std::max(2, t * 2); // heavy thickness
    int dbl_gap = std::max(2, t * 2); // gap for double lines

    auto draw_segment = [&](bool horizontal, bool positive, uint8_t style) {
        if (style == 0) return;
        if (style == 1) { // light
            if (horizontal && positive)      // right
                fill_rect(result.bitmap, w, h, cx, cy, w, cy + t, 255);
            else if (horizontal && !positive) // left
                fill_rect(result.bitmap, w, h, 0, cy, cx + t, cy + t, 255);
            else if (!horizontal && !positive) // up
                fill_rect(result.bitmap, w, h, cx, 0, cx + t, cy + t, 255);
            else                              // down
                fill_rect(result.bitmap, w, h, cx, cy, cx + t, h, 255);
        } else if (style == 2) { // heavy
            int ht = t2;
            int off = ht / 2;
            if (horizontal && positive)
                fill_rect(result.bitmap, w, h, cx, cy - off, w, cy - off + ht, 255);
            else if (horizontal && !positive)
                fill_rect(result.bitmap, w, h, 0, cy - off, cx + off, cy - off + ht, 255);
            else if (!horizontal && !positive)
                fill_rect(result.bitmap, w, h, cx - off, 0, cx - off + ht, cy + off, 255);
            else
                fill_rect(result.bitmap, w, h, cx - off, cy - off, cx - off + ht, h, 255);
        } else if (style == 3) { // double
            int off = dbl_gap / 2 + t;
            if (horizontal && positive) {
                fill_rect(result.bitmap, w, h, cx, cy - off, w, cy - off + t, 255);
                fill_rect(result.bitmap, w, h, cx, cy + off - t + 1, w, cy + off + 1, 255);
            } else if (horizontal && !positive) {
                fill_rect(result.bitmap, w, h, 0, cy - off, cx + t, cy - off + t, 255);
                fill_rect(result.bitmap, w, h, 0, cy + off - t + 1, cx + t, cy + off + 1, 255);
            } else if (!horizontal && !positive) {
                fill_rect(result.bitmap, w, h, cx - off, 0, cx - off + t, cy + t, 255);
                fill_rect(result.bitmap, w, h, cx + off - t + 1, 0, cx + off + 1, cy + t, 255);
            } else {
                fill_rect(result.bitmap, w, h, cx - off, cy, cx - off + t, h, 255);
                fill_rect(result.bitmap, w, h, cx + off - t + 1, cy, cx + off + 1, h, 255);
            }
        }
    };

    // Draw center block: use heavy size if any heavy edge exists, otherwise light
    bool has_light = (e.right == 1 || e.left == 1 || e.up == 1 || e.down == 1);
    bool has_heavy = (e.right == 2 || e.left == 2 || e.up == 2 || e.down == 2);

    if (has_heavy) {
        // Heavy center block covers both heavy and light connections
        int ht = t2;
        int off = ht / 2;
        fill_rect(result.bitmap, w, h, cx - off, cy - off, cx - off + ht, cy - off + ht, 255);
    } else if (has_light) {
        // Light-only center block
        fill_rect(result.bitmap, w, h, cx, cy, cx + t, cy + t, 255);
    }

    draw_segment(true, true, e.right);    // right
    draw_segment(false, false, e.up);     // up
    draw_segment(true, false, e.left);    // left
    draw_segment(false, true, e.down);    // down

    return result;
}

} // namespace termcore
