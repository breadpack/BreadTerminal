#include "termcore/grapheme.h"
#include "termcore/font/unicode_width.h"

namespace termcore {

bool isGraphemeBreak(uint32_t cp1, uint32_t cp2) {
    // Simple two-codepoint check without full cluster context.
    // For RI pairs and ZWJ sequences, this gives a conservative answer.
    GBP p1 = graphemeBreakProperty(static_cast<char32_t>(cp1));
    GBP p2 = graphemeBreakProperty(static_cast<char32_t>(cp2));

    // GB3: CR x LF
    if (p1 == GBP::CR && p2 == GBP::LF) return false;

    // GB4: (Control|CR|LF) divides
    if (p1 == GBP::Control || p1 == GBP::CR || p1 == GBP::LF) return true;

    // GB5: divides (Control|CR|LF)
    if (p2 == GBP::Control || p2 == GBP::CR || p2 == GBP::LF) return true;

    // GB6: L x (L|V|LV|LVT)
    if (p1 == GBP::L && (p2 == GBP::L || p2 == GBP::V || p2 == GBP::LV || p2 == GBP::LVT))
        return false;

    // GB7: (LV|V) x (V|T)
    if ((p1 == GBP::LV || p1 == GBP::V) && (p2 == GBP::V || p2 == GBP::T))
        return false;

    // GB8: (LVT|T) x T
    if ((p1 == GBP::LVT || p1 == GBP::T) && p2 == GBP::T)
        return false;

    // GB9: x (Extend|ZWJ)
    if (p2 == GBP::Extend || p2 == GBP::ZWJ) return false;

    // GB9a: x SpacingMark
    if (p2 == GBP::SpacingMark) return false;

    // GB9b: Prepend x
    if (p1 == GBP::Prepend) return false;

    // GB11: ZWJ x ExtPict (simplified — full check requires ExtPict Extend* ZWJ context)
    if (p1 == GBP::ZWJ && p2 == GBP::Extended_Pictographic) return false;

    // GB12/GB13: RI x RI — without context, assume first pair (no break)
    if (p1 == GBP::Regional_Indicator && p2 == GBP::Regional_Indicator)
        return false;

    // GB999: Otherwise, break
    return true;
}

int graphemeClusterWidth(const std::vector<uint32_t>& codepoints) {
    if (codepoints.empty()) return 0;

    // Check if it contains a ZWJ sequence (emoji ZWJ sequence = width 2)
    bool has_zwj = false;
    bool has_emoji = false;
    int ri_count = 0;

    for (uint32_t cp : codepoints) {
        if (cp == 0x200D) has_zwj = true;
        GBP prop = graphemeBreakProperty(static_cast<char32_t>(cp));
        if (prop == GBP::Extended_Pictographic) has_emoji = true;
        if (prop == GBP::Regional_Indicator) ri_count++;
    }

    // Emoji ZWJ sequence (e.g., family emoji) = width 2
    if (has_zwj && has_emoji) return 2;

    // Flag sequence (two regional indicators) = width 2
    if (ri_count == 2) return 2;

    // For base + combining marks, width comes from the base codepoint
    return codepoint_width(static_cast<char32_t>(codepoints[0]));
}

} // namespace termcore
