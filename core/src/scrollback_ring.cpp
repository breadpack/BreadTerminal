#include "termcore/scrollback_ring.h"
#include "termcore/font/unicode_width.h"
#include <algorithm>
#include <cassert>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <sys/mman.h>
#endif

namespace termcore {

// ─── Platform allocation ────────────────────────────────────────────────────

void* ScrollbackSegment::platformAlloc(size_t bytes) {
    if (bytes == 0) return nullptr;
#ifdef _WIN32
    void* ptr = VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    void* ptr = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) ptr = nullptr;
#endif
    return ptr;
}

void ScrollbackSegment::platformFree(void* ptr, size_t bytes) {
    if (!ptr) return;
#ifdef _WIN32
    (void)bytes;
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, bytes);
#endif
}

// ─── ScrollbackSegment ─────────────────────────────────────────────────────

ScrollbackSegment* ScrollbackSegment::allocate(int cols) {
    auto* seg = new ScrollbackSegment();
    seg->col_count = cols;
    seg->used_rows = 0;

    size_t cpu_bytes = static_cast<size_t>(kRowsPerSegment) * cols * sizeof(CpuCell);
    size_t gpu_bytes = static_cast<size_t>(kRowsPerSegment) * cols * sizeof(GpuCell);
    size_t occ_bytes = static_cast<size_t>(kRowsPerSegment) * sizeof(uint16_t);

    seg->cpu_cells = static_cast<CpuCell*>(platformAlloc(cpu_bytes));
    seg->gpu_cells = static_cast<GpuCell*>(platformAlloc(gpu_bytes));
    seg->row_occupancy = static_cast<uint16_t*>(platformAlloc(occ_bytes));

    // VirtualAlloc/mmap returns zero-initialized memory, so CpuCell/GpuCell
    // fields are already zeroed. For correctness, set default fg/bg colors
    // only when actually writing rows (writeRow handles this).

    return seg;
}

void ScrollbackSegment::deallocate(ScrollbackSegment* seg) {
    if (!seg) return;

    size_t cpu_bytes = static_cast<size_t>(kRowsPerSegment) * seg->col_count * sizeof(CpuCell);
    size_t gpu_bytes = static_cast<size_t>(kRowsPerSegment) * seg->col_count * sizeof(GpuCell);
    size_t occ_bytes = static_cast<size_t>(kRowsPerSegment) * sizeof(uint16_t);

    platformFree(seg->cpu_cells, cpu_bytes);
    platformFree(seg->gpu_cells, gpu_bytes);
    platformFree(seg->row_occupancy, occ_bytes);

    delete seg;
}

TermCell ScrollbackSegment::cellAt(int row, int col) const {
    if (col < 0 || col >= col_count) return TermCell{};

    const CpuCell& cpu = cpuCellAt(row, col);
    const GpuCell& gpu = gpuCellAt(row, col);

    TermCell tc = fromCells(cpu, gpu);

    // Resolve colors from segment color table
    tc.fg_color = resolveFgColor(row, col);
    tc.bg_color = resolveBgColor(row, col);

    // Restore grapheme extras from the store
    if (cpu.grapheme_idx != 0) {
        uint8_t count = 0;
        const char32_t* extras = grapheme_store.get(cpu.grapheme_idx, count);
        if (extras) {
            int n = (count < kMaxExtraCodepoints) ? count : kMaxExtraCodepoints;
            for (int i = 0; i < n; ++i) {
                tc.extra[i] = extras[i];
            }
            tc.extra_count = static_cast<uint8_t>(n);
        }
    }

    // Restore underline_color from sparse map
    uint32_t key = static_cast<uint32_t>(row) * static_cast<uint32_t>(col_count)
                   + static_cast<uint32_t>(col);
    auto it = underline_colors.find(key);
    if (it != underline_colors.end()) {
        tc.underline_color = it->second;
    }

    return tc;
}

void ScrollbackSegment::writeRow(int row, const std::vector<TermCell>& cells) {
    writeRow(row, cells, -1);
}

