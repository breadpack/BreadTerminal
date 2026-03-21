#ifndef TERMCORE_TERM_CELL_H
#define TERMCORE_TERM_CELL_H

#include "termcore/dynamic_colors.h"  // for kColorDefault
#include <cstdint>

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

/// A single cell in the terminal grid.
struct TermCell {
    char32_t codepoint = ' ';
    uint32_t fg_color = kColorDefault;
    uint32_t bg_color = kColorDefault;
    uint16_t attributes = 0;
    uint8_t width = 1;
    uint8_t underline_style = UnderlineNone;
    uint32_t underline_color = kColorDefault;
};

} // namespace termcore

#endif // TERMCORE_TERM_CELL_H
