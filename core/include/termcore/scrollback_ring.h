#ifndef TERMCORE_SCROLLBACK_RING_H
#define TERMCORE_SCROLLBACK_RING_H

#include "termcore/cell_types.h"
#include "termcore/grapheme_store.h"
#include "termcore/term_cell.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace termcore {

/// A fixed-size segment holding up to kRowsPerSegment rows of CpuCell/GpuCell data.
/// Cell arrays are allocated via VirtualAlloc (Windows) or mmap (Unix) for
/// zero-init and efficient page-level memory management.
struct ScrollbackSegment {
    static constexpr int kRowsPerSegment = 2048;

    CpuCell* cpu_cells = nullptr;   // flat: kRowsPerSegment * col_count
    GpuCell* gpu_cells = nullptr;   // flat: kRowsPerSegment * col_count
    uint16_t* row_occupancy = nullptr; // kRowsPerSegment entries
    GraphemeStore grapheme_store;
    std::unordered_map<uint32_t, uint32_t> underline_colors; // sparse (row*MAX_COLS+col) -> color
    int col_count = 0;
    int used_rows = 0;

    /// Allocate a new segment with VirtualAlloc/mmap-backed cell arrays.
    static ScrollbackSegment* allocate(int cols);

    /// Free segment and release all memory.
    static void deallocate(ScrollbackSegment* seg);

    // --- Cell access ---
    CpuCell& cpuCellAt(int row, int col) { return cpu_cells[row * col_count + col]; }
    const CpuCell& cpuCellAt(int row, int col) const { return cpu_cells[row * col_count + col]; }

    GpuCell& gpuCellAt(int row, int col) { return gpu_cells[row * col_count + col]; }
    const GpuCell& gpuCellAt(int row, int col) const { return gpu_cells[row * col_count + col]; }

    /// Reconstruct a TermCell from stored CpuCell + GpuCell + extras.
    TermCell cellAt(int row, int col) const;

    /// Write a vector of TermCells into the segment at the given row index.
    void writeRow(int row, const std::vector<TermCell>& cells);

    /// Get the text content of a row (for search), trimmed of trailing spaces.
    std::string rowText(int row) const;

private:
    /// Platform-specific allocation helpers for flat cell arrays.
    static void* platformAlloc(size_t bytes);
    static void platformFree(void* ptr, size_t bytes);
};

/// Ring buffer of ScrollbackSegments providing O(1) push and eviction.
///
/// Rows are indexed linearly: 0 = oldest visible row, size()-1 = newest.
/// When capacity is exceeded, the oldest segment's rows are evicted.
class ScrollbackRing {
public:
    explicit ScrollbackRing(int cols, size_t max_rows = 10000);
    ~ScrollbackRing();

    // Non-copyable
    ScrollbackRing(const ScrollbackRing&) = delete;
    ScrollbackRing& operator=(const ScrollbackRing&) = delete;

    // Movable
    ScrollbackRing(ScrollbackRing&& other) noexcept;
    ScrollbackRing& operator=(ScrollbackRing&& other) noexcept;

    /// Push a row from the live grid into scrollback.
    void pushRow(const std::vector<TermCell>& row_cells);

    /// Access a scrollback cell (0 = oldest, size()-1 = newest).
    TermCell cellAt(int scrollback_idx, int col) const;

    /// Get text for a scrollback row (for search).
    std::string rowText(int scrollback_idx) const;

    /// Current number of rows in the ring.
    size_t size() const { return total_rows_; }

    /// Maximum row capacity.
    size_t maxRows() const { return max_rows_; }

    /// Change the maximum capacity. May evict rows if shrinking.
    void setMaxRows(size_t max);

    /// Total number of rows evicted since creation (for monotonic row tracking).
    int64_t evictedCount() const { return evicted_count_; }

    /// Column count used for new segments.
    int cols() const { return cols_; }

    /// Clear all rows and free all segments.
    void clear();

private:
    std::vector<ScrollbackSegment*> segments_;
    int cols_;
    size_t max_rows_;
    size_t total_rows_ = 0;
    int64_t evicted_count_ = 0;

    // Ring indices
    int head_segment_ = 0;  // oldest segment
    int head_row_ = 0;      // first valid row within head segment
    int tail_segment_ = 0;  // current write segment
    int tail_row_ = 0;      // next write position within tail segment

    /// Resolve a linear index (0-based from oldest) to (segment_index, row_in_segment).
    std::pair<int, int> resolveIndex(int linear_idx) const;

    /// Evict the oldest `count` rows.
    void evict(int count);

    /// Allocate and append a new segment.
    void allocateNewSegment();
};

} // namespace termcore

#endif // TERMCORE_SCROLLBACK_RING_H
