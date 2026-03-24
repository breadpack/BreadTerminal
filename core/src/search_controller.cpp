#include "termcore/search_controller.h"

namespace termcore {

void SearchController::open() {
    active_ = true;
    pending_query_.clear();
    incremental_dirty_ = false;
    history_.resetNavigation();
}

void SearchController::close() {
    active_ = false;
    search_.clear();
    pending_query_.clear();
    incremental_dirty_ = false;
    history_.resetNavigation();
}

void SearchController::setQuery(const std::string& query, Screen& screen) {
    SearchOptions opts;
    opts.use_regex = useRegex_;
    pending_query_ = query;
    search_.search(screen, query, opts);
    incremental_dirty_ = false;
}

void SearchController::next(Screen& screen) {
    (void)screen;
    search_.next();
}

void SearchController::prev(Screen& screen) {
    (void)screen;
    search_.prev();
}

int SearchController::currentMatch() const {
    return search_.currentIndex();
}

int SearchController::totalMatches() const {
    return static_cast<int>(search_.matchCount());
}

void SearchController::setQueryIncremental(const std::string& partial) {
    pending_query_ = partial;
    incremental_dirty_ = true;
    last_input_time_ = std::chrono::steady_clock::now();
}

void SearchController::onCharTyped(char c) {
    pending_query_ += c;
    incremental_dirty_ = true;
    last_input_time_ = std::chrono::steady_clock::now();
}

bool SearchController::flushIncremental(Screen& screen) {
    if (!incremental_dirty_) return false;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_input_time_);

    if (elapsed.count() < debounce_ms_) return false;

    SearchOptions opts;
    opts.use_regex = useRegex_;
    search_.search(screen, pending_query_, opts);
    incremental_dirty_ = false;
    return true;
}

void SearchController::submitQuery(Screen& screen) {
    if (!pending_query_.empty()) {
        SearchOptions opts;
        opts.use_regex = useRegex_;
        search_.search(screen, pending_query_, opts);
        history_.addQuery(pending_query_);
        incremental_dirty_ = false;
    }
}

bool SearchController::historyUp() {
    if (history_.navigateUp()) {
        pending_query_ = history_.currentEntry();
        incremental_dirty_ = true;
        last_input_time_ = std::chrono::steady_clock::now();
        return true;
    }
    return false;
}

bool SearchController::historyDown() {
    if (history_.navigateDown()) {
        pending_query_ = history_.currentEntry();
        incremental_dirty_ = true;
        last_input_time_ = std::chrono::steady_clock::now();
        return true;
    }
    return false;
}

} // namespace termcore
