#ifndef TERMCORE_SEARCH_H
#define TERMCORE_SEARCH_H

#include <cstdint>
#include <string>
#include <vector>

namespace termcore {

class Screen;

/// A search match location
struct SearchMatch {
    int row;         // Row in screen (negative = scrollback, e.g., -1 = first scrollback line)
    int start_col;   // Starting column
    int end_col;     // Ending column (exclusive)
};

/// Search options
struct SearchOptions {
    bool case_sensitive = false;
    bool wrap_around = true;
    bool search_scrollback = true;
};

/// Terminal text search engine
class TerminalSearch {
public:
    TerminalSearch();
    ~TerminalSearch() = default;

    /// Start a new search. Finds all matches in the screen and scrollback.
    /// Returns number of matches found.
    int search(const Screen& screen, const std::string& query,
               const SearchOptions& options = {});

    /// Get all matches
    const std::vector<SearchMatch>& matches() const { return matches_; }

    /// Get current match index (-1 if none)
    int currentIndex() const { return current_; }

    /// Get current match (nullptr if none)
    const SearchMatch* currentMatch() const;

    /// Move to next match. Returns the match or nullptr.
    const SearchMatch* next();

    /// Move to previous match. Returns the match or nullptr.
    const SearchMatch* prev();

    /// Move to match nearest to a given row.
    const SearchMatch* nearestTo(int row);

    /// Get total match count
    size_t matchCount() const { return matches_.size(); }

    /// Clear search state
    void clear();

    /// Get the current query
    const std::string& query() const { return query_; }

    /// Check if search is active
    bool isActive() const { return !query_.empty(); }

private:
    std::string query_;
    std::vector<SearchMatch> matches_;
    int current_ = -1;
    SearchOptions options_;

    /// Extract text from a screen row for searching
    std::string getRowText(const Screen& screen, int row) const;

    /// Find all occurrences of query in a line
    void findInLine(const std::string& line, int row,
                    const std::string& query, bool case_sensitive);
};

} // namespace termcore
#endif
