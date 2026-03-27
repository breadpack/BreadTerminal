#pragma once

#include "termcore/term_cell.h"
#include <cstdint>
#include <optional>

namespace termcore {

/// The Kitty Unicode Placeholder base codepoint.
inline constexpr char32_t kKittyPlaceholder = 0x10EEEE;

/// Diacritical mark selectors used in Kitty Unicode Placeholder encoding.
/// Each selector is followed by a codepoint whose value encodes the data.
inline constexpr char32_t kPlaceholderImageIdHigh  = 0x0305;
inline constexpr char32_t kPlaceholderImageIdLow   = 0x030D;
inline constexpr char32_t kPlaceholderPlacementId  = 0x0310;
inline constexpr char32_t kPlaceholderRow          = 0x0312;
inline constexpr char32_t kPlaceholderCol          = 0x0313;

/// Decoded information from a Kitty Unicode Placeholder cell.
struct PlaceholderInfo {
    uint32_t image_id = 0;
    uint32_t placement_id = 0;
    int row = 0;
    int col = 0;
};

/// Returns true if the given codepoint is a Kitty Unicode Placeholder diacritical selector.
inline bool isPlaceholderSelector(char32_t cp) {
    return cp == kPlaceholderImageIdHigh
        || cp == kPlaceholderImageIdLow
        || cp == kPlaceholderPlacementId
        || cp == kPlaceholderRow
        || cp == kPlaceholderCol;
}

/// Returns placeholder info if the cell is a Kitty Unicode Placeholder (U+10EEEE).
/// Returns std::nullopt if the cell is not a placeholder.
///
/// Encoding: U+10EEEE followed by pairs of (selector, value_codepoint):
///   U+0305 + chr(id_high)  -> image ID upper byte
///   U+030D + chr(id_low)   -> image ID lower byte
///   U+0310 + chr(pid)      -> placement ID
///   U+0312 + chr(row)      -> row index (0-based)
///   U+0313 + chr(col)      -> column index (0-based)
inline std::optional<PlaceholderInfo> decodePlaceholder(const TermCell& cell) {
    if (cell.codepoint != kKittyPlaceholder) return std::nullopt;

    PlaceholderInfo info;

    for (uint8_t i = 0; i < cell.extra_count; ++i) {
        char32_t selector = cell.extra[i];
        // Each selector must be followed by a value codepoint
        if (i + 1 >= cell.extra_count) break;

        if (selector == kPlaceholderImageIdHigh) {
            uint32_t val = static_cast<uint32_t>(cell.extra[++i]);
            info.image_id = (info.image_id & 0x00FF) | (val << 8);
        } else if (selector == kPlaceholderImageIdLow) {
            uint32_t val = static_cast<uint32_t>(cell.extra[++i]);
            info.image_id = (info.image_id & 0xFF00) | (val & 0xFF);
        } else if (selector == kPlaceholderPlacementId) {
            info.placement_id = static_cast<uint32_t>(cell.extra[++i]);
        } else if (selector == kPlaceholderRow) {
            info.row = static_cast<int>(cell.extra[++i]);
        } else if (selector == kPlaceholderCol) {
            info.col = static_cast<int>(cell.extra[++i]);
        }
        // Unknown selectors are skipped (single codepoint, not a pair)
    }

    return info;
}

} // namespace termcore
