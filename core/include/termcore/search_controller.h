#ifndef TERMCORE_SEARCH_CONTROLLER_H
#define TERMCORE_SEARCH_CONTROLLER_H

#include "termcore/search.h"
#include "termcore/search_history.h"
#include "termcore/screen.h"
#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace termcore {

class SearchController {
public:
    /// Debounce delay for incremental search (milliseconds)
    static constexpr int kDefaultDebounceMs = 50;

    void open();
    void close();
    bool isActive() const { return active_; }

    void setQuery(const std::string& query, Screen& screen);
    void next(Screen& screen);
    void prev(Screen& screen);

    void setUseRegex(bool enabled) { useRegex_ = enabled; }
    bool useRegex() const { return useRegex_; }

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

    /// Set debounce delay in milliseconds (Lua-configurable).
    void setDebounceMs(int ms) { debounce_ms_ = ms; }

    /// Get current debounce delay in milliseconds.
    int debounceMs() const { return debounce_ms_; }

    /// Result callback: called after each search with the list of matches.
    std::function<void(const std::vector<SearchMatch>&)> onResultCallback;

private:
    TerminalSearch search_;
    SearchHistory history_;
    bool active_ = false;
    bool useRegex_ = false;
    int debounce_ms_ = kDefaultDebounceMs;

    std::string pending_query_;
    bool incremental_dirty_ = false;
    std::chrono::steady_clock::time_point last_input_time_;
};

} // namespace termcore
#endif