void ScrollbackSegment::writeRow(int row, const std::vector<TermCell>& cells, int occ_hint) {
    int ncols = std::min(static_cast<int>(cells.size()), col_count);

    // Pre-compute base pointers to avoid repeated row * col_count multiply
    const int base_offset = row * col_count;
    CpuCell* cpu_row = cpu_cells + base_offset;
    GpuCell* gpu_row = gpu_cells + base_offset;

    // If caller provided occupancy hint, only process up to that many cells.
    // Cells beyond occ_hint are known to be default (space, default colors, no attrs).
    int process_cols = ncols;
    int occ = 0;
    bool need_occ_calc = true;

    if (occ_hint >= 0) {
        // Trust the caller's occupancy hint
        process_cols = std::min(occ_hint, ncols);
        occ = process_cols;
        need_occ_calc = false;
    }

    // Track whether we need any rare-path processing
    bool has_extras = false;
    bool has_underline_colors = false;

    for (int c = 0; c < process_cols; ++c) {
        const TermCell& tc = cells[c];
        CpuCell& cpu = cpu_row[c];
        GpuCell& gpu = gpu_row[c];

        cpu.codepoint = tc.codepoint;
        cpu.width = tc.width;
        cpu.extra_count = tc.extra_count;
        cpu.grapheme_idx = 0;

        // Intern colors through the segment color table
        gpu.fg_color_idx = color_table.intern(tc.fg_color);
        gpu.bg_color_idx = color_table.intern(tc.bg_color);
        gpu.setAttributes(tc.attributes);
        gpu.setUnderlineStyle(tc.underline_style);

        // If color overflowed, store in overflow map
        if (gpu.fg_color_idx == GpuCell::kOverflowIdx) {
            uint32_t key = (static_cast<uint32_t>(row) * static_cast<uint32_t>(col_count)
                           + static_cast<uint32_t>(c)) | 0x80000000u;
            overflow_colors[key] = tc.fg_color;
        }
        if (gpu.bg_color_idx == GpuCell::kOverflowIdx) {
            uint32_t key = static_cast<uint32_t>(row) * static_cast<uint32_t>(col_count)
                           + static_cast<uint32_t>(c);
            overflow_colors[key] = tc.bg_color;
        }

        // Defer rare-path processing
        has_extras |= (tc.extra_count > 0);
        has_underline_colors |= (tc.underline_color != kColorDefault);

        // Track occupancy only if not provided by caller
        if (need_occ_calc) {
            if (tc.codepoint != ' ' || tc.extra_count > 0 ||
                tc.fg_color != kColorDefault || tc.bg_color != kColorDefault ||
                tc.attributes != 0) {
                occ = c + 1;
            }
        }
    }

    // Second pass for rare features (grapheme extras, underline colors)
    if (has_extras || has_underline_colors) {
        for (int c = 0; c < process_cols; ++c) {
            const TermCell& tc = cells[c];
            if (tc.extra_count > 0) {
                cpu_row[c].grapheme_idx = grapheme_store.store(tc.extra, tc.extra_count);
            }
            if (tc.underline_color != kColorDefault) {
                uint32_t key = static_cast<uint32_t>(row) * static_cast<uint32_t>(col_count)
                               + static_cast<uint32_t>(c);
                underline_colors[key] = tc.underline_color;
            }
        }
    }

    // Zero remaining columns beyond processed range
    if (process_cols < col_count) {
        std::memset(cpu_row + process_cols, 0,
                    static_cast<size_t>(col_count - process_cols) * sizeof(CpuCell));
        std::memset(gpu_row + process_cols, 0,
                    static_cast<size_t>(col_count - process_cols) * sizeof(GpuCell));
    }

    row_occupancy[row] = static_cast<uint16_t>(occ);
    if (row >= used_rows) {
        used_rows = row + 1;
    }
}

std::string ScrollbackSegment::rowText(int row) const {
    std::string result;
    int occ = row_occupancy[row];
    int limit = std::min(occ, col_count);

    for (int c = 0; c < limit; ++c) {
        const CpuCell& cpu = cpuCellAt(row, c);

        // Skip continuation cells (width == 0 and codepoint == 0)
        if (cpu.codepoint == 0 && cpu.width == 0) continue;

        utf8_encode(cpu.codepoint, result);

        // Append grapheme extras
        if (cpu.grapheme_idx != 0) {
            uint8_t count = 0;
            const char32_t* extras = grapheme_store.get(cpu.grapheme_idx, count);
            if (extras) {
                for (uint8_t i = 0; i < count; ++i) {
                    utf8_encode(extras[i], result);
                }
            }
        }
    }

    // Trim trailing spaces
    auto pos = result.find_last_not_of(' ');
    if (pos != std::string::npos) {
        result.erase(pos + 1);
    } else {
        result.clear();
    }
    return result;
}

// ─── ScrollbackRing ─────────────────────────────────────────────────────────

ScrollbackRing::ScrollbackRing(int cols, size_t max_rows)
    : cols_(cols), max_rows_(max_rows)
{
    // Pre-allocate one segment
    allocateNewSegment();
}

