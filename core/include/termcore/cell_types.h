#ifndef TERMCORE_CELL_TYPES_H
#define TERMCORE_CELL_TYPES_H

#include "termcore/term_cell.h"
#include <cstdint>

namespace termcore {

/// Lightweight cell for text processing, search, and selection (~8 bytes).
/// Stores only the codepoint and text-layout data; grapheme cluster extras
/// are externalized to a GraphemeStore via grapheme_idx.
struct CpuCell {
    char32_t codepoint = 0;    // 4B - base codepoint (U+0000 = empty)
    uint8_t  width = 1;        // 1B - display width (1 or 2 for wide chars)
    uint8_t  extra_count = 0;  // 1B - number of extra codepoints in grapheme store
    uint16_t grapheme_idx = 0; // 2B - index into GraphemeStore (0 = none)
};

/// Rendering-only cell (~12 bytes).
/// Contains colors and SGR attributes needed by the GPU renderer.
struct GpuCell {
    uint32_t fg_color = kColorDefault; // 4B
    uint32_t bg_color = kColorDefault; // 4B
    uint16_t attributes = 0;          // 2B - CellAttribute bitmask
    uint8_t  underline_style = 0;     // 1B - UnderlineStyle enum
    uint8_t  _pad = 0;               // 1B - alignment padding
};

// Compile-time size guarantees
static_assert(sizeof(CpuCell) <= 8, "CpuCell should be <= 8 bytes");
static_assert(sizeof(GpuCell) <= 12, "GpuCell should be <= 12 bytes");

// --- Conversion functions between TermCell and CpuCell/GpuCell ---

/// Extract text-processing fields from a TermCell.
/// Note: grapheme_idx is set to 0 (caller must use GraphemeStore to externalize extras).
inline CpuCell toCpuCell(const TermCell& tc) {
    CpuCell c;
    c.codepoint = tc.codepoint;
    c.width = tc.width;
    c.extra_count = tc.extra_count;
    c.grapheme_idx = 0; // requires GraphemeStore - caller must populate
    return c;
}

/// Extract rendering fields from a TermCell.
/// Note: underline_color is not stored in GpuCell (moved to sparse map).
inline GpuCell toGpuCell(const TermCell& tc) {
    GpuCell g;
    g.fg_color = tc.fg_color;
    g.bg_color = tc.bg_color;
    g.attributes = tc.attributes;
    g.underline_style = tc.underline_style;
    return g;
}

/// Reconstruct a TermCell from CpuCell + GpuCell pair.
/// Grapheme extras must be restored separately via GraphemeStore.
inline TermCell fromCells(const CpuCell& cpu, const GpuCell& gpu) {
    TermCell tc;
    tc.codepoint = cpu.codepoint;
    tc.width = cpu.width;
    tc.extra_count = cpu.extra_count;
    tc.fg_color = gpu.fg_color;
    tc.bg_color = gpu.bg_color;
    tc.attributes = gpu.attributes;
    tc.underline_style = gpu.underline_style;
    // underline_color and extra[] must be restored by the caller
    return tc;
}

} // namespace termcore

#endif // TERMCORE_CELL_TYPES_H
