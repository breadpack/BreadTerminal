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
// Grapheme Break Property (UAX #29)
// ---------------------------------------------------------------------------

GBP graphemeBreakProperty(char32_t cp) {
    if (cp == 0x000D) return GBP::CR;
    if (cp == 0x000A) return GBP::LF;
    if (cp == 0x200D) return GBP::ZWJ;

    // Control characters
    if (cp <= 0x001F || (cp >= 0x007F && cp <= 0x009F) ||
        cp == 0x00AD || cp == 0x061C ||
        cp == 0x200B || cp == 0x200E || cp == 0x200F ||
        (cp >= 0x2028 && cp <= 0x2029) ||
        (cp >= 0x2060 && cp <= 0x2064) ||
        (cp >= 0xFFF0 && cp <= 0xFFF8) ||
        cp == 0xFEFF) {
        return GBP::Control;
    }

    // Regional Indicators (U+1F1E6..U+1F1FF)
    if (cp >= 0x1F1E6 && cp <= 0x1F1FF) return GBP::Regional_Indicator;

    // Hangul Jamo
    if (cp >= 0x1100 && cp <= 0x115F) return GBP::L;
    if (cp >= 0xA960 && cp <= 0xA97C) return GBP::L;
    if (cp >= 0x1160 && cp <= 0x11A7) return GBP::V;
    if (cp >= 0xD7B0 && cp <= 0xD7C6) return GBP::V;
    if (cp >= 0x11A8 && cp <= 0x11FF) return GBP::T;
    if (cp >= 0xD7CB && cp <= 0xD7FB) return GBP::T;
    // LV and LVT syllables
    if (cp >= 0xAC00 && cp <= 0xD7A3) {
        if ((cp - 0xAC00) % 28 == 0) return GBP::LV;
        return GBP::LVT;
    }

    // Extended_Pictographic (common emoji ranges)
    if (cp == 0x00A9 || cp == 0x00AE) return GBP::Extended_Pictographic;
    if (cp >= 0x2600 && cp <= 0x27BF) return GBP::Extended_Pictographic;
    if (cp >= 0x2B05 && cp <= 0x2B55) return GBP::Extended_Pictographic;
    if (cp >= 0x1F000 && cp <= 0x1FAFF) return GBP::Extended_Pictographic;
    if (cp >= 0xFE00 && cp <= 0xFE0F) return GBP::Extend; // variation selectors

    // Extend (combining marks) — simplified ranges
    if (cp >= 0x0300 && cp <= 0x036F) return GBP::Extend;  // Combining Diacriticals
    if (cp >= 0x0483 && cp <= 0x0489) return GBP::Extend;
    if (cp >= 0x0591 && cp <= 0x05BD) return GBP::Extend;
    if (cp == 0x05BF) return GBP::Extend;
    if (cp >= 0x05C1 && cp <= 0x05C2) return GBP::Extend;
    if (cp >= 0x05C4 && cp <= 0x05C5) return GBP::Extend;
    if (cp == 0x05C7) return GBP::Extend;
    if (cp >= 0x0610 && cp <= 0x061A) return GBP::Extend;
    if (cp >= 0x064B && cp <= 0x065F) return GBP::Extend;
    if (cp == 0x0670) return GBP::Extend;
    if (cp >= 0x06D6 && cp <= 0x06DC) return GBP::Extend;
    if (cp >= 0x06DF && cp <= 0x06E4) return GBP::Extend;
    if (cp >= 0x06E7 && cp <= 0x06E8) return GBP::Extend;
    if (cp >= 0x06EA && cp <= 0x06ED) return GBP::Extend;
    if (cp == 0x0711) return GBP::Extend;
    if (cp >= 0x0730 && cp <= 0x074A) return GBP::Extend;
    if (cp >= 0x0900 && cp <= 0x0903) return GBP::Extend;  // Devanagari
    if (cp >= 0x093A && cp <= 0x094F) return GBP::Extend;
    if (cp >= 0x0951 && cp <= 0x0957) return GBP::Extend;
    if (cp >= 0x0962 && cp <= 0x0963) return GBP::Extend;
    if (cp >= 0x0981 && cp <= 0x0983) return GBP::Extend;  // Bengali
    if (cp == 0x09BC) return GBP::Extend;
    if (cp >= 0x09BE && cp <= 0x09CD) return GBP::Extend;
    if (cp >= 0x1AB0 && cp <= 0x1AFF) return GBP::Extend;  // Combining Diacriticals Extended
    if (cp >= 0x1DC0 && cp <= 0x1DFF) return GBP::Extend;  // Combining Diacriticals Supplement
    if (cp >= 0x20D0 && cp <= 0x20FF) return GBP::Extend;  // Combining Marks for Symbols
    if (cp >= 0xFE20 && cp <= 0xFE2F) return GBP::Extend;  // Combining Half Marks
    if (cp == 0x200C) return GBP::Extend;  // ZWNJ
    // Emoji modifiers (skin tone)
    if (cp >= 0x1F3FB && cp <= 0x1F3FF) return GBP::Extend;
    // Variation selectors supplement
    if (cp >= 0xE0100 && cp <= 0xE01EF) return GBP::Extend;
    // Enclosing marks
    if (cp >= 0x20DD && cp <= 0x20E0) return GBP::Extend;
    if (cp >= 0x20E2 && cp <= 0x20E4) return GBP::Extend;
    if (cp == 0xFE0E || cp == 0xFE0F) return GBP::Extend;  // VS15/VS16

    // SpacingMark (common ones)
    if (cp == 0x0903) return GBP::SpacingMark;

    return GBP::Other;
}

