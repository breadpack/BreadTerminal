#ifndef TERMCORE_COMPRESSED_ROW_H
#define TERMCORE_COMPRESSED_ROW_H

#include "termcore/term_cell.h"  // for TermCell
#include <vector>
#include <cstdint>
#include <string>

namespace termcore {

/// A run of identical cells in a compressed row.
struct CellRun {
    TermCell cell;
    uint16_t count;  // Number of consecutive identical cells (1-65535)
};

/// RLE-compressed representation of a terminal row.
/// Used for scrollback lines which are rarely accessed cell-by-cell
/// but can contain long runs of identical cells (especially spaces).
class CompressedRow {
public:
    /// Compress from a regular row of cells.
    void compress(const std::vector<TermCell>& row);

    /// Decompress to a regular row, resized to `cols` cells.
    void decompress(std::vector<TermCell>& out, int cols) const;

    /// Get a single cell by column index (slower than bulk decompress).
    TermCell cellAt(int col) const;

    /// Get the text content of the row (for search), trimmed of trailing spaces.
    std::string text(int cols) const;

    /// Total number of columns represented by this compressed row.
    int totalCols() const;

    /// Approximate memory usage in bytes.
    size_t memoryUsage() const;

    /// Number of runs (for diagnostics).
    size_t runCount() const { return runs_.size(); }

private:
    std::vector<CellRun> runs_;
};

} // namespace termcore

#endif // TERMCORE_COMPRESSED_ROW_H
