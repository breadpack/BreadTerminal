#include "termcore/clipboard_history.h"

#include <algorithm>

namespace termcore {

void ClipboardHistory::addEntry(const std::string& text) {
    if (text.empty()) return;

    // Deduplicate: remove existing entry with same text
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                        [&text](const ClipboardEntry& e) { return e.text == text; }),
        entries_.end());

    // Insert at front
    ClipboardEntry entry;
    entry.text = text;
    entry.preview = makePreview(text, preview_max_length_);
    entry.timestamp = std::chrono::system_clock::now();
    entries_.insert(entries_.begin(), std::move(entry));

    // Enforce capacity
    if (entries_.size() > max_entries_) {
        entries_.resize(max_entries_);
    }

    // Fire copy callback
    if (onCopyCallback) {
        onCopyCallback(text);
    }
}

const std::vector<ClipboardEntry>& ClipboardHistory::getEntries() const {
    return entries_;
}

std::string ClipboardHistory::getEntry(int index) const {
    if (index < 0 || index >= static_cast<int>(entries_.size())) {
        return {};
    }
    return entries_[index].text;
}

void ClipboardHistory::clear() {
    entries_.clear();
}

size_t ClipboardHistory::size() const {
    return entries_.size();
}

std::string ClipboardHistory::makePreview(const std::string& text, size_t maxLen) {
    // Take first line only, then truncate to maxLen
    std::string preview;
    for (char c : text) {
        if (c == '\n' || c == '\r') break;
        preview += c;
        if (preview.size() >= maxLen) break;
    }
    if (preview.size() >= maxLen || preview.size() < text.size()) {
        // Indicate truncation if we stopped early
        if (preview.size() >= maxLen) {
            preview.resize(maxLen);
        }
        if (preview.size() < text.size()) {
            preview += "...";
        }
    }
    return preview;
}

} // namespace termcore
