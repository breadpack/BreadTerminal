#ifndef TERMCORE_CLIPBOARD_HISTORY_H
#define TERMCORE_CLIPBOARD_HISTORY_H

#include <chrono>
#include <functional>
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

    /// Set configurable maximum number of entries (overrides kMaxEntries).
    void setMaxEntries(size_t n) { max_entries_ = n; }

    /// Set configurable preview maximum length (overrides kPreviewMaxLength).
    void setPreviewMaxLength(size_t n) { preview_max_length_ = n; }

    /// Get current max entries limit.
    size_t maxEntries() const { return max_entries_; }

    /// Get current preview max length.
    size_t previewMaxLength() const { return preview_max_length_; }

    /// Copy callback: called each time an entry is added.
    std::function<void(const std::string&)> onCopyCallback;

private:
    static std::string makePreview(const std::string& text, size_t maxLen);

    std::vector<ClipboardEntry> entries_;
    size_t max_entries_ = kMaxEntries;
    size_t preview_max_length_ = kPreviewMaxLength;
};

} // namespace termcore

#endif
