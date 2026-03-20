#ifndef TERMCORE_SEARCH_CONTROLLER_H
#define TERMCORE_SEARCH_CONTROLLER_H

#include "termcore/search.h"
#include "termcore/screen.h"
#include <string>

namespace termcore {

class SearchController {
public:
    void open();
    void close();
    bool isActive() const { return active_; }

    void setQuery(const std::string& query, Screen& screen);
    void next(Screen& screen);
    void prev(Screen& screen);

    int currentMatch() const;
    int totalMatches() const;
    const TerminalSearch& search() const { return search_; }

private:
    TerminalSearch search_;
    bool active_ = false;
};

} // namespace termcore
#endif
