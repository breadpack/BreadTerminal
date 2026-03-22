#include "termcore/search_controller.h"

namespace termcore {

void SearchController::open() {
    active_ = true;
}

void SearchController::close() {
    active_ = false;
    search_.clear();
}

void SearchController::setQuery(const std::string& query, Screen& screen) {
    SearchOptions opts;
    opts.use_regex = useRegex_;
    search_.search(screen, query, opts);
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

} // namespace termcore