// ---------------------------------------------------------------------------
// Grapheme Break detection (UAX #29)
// ---------------------------------------------------------------------------

bool isGraphemeBreak(char32_t cp1, char32_t cp2, int ri_count, bool after_zwj) {
    GBP p1 = graphemeBreakProperty(cp1);
    GBP p2 = graphemeBreakProperty(cp2);

    // GB3: CR x LF
    if (p1 == GBP::CR && p2 == GBP::LF) return false;

    // GB4: (Control|CR|LF) ÷
    if (p1 == GBP::Control || p1 == GBP::CR || p1 == GBP::LF) return true;

    // GB5: ÷ (Control|CR|LF)
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

    // GB11: ExtPict Extend* ZWJ x ExtPict
    if (after_zwj && p2 == GBP::Extended_Pictographic) return false;

    // GB12/GB13: Regional_Indicator x Regional_Indicator (only if odd count)
    if (p1 == GBP::Regional_Indicator && p2 == GBP::Regional_Indicator) {
        return (ri_count % 2) == 0;  // break if even count (pair complete)
    }

    // GB999: Otherwise, ÷
    return true;
}

// ---------------------------------------------------------------------------
// Grapheme cluster splitting
// ---------------------------------------------------------------------------

std::vector<GraphemeCluster> split_graphemes(std::string_view str) {
    std::vector<GraphemeCluster> clusters;
    size_t pos = 0;

    // State for grapheme break rules
    int ri_count = 0;          // consecutive Regional_Indicator count
    bool seen_ext_pic = false; // have we seen Extended_Pictographic in current cluster
    char32_t prev_cp = 0;

    while (pos < str.size()) {
        char32_t cp = utf8_decode(str.data(), str.size(), pos);

        if (clusters.empty()) {
            // First codepoint always starts a new cluster
            GraphemeCluster cluster;
            cluster.codepoints.push_back(cp);
            cluster.display_width = codepoint_width(cp);
            clusters.push_back(std::move(cluster));

            GBP prop = graphemeBreakProperty(cp);
            ri_count = (prop == GBP::Regional_Indicator) ? 1 : 0;
            seen_ext_pic = (prop == GBP::Extended_Pictographic);
            prev_cp = cp;
            continue;
        }

        // Determine after_zwj: was the previous codepoint a ZWJ, and did we
        // see an Extended_Pictographic earlier in this cluster?
        bool after_zwj = seen_ext_pic && (graphemeBreakProperty(prev_cp) == GBP::ZWJ);

        bool should_break = isGraphemeBreak(prev_cp, cp, ri_count, after_zwj);

        if (!should_break) {
            // Extend current cluster
            clusters.back().codepoints.push_back(cp);

            GBP prop = graphemeBreakProperty(cp);

            // Update RI count
            if (prop == GBP::Regional_Indicator) {
                ri_count++;
                // Regional indicator pairs have display width 2
                if (ri_count == 2) {
                    clusters.back().display_width = 2;
                }
            }

            // Track Extended_Pictographic within current cluster
            if (prop == GBP::Extended_Pictographic) {
                seen_ext_pic = true;
            }
        } else {
            // Start new cluster
            GraphemeCluster cluster;
            cluster.codepoints.push_back(cp);
            cluster.display_width = codepoint_width(cp);
            clusters.push_back(std::move(cluster));

            GBP prop = graphemeBreakProperty(cp);
            ri_count = (prop == GBP::Regional_Indicator) ? 1 : 0;
            seen_ext_pic = (prop == GBP::Extended_Pictographic);
        }

        prev_cp = cp;
    }

    return clusters;
}

} // namespace termcore
