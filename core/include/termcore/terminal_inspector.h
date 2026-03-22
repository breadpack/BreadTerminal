#pragma once
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

namespace termcore {

/// A single logged VT sequence or text event
struct InspectorEntry {
    enum class Type { Text, CSI, OSC, ESC, DCS, Control };

    Type type;
    std::string raw;          // raw bytes
    std::string description;  // human-readable description (e.g., "SGR: Bold On")
    std::chrono::steady_clock::time_point timestamp;
    int sequence_number = 0;
};

/// Terminal Inspector - captures and presents VT sequences for debugging
class TerminalInspector {
public:
    TerminalInspector();

    /// Enable/disable capture
    void setEnabled(bool enabled);
    bool isEnabled() const;
    void toggle();

    /// Log entries
    void logText(const std::string& text);
    void logCSI(const std::string& raw, const std::string& description);
    void logOSC(int number, const std::string& data, const std::string& description);
    void logESC(const std::string& raw, const std::string& description);
    void logDCS(const std::string& raw, const std::string& description);
    void logControl(char c, const std::string& description);

    /// Access entries
    const std::vector<InspectorEntry>& entries() const;
    size_t entryCount() const;

    /// Clear log
    void clear();

    /// Filter
    void setTypeFilter(InspectorEntry::Type type, bool show);
    bool isTypeVisible(InspectorEntry::Type type) const;
    std::vector<const InspectorEntry*> filteredEntries() const;

    /// Pause/resume capture (keeps existing entries)
    void pause();
    void resume();
    bool isPaused() const;

    /// Max entries (ring buffer behavior)
    void setMaxEntries(size_t max);
    size_t maxEntries() const;

private:
    bool enabled_ = false;
    bool paused_ = false;
    size_t maxEntries_ = 10000;
    int nextSequenceNumber_ = 0;
    std::vector<InspectorEntry> entries_;
    bool typeFilter_[6] = {true, true, true, true, true, true}; // all visible by default

    void addEntry(InspectorEntry entry);
};

} // namespace termcore
