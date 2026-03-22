#include "termcore/font/ligature.h"
#include "termcore/font/font_shaper.h"

#include <hb.h>

#include <algorithm>
#include <cstring>

namespace termcore {

// ---- Candidate ligature sequences (sorted longest-first for greedy matching) ----

// Static storage for candidate character arrays
static const char32_t kSeq_arrow_fat[]       = { '=', '>' };
static const char32_t kSeq_arrow_thin[]      = { '-', '>' };
static const char32_t kSeq_arrow_long[]      = { '-', '-', '>' };
static const char32_t kSeq_arrow_left[]      = { '<', '-' };
static const char32_t kSeq_arrow_left_long[] = { '<', '-', '-' };
static const char32_t kSeq_neq[]             = { '!', '=' };
static const char32_t kSeq_neq_strict[]      = { '!', '=', '=' };
static const char32_t kSeq_gte[]             = { '>', '=' };
static const char32_t kSeq_lte[]             = { '<', '=' };
static const char32_t kSeq_eq2[]             = { '=', '=' };
static const char32_t kSeq_eq3[]             = { '=', '=', '=' };
static const char32_t kSeq_scope[]           = { ':', ':' };
static const char32_t kSeq_dot2[]            = { '.', '.' };
static const char32_t kSeq_dot3[]            = { '.', '.', '.' };
static const char32_t kSeq_and[]             = { '&', '&' };
static const char32_t kSeq_or[]              = { '|', '|' };
static const char32_t kSeq_pipe_fwd[]        = { '|', '>' };
static const char32_t kSeq_pipe_bwd[]        = { '<', '|' };
static const char32_t kSeq_shr[]             = { '>', '>' };
static const char32_t kSeq_shl[]             = { '<', '<' };
static const char32_t kSeq_inc[]             = { '+', '+' };
static const char32_t kSeq_dec[]             = { '-', '-' };
static const char32_t kSeq_comment_open[]    = { '/', '*' };
static const char32_t kSeq_comment_close[]   = { '*', '/' };
static const char32_t kSeq_line_comment[]    = { '/', '/' };
static const char32_t kSeq_doc_comment[]     = { '/', '*', '*' };
static const char32_t kSeq_jsx_close[]       = { '<', '/', '>' };
static const char32_t kSeq_tilde2[]          = { '~', '~' };
static const char32_t kSeq_tilde_arrow[]     = { '~', '>' };
static const char32_t kSeq_www[]             = { 'w', 'w', 'w' };

const std::vector<LigatureDetector::CandidateSequence>&
LigatureDetector::getCandidates() {
    // Sorted by length descending so longer matches are tried first
    static const std::vector<CandidateSequence> candidates = {
        // 3-char sequences
        { kSeq_neq_strict,      3 },
        { kSeq_eq3,             3 },
        { kSeq_arrow_long,      3 },
        { kSeq_arrow_left_long, 3 },
        { kSeq_dot3,            3 },
        { kSeq_doc_comment,     3 },
        { kSeq_jsx_close,       3 },
        { kSeq_www,             3 },
        // 2-char sequences
        { kSeq_arrow_fat,       2 },
        { kSeq_arrow_thin,      2 },
        { kSeq_arrow_left,      2 },
        { kSeq_neq,             2 },
        { kSeq_gte,             2 },
        { kSeq_lte,             2 },
        { kSeq_eq2,             2 },
        { kSeq_scope,           2 },
        { kSeq_dot2,            2 },
        { kSeq_and,             2 },
        { kSeq_or,              2 },
        { kSeq_pipe_fwd,        2 },
        { kSeq_pipe_bwd,        2 },
        { kSeq_shr,             2 },
        { kSeq_shl,             2 },
        { kSeq_inc,             2 },
        { kSeq_dec,             2 },
        { kSeq_comment_open,    2 },
        { kSeq_comment_close,   2 },
        { kSeq_line_comment,    2 },
        { kSeq_tilde2,          2 },
        { kSeq_tilde_arrow,     2 },
    };
    return candidates;
}

LigatureDetector::LigatureDetector() = default;
LigatureDetector::~LigatureDetector() = default;

bool LigatureDetector::matchesAt(const std::vector<uint32_t>& codepoints,
                                  size_t pos,
                                  const CandidateSequence& candidate) {
    if (pos + static_cast<size_t>(candidate.length) > codepoints.size()) {
        return false;
    }
    for (int i = 0; i < candidate.length; ++i) {
        if (codepoints[pos + i] != static_cast<uint32_t>(candidate.chars[i])) {
            return false;
        }
    }
    return true;
}

std::vector<LigatureSpan> LigatureDetector::detectLigatures(
    const std::vector<uint32_t>& codepoints,
    int row_start_col) const {

    std::vector<LigatureSpan> spans;
    if (codepoints.empty()) return spans;

    const auto& candidates = getCandidates();
    size_t pos = 0;

    while (pos < codepoints.size()) {
        bool found = false;

        // Try candidates longest-first (they are already sorted by length desc)
        for (const auto& candidate : candidates) {
            if (matchesAt(codepoints, pos, candidate)) {
                LigatureSpan span;
                span.start_col = row_start_col + static_cast<int>(pos);
                span.end_col = span.start_col + candidate.length;
                span.codepoints.assign(
                    codepoints.begin() + pos,
                    codepoints.begin() + pos + candidate.length);
                spans.push_back(std::move(span));

                pos += static_cast<size_t>(candidate.length);
                found = true;
                break;
            }
        }

        if (!found) {
            ++pos;
        }
    }

    return spans;
}

bool LigatureDetector::hasLigatureSupport(FontShaper& shaper,
                                           FontFaceId face_id) {
    // Shape a known ligature candidate with and without ligatures enabled.
    // If the glyph IDs differ, the font has ligature support.
    std::u32string test_str = U"!=";

    ShaperConfig with_lig;
    with_lig.enable_ligatures = true;
    with_lig.enable_liga = true;

    ShaperConfig without_lig;
    without_lig.enable_ligatures = false;
    without_lig.enable_liga = false;

    auto glyphs_with = shaper.shape(face_id, test_str, with_lig);
    auto glyphs_without = shaper.shape(face_id, test_str, without_lig);

    // If different number of glyphs or different glyph IDs, font has ligatures
    if (glyphs_with.size() != glyphs_without.size()) {
        return true;
    }

    for (size_t i = 0; i < glyphs_with.size(); ++i) {
        if (glyphs_with[i].glyph_index != glyphs_without[i].glyph_index) {
            return true;
        }
    }

    // Also try "=>" as another common ligature
    test_str = U"=>";
    glyphs_with = shaper.shape(face_id, test_str, with_lig);
    glyphs_without = shaper.shape(face_id, test_str, without_lig);

    if (glyphs_with.size() != glyphs_without.size()) {
        return true;
    }
    for (size_t i = 0; i < glyphs_with.size(); ++i) {
        if (glyphs_with[i].glyph_index != glyphs_without[i].glyph_index) {
            return true;
        }
    }

    return false;
}

LigatureShapingResult LigatureDetector::shapeLigature(
    FontShaper& shaper,
    FontFaceId face_id,
    const LigatureSpan& span,
    const ShaperConfig& config) {

    LigatureShapingResult result;
    result.cell_count = span.end_col - span.start_col;

    if (span.codepoints.empty()) return result;

    // Convert to u32string for the shaper
    std::u32string text;
    text.reserve(span.codepoints.size());
    for (uint32_t cp : span.codepoints) {
        text.push_back(static_cast<char32_t>(cp));
    }

    // Shape with ligatures enabled (using the provided config)
    result.glyphs = shaper.shape(face_id, text, config);

    return result;
}

} // namespace termcore
