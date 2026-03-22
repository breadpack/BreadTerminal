#include "termcore/search.h"
#include "termcore/screen.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <regex>

namespace termcore {

TerminalSearch::TerminalSearch() = default;

int TerminalSearch::search(const Screen& screen, const std::string& query,
                           const SearchOptions& options) {
    clear();

    if (query.empty()) {
        return 0;
    }

    query_ = query;
    options_ = options;

    if (options.use_regex) {
        // Compile regex pattern; if invalid, return 0 matches
        std::regex pattern;
        try {
            auto flags = std::regex::ECMAScript;
            if (!options.case_sensitive) {
                flags |= std::regex::icase;
            }
            pattern = std::regex(query, flags);
        } catch (const std::regex_error&) {
            return 0;
        }

        // Search scrollback lines (negative row indices)
        if (options.search_scrollback) {
            int sb_size = static_cast<int>(screen.scrollbackSize());
            for (int i = sb_size; i >= 1; --i) {
                int row = -i;
                std::string text = getRowText(screen, row);
                findRegexInLine(text, row, pattern);
            }
        }

        // Search visible screen rows
        for (int r = 0; r < screen.rows(); ++r) {
            std::string text = getRowText(screen, r);
            findRegexInLine(text, r, pattern);
        }
    } else {
        // Prepare the search query (lowercase if case-insensitive)
        std::string search_query = query;
        if (!options.case_sensitive) {
            std::transform(search_query.begin(), search_query.end(),
                           search_query.begin(),
                           [](unsigned char c) { return std::tolower(c); });
        }

        // Search scrollback lines (negative row indices)
        if (options.search_scrollback) {
            int sb_size = static_cast<int>(screen.scrollbackSize());
            for (int i = sb_size; i >= 1; --i) {
                int row = -i;
                std::string text = getRowText(screen, row);
                findInLine(text, row, search_query, options.case_sensitive);
            }
        }

        // Search visible screen rows
        for (int r = 0; r < screen.rows(); ++r) {
            std::string text = getRowText(screen, r);
            findInLine(text, r, search_query, options.case_sensitive);
        }
    }

    if (!matches_.empty()) {
        current_ = 0;
    }

    return static_cast<int>(matches_.size());
}

const SearchMatch* TerminalSearch::currentMatch() const {
    if (current_ >= 0 && current_ < static_cast<int>(matches_.size())) {
        return &matches_[current_];
    }
    return nullptr;
}

const SearchMatch* TerminalSearch::next() {
    if (matches_.empty()) {
        return nullptr;
    }

    if (current_ < 0) {
        current_ = 0;
        return &matches_[current_];
    }

    if (current_ + 1 < static_cast<int>(matches_.size())) {
        ++current_;
        return &matches_[current_];
    }

    // At the end
    if (options_.wrap_around) {
        current_ = 0;
        return &matches_[current_];
    }

    return nullptr;
}

const SearchMatch* TerminalSearch::prev() {
    if (matches_.empty()) {
        return nullptr;
    }

    if (current_ < 0) {
        current_ = static_cast<int>(matches_.size()) - 1;
        return &matches_[current_];
    }

    if (current_ > 0) {
        --current_;
        return &matches_[current_];
    }

    // At the beginning
    if (options_.wrap_around) {
        current_ = static_cast<int>(matches_.size()) - 1;
        return &matches_[current_];
    }

    return nullptr;
}

const SearchMatch* TerminalSearch::nearestTo(int row) {
    if (matches_.empty()) {
        return nullptr;
    }

    int best_idx = 0;
    int best_dist = std::abs(matches_[0].row - row);

    for (int i = 1; i < static_cast<int>(matches_.size()); ++i) {
        int dist = std::abs(matches_[i].row - row);
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = i;
        }
    }

    current_ = best_idx;
    return &matches_[current_];
}

void TerminalSearch::clear() {
    query_.clear();
    matches_.clear();
    current_ = -1;
}

std::string TerminalSearch::getRowText(const Screen& screen, int row) const {
    if (row >= 0) {
        return screen.getLineText(row);
    }
    // Negative row: scrollback. -1 = most recent (line 0), -2 = line 1, etc.
    int line = -row - 1;
    return screen.getScrollbackLineText(line);
}

void TerminalSearch::findInLine(const std::string& line, int row,
                                const std::string& query,
                                bool case_sensitive) {
    if (line.empty() || query.empty()) {
        return;
    }

    std::string search_line;
    if (!case_sensitive) {
        search_line.resize(line.size());
        std::transform(line.begin(), line.end(),
                       search_line.begin(),
                       [](unsigned char c) { return std::tolower(c); });
    } else {
        search_line = line;
    }

    size_t pos = 0;
    int query_len = static_cast<int>(query.size());

    while ((pos = search_line.find(query, pos)) != std::string::npos) {
        SearchMatch match;
        match.row = row;
        match.start_col = static_cast<int>(pos);
        match.end_col = static_cast<int>(pos) + query_len;
        matches_.push_back(match);
        ++pos;  // Advance by 1 to find overlapping matches
    }
}

void TerminalSearch::findRegexInLine(const std::string& line, int row,
                                     const std::regex& pattern) {
    if (line.empty()) {
        return;
    }

    auto it = line.cbegin();
    std::smatch match;
    while (std::regex_search(it, line.cend(), match, pattern)) {
        if (match.length() == 0) {
            // Skip zero-length matches to avoid infinite loop
            if (it != line.cend()) {
                ++it;
            }
            continue;
        }
        int start_col = static_cast<int>(match.position()) +
                        static_cast<int>(std::distance(line.cbegin(), it));
        int end_col = start_col + static_cast<int>(match.length());
        matches_.push_back({row, start_col, end_col});
        it = match.suffix().first;
    }
}

} // namespace termcore
