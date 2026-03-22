#ifndef TERMCORE_GRAPHEME_H
#define TERMCORE_GRAPHEME_H

#include <cstdint>
#include <vector>

namespace termcore {

/// Determines if there is a grapheme cluster boundary between two codepoints
/// per UAX #29. This is a simplified wrapper around the full isGraphemeBreak()
/// that uses default context (no RI count tracking, no ZWJ state).
/// For full context-aware detection, use the version in unicode_width.h.
bool isGraphemeBreak(uint32_t cp1, uint32_t cp2);

/// Returns the display width of a grapheme cluster (sequence of codepoints).
/// - Single base codepoint: returns its codepoint_width
/// - Base + combining marks: returns width of base
/// - Emoji ZWJ sequence: returns 2
/// - Regional indicator pair (flags): returns 2
/// - Empty: returns 0
int graphemeClusterWidth(const std::vector<uint32_t>& codepoints);

} // namespace termcore

#endif // TERMCORE_GRAPHEME_H
