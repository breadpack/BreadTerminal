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

/// Per-segment color table for compact color storage.
///
/// GpuCell stores 8-bit color indices instead of full 32-bit colors.
/// Index 0 = kColorDefault (terminal default).
/// Index 1-254 = entries in the color_table vector.
/// Index 255 = overflow (color stored in overflow_colors sparse map).
///
/// Typical segments use < 50 unique colors, so overflow is rare.
class SegmentColorTable {
public:
    /// Register a color and return its 8-bit index.
    /// Returns kDefaultColorIdx (0) for kColorDefault.
    /// Returns kOverflowIdx (255) if the table is full; caller must
    /// store the color in the overflow map.
    uint8_t intern(uint32_t color) {
        if (color == kColorDefault) return GpuCell::kDefaultColorIdx;

        // Fast path: check if same as last interned color (very common
        // when consecutive cells share the same foreground/background)
        if (color == last_color_) return last_idx_;

        // Check if color is already in the table
        auto it = color_to_idx_.find(color);
        if (it != color_to_idx_.end()) {
            last_color_ = color;
            last_idx_ = it->second;
            return it->second;
        }

        // Add new color if space available (indices 1-254)
        if (colors_.size() >= 254) return GpuCell::kOverflowIdx;

        uint8_t idx = static_cast<uint8_t>(colors_.size() + 1); // 1-based
        colors_.push_back(color);
        color_to_idx_[color] = idx;
        last_color_ = color;
        last_idx_ = idx;
        return idx;
    }

    /// Resolve a color index back to a uint32_t color.
    /// Index 0 returns kColorDefault. Index 255 (overflow) returns kColorDefault
    /// as fallback; caller should check overflow map for the actual color.
    uint32_t resolve(uint8_t idx) const {
        if (idx == GpuCell::kDefaultColorIdx) return kColorDefault;
        if (idx == GpuCell::kOverflowIdx) return kColorDefault; // caller handles overflow
        int table_idx = idx - 1; // 1-based to 0-based
        if (table_idx >= 0 && table_idx < static_cast<int>(colors_.size()))
            return colors_[table_idx];
        return kColorDefault; // safety fallback
    }

    /// Clear the table (for segment recycling).
    void clear() {
        colors_.clear();
        color_to_idx_.clear();
        last_color_ = kColorDefault;
        last_idx_ = GpuCell::kDefaultColorIdx;
    }

    /// Number of unique colors stored.
    size_t size() const { return colors_.size(); }

private:
    std::vector<uint32_t> colors_;                    // 0-based color storage
    std::unordered_map<uint32_t, uint8_t> color_to_idx_; // reverse lookup
    uint32_t last_color_ = kColorDefault;             // MRU cache for intern()
    uint8_t last_idx_ = GpuCell::kDefaultColorIdx;    // MRU cache result
};

/// A fixed-size segment holding up to kRowsPerSegment rows of CpuCell/GpuCell data.
/// Cell arrays are allocated via VirtualAlloc (Windows) or mmap (Unix) for
/// zero-init and efficient page-level memory management.
struct ScrollbackSegment {
    static constexpr int kRowsPerSegment = 2048;

    CpuCell* cpu_cells = nullptr;   // flat: kRowsPerSegment * col_count
    GpuCell* gpu_cells = nullptr;   // flat: kRowsPerSegment * col_count
    uint16_t* row_occupancy = nullptr; // kRowsPerSegment entries
    GraphemeStore grapheme_store;
    SegmentColorTable color_table;
    std::unordered_map<uint32_t, uint32_t> underline_colors; // sparse (row*MAX_COLS+col) -> color
    std::unordered_map<uint32_t, uint32_t> overflow_colors;  // sparse (row*MAX_COLS+col) -> color for overflow fg/bg
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

    /// Resolve a GpuCell's fg color to uint32_t, handling overflow.
    uint32_t resolveFgColor(int row, int col) const {
        const GpuCell& gpu = gpuCellAt(row, col);
        if (gpu.fg_color_idx == GpuCell::kOverflowIdx) {
            // Overflow key uses high bit to distinguish fg (bit 31 set) from bg
            uint32_t key = (static_cast<uint32_t>(row) * static_cast<uint32_t>(col_count)
                           + static_cast<uint32_t>(col)) | 0x80000000u;
            auto it = overflow_colors.find(key);
            return (it != overflow_colors.end()) ? it->second : kColorDefault;
        }
        return color_table.resolve(gpu.fg_color_idx);
    }

    /// Resolve a GpuCell's bg color to uint32_t, handling overflow.
    uint32_t resolveBgColor(int row, int col) const {
        const GpuCell& gpu = gpuCellAt(row, col);
        if (gpu.bg_color_idx == GpuCell::kOverflowIdx) {
            uint32_t key = static_cast<uint32_t>(row) * static_cast<uint32_t>(col_count)
                           + static_cast<uint32_t>(col);
            auto it = overflow_colors.find(key);
            return (it != overflow_colors.end()) ? it->second : kColorDefault;
        }
        return color_table.resolve(gpu.bg_color_idx);
    }

    /// Reconstruct a TermCell from stored CpuCell + GpuCell + extras.
    TermCell cellAt(int row, int col) const;

    /// Write a vector of TermCells into the segment at the given row index.
    void writeRow(int row, const std::vector<TermCell>& cells);

    /// Write with occupancy hint: only process cells up to occ_hint, zero the rest.
    /// occ_hint < 0 means "compute occupancy from cell data" (same as above overload).
    void writeRow(int row, const std::vector<TermCell>& cells, int occ_hint);

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

    /// Push a row with occupancy hint (number of non-default cells from left).
    /// Cells beyond occ_hint are assumed to be default and skipped during copy.
    void pushRow(const std::vector<TermCell>& row_cells, int occ_hint);

    /// Access a scrollback cell (0 = oldest, size()-1 = newest).
    TermCell cellAt(int scrollback_idx, int col) const;

    /// Direct row access: returns raw CpuCell/GpuCell pointers for a scrollback row.
    /// This avoids per-cell TermCell reconstruction overhead for rendering/iteration.
    /// Returns false if the index is out of range.
    ///
    /// NOTE: GpuCell stores color indices, not raw colors. Use segment->resolveFgColor()
    /// and segment->resolveBgColor() or segment->color_table.resolve() to get uint32_t colors.
    struct RowView {
        const CpuCell* cpu;
        const GpuCell* gpu;
        const ScrollbackSegment* segment;
        int row_in_segment;
        int col_count;
    };
    bool rowAt(int scrollback_idx, RowView& out) const;

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