ScrollbackRing::~ScrollbackRing() {
    for (auto* seg : segments_) {
        ScrollbackSegment::deallocate(seg);
    }
}

ScrollbackRing::ScrollbackRing(ScrollbackRing&& other) noexcept
    : segments_(std::move(other.segments_))
    , cols_(other.cols_)
    , max_rows_(other.max_rows_)
    , total_rows_(other.total_rows_)
    , evicted_count_(other.evicted_count_)
    , head_segment_(other.head_segment_)
    , head_row_(other.head_row_)
    , tail_segment_(other.tail_segment_)
    , tail_row_(other.tail_row_)
{
    other.total_rows_ = 0;
    other.evicted_count_ = 0;
    other.head_segment_ = 0;
    other.head_row_ = 0;
    other.tail_segment_ = 0;
    other.tail_row_ = 0;
}

ScrollbackRing& ScrollbackRing::operator=(ScrollbackRing&& other) noexcept {
    if (this != &other) {
        for (auto* seg : segments_) {
            ScrollbackSegment::deallocate(seg);
        }
        segments_ = std::move(other.segments_);
        cols_ = other.cols_;
        max_rows_ = other.max_rows_;
        total_rows_ = other.total_rows_;
        evicted_count_ = other.evicted_count_;
        head_segment_ = other.head_segment_;
        head_row_ = other.head_row_;
        tail_segment_ = other.tail_segment_;
        tail_row_ = other.tail_row_;

        other.total_rows_ = 0;
        other.evicted_count_ = 0;
        other.head_segment_ = 0;
        other.head_row_ = 0;
        other.tail_segment_ = 0;
        other.tail_row_ = 0;
    }
    return *this;
}

void ScrollbackRing::allocateNewSegment() {
    segments_.push_back(ScrollbackSegment::allocate(cols_));
}

std::pair<int, int> ScrollbackRing::resolveIndex(int linear_idx) const {
    assert(linear_idx >= 0 && static_cast<size_t>(linear_idx) < total_rows_);

    int offset_from_head = linear_idx + head_row_;
    int seg_offset = offset_from_head / ScrollbackSegment::kRowsPerSegment;
    int row_in_seg = offset_from_head % ScrollbackSegment::kRowsPerSegment;
    int seg_idx = (head_segment_ + seg_offset) % static_cast<int>(segments_.size());

    return { seg_idx, row_in_seg };
}

void ScrollbackRing::evict(int count) {
    if (count <= 0) return;
    int to_evict = std::min(count, static_cast<int>(total_rows_));

    evicted_count_ += to_evict;
    total_rows_ -= to_evict;

    // Advance head_row_ within the head segment
    head_row_ += to_evict;

    // If head_row_ moved past one or more full segments, recycle them
    while (head_row_ >= ScrollbackSegment::kRowsPerSegment && segments_.size() > 1) {
        // Recycle the head segment: clear its data for potential reuse
        auto* old_seg = segments_[head_segment_];

        // Reset the segment's state
        old_seg->used_rows = 0;
        old_seg->grapheme_store.clear();
        old_seg->color_table.clear();
        old_seg->underline_colors.clear();
        old_seg->overflow_colors.clear();

        // Zero the cell arrays - reuse the VirtualAlloc memory
        size_t cpu_bytes = static_cast<size_t>(ScrollbackSegment::kRowsPerSegment)
                           * old_seg->col_count * sizeof(CpuCell);
        size_t gpu_bytes = static_cast<size_t>(ScrollbackSegment::kRowsPerSegment)
                           * old_seg->col_count * sizeof(GpuCell);
        size_t occ_bytes = static_cast<size_t>(ScrollbackSegment::kRowsPerSegment)
                           * sizeof(uint16_t);
        std::memset(old_seg->cpu_cells, 0, cpu_bytes);
        std::memset(old_seg->gpu_cells, 0, gpu_bytes);
        std::memset(old_seg->row_occupancy, 0, occ_bytes);

        head_row_ -= ScrollbackSegment::kRowsPerSegment;

        // Move head to next segment (wrap around in the logical vector)
        // We remove the old segment from front and keep it for potential reuse
        // by moving it to end (but only if col_count matches)
        head_segment_ = (head_segment_ + 1) % static_cast<int>(segments_.size());

        // Actually remove the segment if we have enough capacity
        // For simplicity, just deallocate and remove it
        // Find the old segment's position in the vector
        // Since head_segment_ just advanced, the old segment is at
        // (head_segment_ - 1 + segments_.size()) % segments_.size()
        int old_idx = (head_segment_ - 1 + static_cast<int>(segments_.size()))
                      % static_cast<int>(segments_.size());

        // Deallocate and remove from vector
        ScrollbackSegment::deallocate(segments_[old_idx]);
        segments_.erase(segments_.begin() + old_idx);

        // Adjust indices after erasure
        if (old_idx < head_segment_) {
            head_segment_--;
        }
        if (old_idx < tail_segment_) {
            tail_segment_--;
        } else if (old_idx == tail_segment_) {
            // This shouldn't happen if we have more than 1 segment
            // and tail != head, but guard anyway
            tail_segment_ = std::max(0, tail_segment_ - 1);
        }
    }
}

