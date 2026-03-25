#ifndef TERMCORE_STRING_UTILS_H
#define TERMCORE_STRING_UTILS_H

#include <algorithm>
#include <string>

namespace termcore {

/// Case-insensitive substring search. Returns true if needle is found in haystack.
inline bool containsIgnoreCase(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return false;
    if (needle.size() > haystack.size()) return false;

    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    return it != haystack.end();
}

} // namespace termcore

#endif
