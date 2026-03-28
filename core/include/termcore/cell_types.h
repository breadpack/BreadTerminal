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

/// Rendering-only cell (~4 bytes, packed).
/// Contains color indices and SGR attributes needed by the GPU renderer.
///
/// Colors are stored as 8-bit indices into a per-segment color table.
/// Index 0 is reserved for "default color" (kColorDefault).
/// Index 1-254 are entries in the segment's color_table.
/// Index 255 signals overflow: the actual color is stored in the segment's
/// overflow_colors sparse map.
///
/// Packed attributes layout (uint16_t):
///   bits  0-7 : CellAttribute bitmask (bold, italic, underline, etc.)
///   bits  8-10: UnderlineStyle enum (0-5)
///   bits 11-15: reserved
#pragma pack(push, 1)
struct GpuCell {
    uint8_t  fg_color_idx = 0; // 1B - index into segment color table (0 = default)
    uint8_t  bg_color_idx = 0; // 1B - index into segment color table (0 = default)
    uint16_t packed_attrs = 0; // 2B

    // --- Attribute bit positions ---
    static constexpr uint16_t kAttrMask       = 0x00FF; // bits 0-7
    static constexpr uint16_t kUnderlineShift  = 8;
    static constexpr uint16_t kUnderlineMask   = 0x0700; // bits 8-10

    // --- Color index constants ---
    static constexpr uint8_t kDefaultColorIdx = 0;    // "use terminal default"
    static constexpr uint8_t kOverflowIdx     = 255;  // color in overflow map

    // --- Attribute accessors ---

    /// Get CellAttribute bitmask (bits 0-7).
    uint16_t getAttributes() const {
        return packed_attrs & kAttrMask;
    }

    /// Set CellAttribute bitmask (bits 0-7), preserving other fields.
    void setAttributes(uint16_t attrs) {
        packed_attrs = (packed_attrs & ~kAttrMask) | (attrs & kAttrMask);
    }

    /// Get underline style (bits 8-10).
    uint8_t getUnderlineStyle() const {
        return static_cast<uint8_t>((packed_attrs & kUnderlineMask) >> kUnderlineShift);
    }

    /// Set underline style (bits 8-10), preserving other fields.
    void setUnderlineStyle(uint8_t style) {
        packed_attrs = (packed_attrs & ~kUnderlineMask) |
                       (static_cast<uint16_t>(style & 0x07) << kUnderlineShift);
    }
};
#pragma pack(pop)

// Compile-time size guarantees
static_assert(sizeof(CpuCell) <= 8, "CpuCell should be <= 8 bytes");
static_assert(sizeof(GpuCell) <= 4, "GpuCell should be <= 4 bytes");

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

/// Reconstruct a TermCell from CpuCell + GpuCell pair.
/// NOTE: Colors cannot be resolved from GpuCell alone; caller must use
/// the segment's color table to resolve fg_color_idx/bg_color_idx.
/// Grapheme extras must be restored separately via GraphemeStore.
inline TermCell fromCells(const CpuCell& cpu, const GpuCell& gpu) {
    TermCell tc;
    tc.codepoint = cpu.codepoint;
    tc.width = cpu.width;
    tc.extra_count = cpu.extra_count;
    // Colors are set to defaults here; caller must resolve from segment color table.
    tc.fg_color = kColorDefault;
    tc.bg_color = kColorDefault;
    tc.attributes = gpu.getAttributes();
    tc.underline_style = gpu.getUnderlineStyle();
    // underline_color and extra[] must be restored by the caller
    return tc;
}

} // namespace termcore

#endif // TERMCORE_CELL_TYPES_H