void ScrollbackRing::pushRow(const std::vector<TermCell>& row_cells) {
    pushRow(row_cells, -1);
}

void ScrollbackRing::pushRow(const std::vector<TermCell>& row_cells, int occ_hint) {
    // When max capacity is zero, scrollback is disabled — discard immediately.
    if (max_rows_ == 0) return;

    // Check if we need to evict rows to stay under max
    if (total_rows_ >= max_rows_) {
        evict(1);
    }

    // Check if current tail segment is full
    if (tail_row_ >= ScrollbackSegment::kRowsPerSegment) {
        allocateNewSegment();
        tail_segment_ = static_cast<int>(segments_.size()) - 1;
        tail_row_ = 0;
    }

    // Write the row
    segments_[tail_segment_]->writeRow(tail_row_, row_cells, occ_hint);
    tail_row_++;
    total_rows_++;
}

TermCell ScrollbackRing::cellAt(int scrollback_idx, int col) const {
    if (scrollback_idx < 0 || static_cast<size_t>(scrollback_idx) >= total_rows_) {
        return TermCell{};
    }

    auto [seg_idx, row_in_seg] = resolveIndex(scrollback_idx);
    return segments_[seg_idx]->cellAt(row_in_seg, col);
}

bool ScrollbackRing::rowAt(int scrollback_idx, RowView& out) const {
    if (scrollback_idx < 0 || static_cast<size_t>(scrollback_idx) >= total_rows_) {
        return false;
    }

    auto [seg_idx, row_in_seg] = resolveIndex(scrollback_idx);
    const auto* seg = segments_[seg_idx];
    int offset = row_in_seg * seg->col_count;
    out.cpu = seg->cpu_cells + offset;
    out.gpu = seg->gpu_cells + offset;
    out.segment = seg;
    out.row_in_segment = row_in_seg;
    out.col_count = seg->col_count;
    return true;
}

std::string ScrollbackRing::rowText(int scrollback_idx) const {
    if (scrollback_idx < 0 || static_cast<size_t>(scrollback_idx) >= total_rows_) {
        return "";
    }

    auto [seg_idx, row_in_seg] = resolveIndex(scrollback_idx);
    return segments_[seg_idx]->rowText(row_in_seg);
}

void ScrollbackRing::setMaxRows(size_t max) {
    max_rows_ = max;
    if (total_rows_ > max_rows_) {
        evict(static_cast<int>(total_rows_ - max_rows_));
    }
}

void ScrollbackRing::clear() {
    evicted_count_ += static_cast<int64_t>(total_rows_);
    total_rows_ = 0;

    // Deallocate all segments except one (keep one for reuse)
    while (segments_.size() > 1) {
        ScrollbackSegment::deallocate(segments_.back());
        segments_.pop_back();
    }

    // Reset the remaining segment
    if (!segments_.empty()) {
        auto* seg = segments_[0];
        seg->used_rows = 0;
        seg->grapheme_store.clear();
        seg->color_table.clear();
        seg->underline_colors.clear();
        seg->overflow_colors.clear();

        size_t cpu_bytes = static_cast<size_t>(ScrollbackSegment::kRowsPerSegment)
                           * seg->col_count * sizeof(CpuCell);
        size_t gpu_bytes = static_cast<size_t>(ScrollbackSegment::kRowsPerSegment)
                           * seg->col_count * sizeof(GpuCell);
        size_t occ_bytes = static_cast<size_t>(ScrollbackSegment::kRowsPerSegment)
                           * sizeof(uint16_t);
        std::memset(seg->cpu_cells, 0, cpu_bytes);
        std::memset(seg->gpu_cells, 0, gpu_bytes);
        std::memset(seg->row_occupancy, 0, occ_bytes);
    }

    head_segment_ = 0;
    head_row_ = 0;
    tail_segment_ = 0;
    tail_row_ = 0;
}

} // namespace termcore
