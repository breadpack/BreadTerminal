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
    entry.preview = makePreview(text);
    entry.timestamp = std::chrono::system_clock::now();
    entries_.insert(entries_.begin(), std::move(entry));

    // Enforce capacity
    if (entries_.size() > kMaxEntries) {
        entries_.resize(kMaxEntries);
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

std::string ClipboardHistory::makePreview(const std::string& text) {
    // Take first line only, then truncate to kPreviewMaxLength
    std::string preview;
    for (char c : text) {
        if (c == '\n' || c == '\r') break;
        preview += c;
        if (preview.size() >= kPreviewMaxLength) break;
    }
    if (preview.size() >= kPreviewMaxLength || preview.size() < text.size()) {
        // Indicate truncation if we stopped early
        if (preview.size() >= kPreviewMaxLength) {
            preview.resize(kPreviewMaxLength);
        }
        if (preview.size() < text.size()) {
            preview += "...";
        }
    }
    return preview;
}

} // namespace termcore
