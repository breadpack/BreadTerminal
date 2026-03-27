#ifndef TERMCORE_GRAPHEME_STORE_H
#define TERMCORE_GRAPHEME_STORE_H

#include "termcore/term_cell.h" // for kMaxExtraCodepoints
#include <array>
#include <cstdint>
#include <vector>

namespace termcore {

/// Externalized storage for grapheme cluster extra codepoints.
///
/// When cells are split into CpuCell/GpuCell, the inline extra[7] array
/// from TermCell is too large to keep per-cell. Instead, extra codepoints
/// are stored here and referenced by a 16-bit index in CpuCell::grapheme_idx.
///
/// Index 0 is reserved (means "no extras"). Valid indices start at 1.
class GraphemeStore {
public:
    /// Store extra codepoints for a grapheme cluster.
    /// Returns a non-zero index on success, or 0 if the store is full (65535 entries max).
    uint16_t store(const char32_t* extras, uint8_t count) {
        if (count == 0) return 0;
        if (entries_.size() >= 65535) return 0; // index space exhausted

        Entry entry{};
        uint8_t n = count < kMaxExtraCodepoints ? count : kMaxExtraCodepoints;
        for (uint8_t i = 0; i < n; ++i) {
            entry.codepoints[i] = extras[i];
        }
        entry.count = n;
        entries_.push_back(entry);
        return static_cast<uint16_t>(entries_.size()); // 1-based index
    }

    /// Retrieve the extra codepoints for a given index.
    /// Returns nullptr and sets out_count=0 if idx is 0 or out of range.
    const char32_t* get(uint16_t idx, uint8_t& out_count) const {
        if (idx == 0 || idx > entries_.size()) {
            out_count = 0;
            return nullptr;
        }
        const auto& e = entries_[idx - 1]; // 1-based to 0-based
        out_count = e.count;
        return e.codepoints.data();
    }

    /// Clear all stored grapheme data (e.g., when a segment is recycled).
    void clear() { entries_.clear(); }

    /// Number of stored grapheme entries.
    size_t size() const { return entries_.size(); }

    /// Approximate memory usage in bytes.
    size_t memoryUsage() const {
        return sizeof(GraphemeStore) + entries_.capacity() * sizeof(Entry);
    }

private:
    struct Entry {
        std::array<char32_t, kMaxExtraCodepoints> codepoints{};
        uint8_t count = 0;
    };

    std::vector<Entry> entries_;
};

} // namespace termcore

#endif // TERMCORE_GRAPHEME_STORE_H
