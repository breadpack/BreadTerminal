#ifndef TERMCORE_SEARCH_CONTROLLER_H
#define TERMCORE_SEARCH_CONTROLLER_H

#include "termcore/search.h"
#include "termcore/search_history.h"
#include "termcore/screen.h"
#include <chrono>
#include <string>

namespace termcore {

class SearchController {
public:
    /// Debounce delay for incremental search (milliseconds)
    static constexpr int kDebounceMs = 150;

    void open();
    void close();
    bool isActive() const { return active_; }

    void setQuery(const std::string& query, Screen& screen);
    void next(Screen& screen);
    void prev(Screen& screen);

    int currentMatch() const;
    int totalMatches() const;
    const TerminalSearch& search() const { return search_; }

    /// Incremental search: set a partial query with debounce tracking.
    /// Call flushIncremental() after the debounce period to execute the search.
    void setQueryIncremental(const std::string& partial);

    /// Append a character to the current incremental query and trigger debounce.
    void onCharTyped(char c);

    /// Execute the pending incremental search if the debounce period has elapsed.
    /// Returns true if a search was executed.
    bool flushIncremental(Screen& screen);

    /// Submit the current query: executes search immediately and adds to history.
    void submitQuery(Screen& screen);

    /// Navigate search history up (older). Returns true if query changed.
    bool historyUp();

    /// Navigate search history down (newer). Returns true if query changed.
    bool historyDown();

    /// Get the current incremental query text
    const std::string& pendingQuery() const { return pending_query_; }

    /// Access the search history
    SearchHistory& history() { return history_; }
    const SearchHistory& history() const { return history_; }

private:
    TerminalSearch search_;
    SearchHistory history_;
    bool active_ = false;

    std::string pending_query_;
    bool incremental_dirty_ = false;
    std::chrono::steady_clock::time_point last_input_time_;
};

} // namespace termcore
#endif
