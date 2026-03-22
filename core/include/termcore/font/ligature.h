#ifndef TERMCORE_FONT_LIGATURE_H
#define TERMCORE_FONT_LIGATURE_H

#include "font_metrics.h"
#include "font_shaper.h"
#include <cstdint>
#include <string>
#include <vector>

// Forward declare HarfBuzz types
typedef struct hb_font_t hb_font_t;

namespace termcore {

/// A span of codepoints that may form a ligature
struct LigatureSpan {
    int start_col;                    // First column in the row
    int end_col;                      // One past the last column
    std::vector<uint32_t> codepoints; // The codepoints forming the ligature
};

/// Result of shaping a ligature span
struct LigatureShapingResult {
    std::vector<ShapedGlyph> glyphs;  // Shaped glyphs for the ligature
    int cell_count;                    // Number of terminal cells this occupies
};

/// Detects and shapes programming ligatures using HarfBuzz GSUB tables.
///
/// Programming fonts like Fira Code, JetBrains Mono, Cascadia Code, etc.
/// contain OpenType substitution rules (liga/calt) that replace multi-character
/// sequences like "!=", "=>", "->" with single ligature glyphs.
///
/// This class scans rows of codepoints for candidate sequences and uses
/// HarfBuzz to verify if the loaded font actually supports the ligature.
class LigatureDetector {
public:
    LigatureDetector();
    ~LigatureDetector();

    // Non-copyable
    LigatureDetector(const LigatureDetector&) = delete;
    LigatureDetector& operator=(const LigatureDetector&) = delete;

    /// Scan a row of codepoints and identify multi-char sequences that
    /// should be shaped together as ligatures.
    /// @param codepoints The codepoints in the row
    /// @param row_start_col The column offset for the row (usually 0)
    /// @return Vector of ligature spans found in the row
    std::vector<LigatureSpan> detectLigatures(
        const std::vector<uint32_t>& codepoints,
        int row_start_col) const;

    /// Check if the font loaded in the given shaper has ligature support
    /// (i.e., has 'liga' or 'calt' OpenType features).
    /// @param shaper The font shaper with a loaded font
    /// @param face_id The font face to check
    /// @return true if the font has ligature features
    static bool hasLigatureSupport(FontShaper& shaper, FontFaceId face_id);

    /// Shape a ligature span through HarfBuzz and return glyph IDs + positions.
    /// @param shaper The font shaper to use
    /// @param face_id The font face to shape with
    /// @param span The ligature span to shape
    /// @param config Shaping configuration
    /// @return Shaping result with glyphs and cell count
    static LigatureShapingResult shapeLigature(
        FontShaper& shaper,
        FontFaceId face_id,
        const LigatureSpan& span,
        const ShaperConfig& config = {});

private:
    /// Common programming ligature candidate sequences
    struct CandidateSequence {
        const char32_t* chars;
        int length;
    };

    static const std::vector<CandidateSequence>& getCandidates();

    /// Check if a sequence starting at pos matches a candidate
    static bool matchesAt(const std::vector<uint32_t>& codepoints,
                          size_t pos,
                          const CandidateSequence& candidate);
};

} // namespace termcore
#endif
