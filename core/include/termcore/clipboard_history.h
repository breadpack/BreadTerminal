#ifndef TERMCORE_CLIPBOARD_HISTORY_H
#define TERMCORE_CLIPBOARD_HISTORY_H

#include <chrono>
#include <string>
#include <vector>

namespace termcore {

/// A single clipboard history entry
struct ClipboardEntry {
    std::string text;      // Full clipboard text
    std::string preview;   // Truncated preview (first 80 chars, single line)
    std::chrono::system_clock::time_point timestamp;
};

/// Stores the last N copied items for clipboard history selection.
class ClipboardHistory {
public:
    static constexpr size_t kMaxEntries = 20;
    static constexpr size_t kPreviewMaxLength = 80;

    /// Add text to the front of history. Deduplicates (moves existing to front).
    void addEntry(const std::string& text);

    /// Get all entries (most recent first).
    const std::vector<ClipboardEntry>& getEntries() const;

    /// Get entry by index. Returns empty string if out of range.
    std::string getEntry(int index) const;

    /// Clear all history.
    void clear();

    /// Number of entries.
    size_t size() const;

private:
    static std::string makePreview(const std::string& text);

    std::vector<ClipboardEntry> entries_;
};

} // namespace termcore

#endif
