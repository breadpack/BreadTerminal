#ifndef TERMCORE_UNICODE_WIDTH_H
#define TERMCORE_UNICODE_WIDTH_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace termcore {

/// Returns the display width of a codepoint (0, 1, or 2).
/// - 0: control chars, zero-width joiners, combining marks, etc.
/// - 1: most characters (Latin, Cyrillic, etc.)
/// - 2: CJK ideographs, fullwidth forms, wide emoji
int codepoint_width(char32_t cp);

/// Returns whether a codepoint is a zero-width character.
bool is_zero_width(char32_t cp);

/// Returns the display width of a UTF-8 string (sum of codepoint widths).
int string_display_width(std::string_view str);

/// Grapheme cluster info
struct GraphemeCluster {
    std::u32string codepoints;
    int display_width;  // 1 or 2
};

/// Split a UTF-8 string into grapheme clusters with display widths.
/// Simplified UAX #29 implementation suitable for terminal use.
std::vector<GraphemeCluster> split_graphemes(std::string_view str);

/// Decode one UTF-8 codepoint from a string. Returns codepoint and advances pos.
/// Returns 0xFFFD on invalid sequences.
char32_t utf8_decode(const char* data, size_t len, size_t& pos);

/// Encode a codepoint to UTF-8, appending to output.
void utf8_encode(char32_t cp, std::string& output);

} // namespace termcore

#endif
