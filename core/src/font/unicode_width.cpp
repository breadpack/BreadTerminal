#include "termcore/font/unicode_width.h"

#include <unicode/uchar.h>
#include <unicode/utypes.h>

namespace termcore {

// ---------------------------------------------------------------------------
// UTF-8 codec
// ---------------------------------------------------------------------------

char32_t utf8_decode(const char* data, size_t len, size_t& pos) {
    if (pos >= len) {
        return 0xFFFD;
    }

    auto b0 = static_cast<uint8_t>(data[pos]);

    // 1-byte (ASCII)
    if (b0 < 0x80) {
        pos += 1;
        return static_cast<char32_t>(b0);
    }

    // Determine expected byte count
    int extra = 0;
    char32_t cp = 0;
    if ((b0 & 0xE0) == 0xC0) {
        extra = 1;
        cp = b0 & 0x1F;
    } else if ((b0 & 0xF0) == 0xE0) {
        extra = 2;
        cp = b0 & 0x0F;
    } else if ((b0 & 0xF8) == 0xF0) {
        extra = 3;
        cp = b0 & 0x07;
    } else {
        // Invalid leading byte
        pos += 1;
        return 0xFFFD;
    }

    if (pos + 1 + extra > len) {
        pos += 1;
        return 0xFFFD;
    }

    for (int i = 1; i <= extra; ++i) {
        auto b = static_cast<uint8_t>(data[pos + i]);
        if ((b & 0xC0) != 0x80) {
            pos += 1;
            return 0xFFFD;
        }
        cp = (cp << 6) | (b & 0x3F);
    }

    pos += 1 + extra;

    // Reject overlong encodings and surrogates
    if ((extra == 1 && cp < 0x80) ||
        (extra == 2 && cp < 0x800) ||
        (extra == 3 && cp < 0x10000) ||
        (cp >= 0xD800 && cp <= 0xDFFF) ||
        cp > 0x10FFFF) {
        return 0xFFFD;
    }

    return cp;
}

void utf8_encode(char32_t cp, std::string& output) {
    if (cp < 0x80) {
        output.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        output.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        output.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        output.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        output.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        output.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        output.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        // Invalid codepoint — encode replacement character
        utf8_encode(0xFFFD, output);
    }
}

// ---------------------------------------------------------------------------
// Zero-width detection
// ---------------------------------------------------------------------------

bool is_zero_width(char32_t cp) {
    // C0/C1 control characters
    if (cp <= 0x1F || (cp >= 0x7F && cp <= 0x9F)) {
        return true;
    }

    // Explicit zero-width characters
    if (cp == 0x200B ||  // ZERO WIDTH SPACE
        cp == 0x200C ||  // ZERO WIDTH NON-JOINER
        cp == 0x200D ||  // ZERO WIDTH JOINER
        cp == 0x2060 ||  // WORD JOINER
        cp == 0xFEFF) {  // ZERO WIDTH NO-BREAK SPACE (BOM)
        return true;
    }

    // Soft hyphen
    if (cp == 0x00AD) {
        return true;
    }

    // Variation selectors
    if ((cp >= 0xFE00 && cp <= 0xFE0F) ||
        (cp >= 0xE0100 && cp <= 0xE01EF)) {
        return true;
    }

    // Combining marks via ICU
    auto cat = static_cast<UCharCategory>(u_charType(static_cast<UChar32>(cp)));
    if (cat == U_NON_SPACING_MARK ||
        cat == U_ENCLOSING_MARK) {
        return true;
    }

    // Default ignorable code points
    if (u_hasBinaryProperty(static_cast<UChar32>(cp),
                            UCHAR_DEFAULT_IGNORABLE_CODE_POINT)) {
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// Codepoint width
// ---------------------------------------------------------------------------

int codepoint_width(char32_t cp) {
    // Control characters → 0
    if (cp <= 0x1F || (cp >= 0x7F && cp <= 0x9F)) {
        return 0;
    }

    // Zero-width characters → 0
    if (is_zero_width(cp)) {
        return 0;
    }

    // Use ICU East Asian Width property
    auto eaw = u_getIntPropertyValue(static_cast<UChar32>(cp),
                                     UCHAR_EAST_ASIAN_WIDTH);

    if (eaw == U_EA_FULLWIDTH || eaw == U_EA_WIDE) {
        return 2;
    }

    // Ambiguous → 1 (standard terminal convention)
    return 1;
}

// ---------------------------------------------------------------------------
// String display width
// ---------------------------------------------------------------------------

int string_display_width(std::string_view str) {
    int width = 0;
    size_t pos = 0;
    while (pos < str.size()) {
        char32_t cp = utf8_decode(str.data(), str.size(), pos);
        width += codepoint_width(cp);
    }
    return width;
}

// ---------------------------------------------------------------------------
// Grapheme cluster helpers
// ---------------------------------------------------------------------------

namespace {

bool is_combining(char32_t cp) {
    auto cat = static_cast<UCharCategory>(u_charType(static_cast<UChar32>(cp)));
    return cat == U_NON_SPACING_MARK || cat == U_ENCLOSING_MARK;
}

bool is_regional_indicator(char32_t cp) {
    return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

bool is_variation_selector(char32_t cp) {
    return (cp >= 0xFE00 && cp <= 0xFE0F) ||
           (cp >= 0xE0100 && cp <= 0xE01EF);
}

constexpr char32_t ZWJ = 0x200D;

} // anonymous namespace

std::vector<GraphemeCluster> split_graphemes(std::string_view str) {
    std::vector<GraphemeCluster> clusters;
    size_t pos = 0;

    while (pos < str.size()) {
        char32_t cp = utf8_decode(str.data(), str.size(), pos);

        // Skip zero-width characters that appear at the start (no cluster to extend)
        if (clusters.empty() && is_zero_width(cp) && !is_regional_indicator(cp)) {
            // Still create a cluster for standalone zero-width chars
            GraphemeCluster cluster;
            cluster.codepoints.push_back(cp);
            cluster.display_width = 0;
            clusters.push_back(std::move(cluster));
            continue;
        }

        // Check if this codepoint extends the previous cluster
        bool extends = false;
        if (!clusters.empty()) {
            if (is_combining(cp)) {
                extends = true;
            } else if (cp == ZWJ) {
                extends = true;
            } else if (is_variation_selector(cp)) {
                extends = true;
            } else if (is_regional_indicator(cp)) {
                // Regional indicators pair up: extend if previous cluster
                // has exactly one regional indicator
                auto& prev = clusters.back();
                if (prev.codepoints.size() == 1 &&
                    is_regional_indicator(prev.codepoints[0])) {
                    extends = true;
                }
            }
            // After ZWJ, the next codepoint also extends
            if (!extends && !clusters.empty()) {
                auto& prev = clusters.back();
                if (!prev.codepoints.empty() &&
                    prev.codepoints.back() == ZWJ) {
                    extends = true;
                }
            }
        }

        if (extends) {
            clusters.back().codepoints.push_back(cp);
            // Recalculate display width: use width of the base character
            // For regional indicator pairs, width is 2
            if (is_regional_indicator(cp) && clusters.back().codepoints.size() == 2) {
                clusters.back().display_width = 2;
            }
        } else {
            GraphemeCluster cluster;
            cluster.codepoints.push_back(cp);
            cluster.display_width = codepoint_width(cp);
            clusters.push_back(std::move(cluster));
        }
    }

    return clusters;
}

} // namespace termcore
