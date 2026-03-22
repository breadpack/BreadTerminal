#ifndef TERMCORE_ANNOTATIONS_H
#define TERMCORE_ANNOTATIONS_H

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace termcore {

/// A user annotation attached to a screen row
struct Annotation {
    int absoluteRow = 0;  // absolute row (including scrollback)
    int startCol = -1;    // -1 means whole line
    int endCol = -1;
    std::string text;
    uint32_t color = 0xFFFF00;  // highlight color (RGB)
    std::chrono::steady_clock::time_point created;
};

/// Tab badge - a small status string shown on the tab
struct TabBadge {
    std::string text;      // badge text (short, e.g., "SSH", "root", hostname)
    uint32_t bgColor = 0;  // 0 = use default
    uint32_t fgColor = 0;  // 0 = use default
    bool visible = true;
};

/// Manages annotations for a single pane/screen
class AnnotationManager {
public:
    /// Add annotation to a row. Returns annotation ID.
    int addAnnotation(int absoluteRow, const std::string& text,
                      int startCol = -1, int endCol = -1,
                      uint32_t color = 0xFFFF00);

    /// Remove annotation by ID
    bool removeAnnotation(int id);

    /// Get all annotations
    const std::map<int, Annotation>& annotations() const;

    /// Get annotations visible in a row range
    std::vector<const Annotation*> annotationsInRange(int startRow,
                                                      int endRow) const;

    /// Check if a row has annotations
    bool hasAnnotation(int absoluteRow) const;

    /// Clear all annotations
    void clear();

    /// Count
    size_t count() const;

private:
    int nextId_ = 1;
    std::map<int, Annotation> annotations_;  // id -> Annotation
};

/// Badge format string expansion.
/// Supports: {hostname}, {user}, {shell}, {cwd}, {branch}
std::string expandBadgeFormat(const std::string& format,
                              const std::string& hostname,
                              const std::string& user,
                              const std::string& shell,
                              const std::string& cwd,
                              const std::string& branch);

}  // namespace termcore

#endif  // TERMCORE_ANNOTATIONS_H
