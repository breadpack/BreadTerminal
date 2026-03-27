#ifndef TERMCORE_TERM_CELL_H
#define TERMCORE_TERM_CELL_H

#include "termcore/dynamic_colors.h"  // for kColorDefault
#include <cstdint>
#include <string>

namespace termcore {

/// Attribute flags for TermCell.
enum CellAttribute : uint16_t {
    AttrBold          = 1,
    AttrItalic        = 2,
    AttrUnderline     = 4,
    AttrBlink         = 8,
    AttrInverse       = 16,
    AttrHidden        = 32,
    AttrStrikethrough = 64,
    AttrDim           = 128,
};

/// Underline style values (SGR 4:x).
enum UnderlineStyle : uint8_t {
    UnderlineNone   = 0,
    UnderlineSingle = 1,
    UnderlineDouble = 2,
    UnderlineCurly  = 3,
    UnderlineDotted = 4,
    UnderlineDashed = 5,
};

/// Maximum extra codepoints stored inline per cell for grapheme clusters.
/// 10 slots to accommodate Kitty Unicode Placeholder (U+10EEEE) which can have
/// up to 10 extra codepoints: 5 diacritical selectors + 5 value codepoints.
static constexpr int kMaxExtraCodepoints = 10;

/// A single cell in the terminal grid.
struct TermCell {
    char32_t codepoint = ' ';
    uint32_t fg_color = kColorDefault;
    uint32_t bg_color = kColorDefault;
    uint16_t attributes = 0;
    uint8_t width = 1;
    uint8_t underline_style = UnderlineNone;
    uint32_t underline_color = kColorDefault;

    /// Extra codepoints for grapheme clusters (combining marks, ZWJ sequences, etc.)
    char32_t extra[kMaxExtraCodepoints] = {};
    uint8_t extra_count = 0;

    /// Append a codepoint to the grapheme cluster.  Returns false if full.
    bool appendCodepoint(char32_t cp) {
        if (extra_count >= kMaxExtraCodepoints) return false;
        extra[extra_count++] = cp;
        return true;
    }

    /// Return all codepoints (base + extras) as a u32string.
    std::u32string allCodepoints() const {
        std::u32string s;
        s.reserve(1 + extra_count);
        s.push_back(codepoint);
        for (uint8_t i = 0; i < extra_count; ++i)
            s.push_back(extra[i]);
        return s;
    }
};

} // namespace termcore

#endif // TERMCORE_TERM_CELL_H
