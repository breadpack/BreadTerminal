#include "termcore/terminal_inspector.h"

namespace termcore {

TerminalInspector::TerminalInspector() = default;

void TerminalInspector::setEnabled(bool enabled) {
    enabled_ = enabled;
}

bool TerminalInspector::isEnabled() const {
    return enabled_;
}

void TerminalInspector::toggle() {
    enabled_ = !enabled_;
}

void TerminalInspector::logText(const std::string& text) {
    if (!enabled_ || paused_) return;
    InspectorEntry entry;
    entry.type = InspectorEntry::Type::Text;
    entry.raw = text;
    entry.description = "Text: \"" + text + "\"";
    addEntry(std::move(entry));
}

void TerminalInspector::logCSI(const std::string& raw, const std::string& description) {
    if (!enabled_ || paused_) return;
    InspectorEntry entry;
    entry.type = InspectorEntry::Type::CSI;
    entry.raw = raw;
    entry.description = description;
    addEntry(std::move(entry));
}

void TerminalInspector::logOSC(int number, const std::string& data, const std::string& description) {
    if (!enabled_ || paused_) return;
    InspectorEntry entry;
    entry.type = InspectorEntry::Type::OSC;
    entry.raw = "\x1b]" + std::to_string(number) + ";" + data + "\x07";
    entry.description = description;
    addEntry(std::move(entry));
}

void TerminalInspector::logESC(const std::string& raw, const std::string& description) {
    if (!enabled_ || paused_) return;
    InspectorEntry entry;
    entry.type = InspectorEntry::Type::ESC;
    entry.raw = raw;
    entry.description = description;
    addEntry(std::move(entry));
}

void TerminalInspector::logDCS(const std::string& raw, const std::string& description) {
    if (!enabled_ || paused_) return;
    InspectorEntry entry;
    entry.type = InspectorEntry::Type::DCS;
    entry.raw = raw;
    entry.description = description;
    addEntry(std::move(entry));
}

void TerminalInspector::logControl(char c, const std::string& description) {
    if (!enabled_ || paused_) return;
    InspectorEntry entry;
    entry.type = InspectorEntry::Type::Control;
    entry.raw = std::string(1, c);
    entry.description = description;
    addEntry(std::move(entry));
}

const std::vector<InspectorEntry>& TerminalInspector::entries() const {
    return entries_;
}

size_t TerminalInspector::entryCount() const {
    return entries_.size();
}

void TerminalInspector::clear() {
    entries_.clear();
    nextSequenceNumber_ = 0;
}

void TerminalInspector::setTypeFilter(InspectorEntry::Type type, bool show) {
    typeFilter_[static_cast<int>(type)] = show;
}

bool TerminalInspector::isTypeVisible(InspectorEntry::Type type) const {
    return typeFilter_[static_cast<int>(type)];
}

std::vector<const InspectorEntry*> TerminalInspector::filteredEntries() const {
    std::vector<const InspectorEntry*> result;
    for (const auto& entry : entries_) {
        if (typeFilter_[static_cast<int>(entry.type)]) {
            result.push_back(&entry);
        }
    }
    return result;
}

void TerminalInspector::pause() {
    paused_ = true;
}

void TerminalInspector::resume() {
    paused_ = false;
}

bool TerminalInspector::isPaused() const {
    return paused_;
}

void TerminalInspector::setMaxEntries(size_t max) {
    maxEntries_ = max;
    // Trim if current entries exceed new max
    while (entries_.size() > maxEntries_) {
        entries_.erase(entries_.begin());
    }
}

size_t TerminalInspector::maxEntries() const {
    return maxEntries_;
}

void TerminalInspector::addEntry(InspectorEntry entry) {
    entry.timestamp = std::chrono::steady_clock::now();
    entry.sequence_number = nextSequenceNumber_++;
    // Ring buffer: erase from front when exceeding max
    if (entries_.size() >= maxEntries_) {
        entries_.erase(entries_.begin());
    }
    entries_.push_back(std::move(entry));
}

} // namespace termcore
