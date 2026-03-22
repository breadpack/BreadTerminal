#ifndef TERMCORE_SEARCH_HISTORY_H
#define TERMCORE_SEARCH_HISTORY_H

#include <string>
#include <vector>

namespace termcore {

/// Stores recent search queries with deduplication and navigation.
/// Maximum capacity is 50 entries, most recent first.
class SearchHistory {
public:
    static constexpr size_t kMaxEntries = 50;

    SearchHistory() = default;

    /// Add a query to history (moves to front if already present)
    void addQuery(const std::string& query);

    /// Get all history entries (most recent first)
    const std::vector<std::string>& getHistory() const { return entries_; }

    /// Navigate up (older entries). Returns true if position changed.
    bool navigateUp();

    /// Navigate down (newer entries). Returns true if position changed.
    bool navigateDown();

    /// Get the current history entry, or empty string if at initial position
    std::string currentEntry() const;

    /// Reset navigation position to before-first (typing position)
    void resetNavigation();

    /// Clear all history entries
    void clear();

    /// Save history to a JSON file
    void saveToDisk(const std::string& path) const;

    /// Load history from a JSON file
    void loadFromDisk(const std::string& path);

    /// Get the default search history file path for the current platform
    static std::string defaultPath();

private:
    std::vector<std::string> entries_;
    int nav_index_ = -1;  // -1 = not navigating (typing position)
};

} // namespace termcore
#endif
