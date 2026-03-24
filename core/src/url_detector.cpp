#include "termcore/url_detector.h"
#include "termcore/screen.h"

#include <algorithm>

namespace termcore {

static const std::string kSchemes[] = {
    "https://", "http://", "ftp://", "file://", "ssh://", "git://",
};

static bool isUrlTerminator(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r'
        || c == '<' || c == '>' || c == '"' || c == '\'' || c == '`';
}

static bool isTrailingPunctuation(char c) {
    return c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?';
}

size_t UrlDetector::findUrlEnd(const std::string& text, size_t pos) {
    int parenDepth = 0;
    int bracketDepth = 0;

    size_t end = pos;
    while (end < text.size()) {
        char c = text[end];

        if (isUrlTerminator(c)) break;

        // Track balanced parentheses (common in Wikipedia URLs)
        if (c == '(') {
            parenDepth++;
        } else if (c == ')') {
            if (parenDepth > 0) {
                parenDepth--;
            } else {
                // Unmatched closing paren — stop here
                break;
            }
        }

        if (c == '[') {
            bracketDepth++;
        } else if (c == ']') {
            if (bracketDepth > 0) {
                bracketDepth--;
            } else {
                break;
            }
        }

        end++;
    }

    // Strip trailing punctuation
    while (end > pos && isTrailingPunctuation(text[end - 1])) {
        end--;
    }

    return end;
}

std::vector<DetectedUrl> UrlDetector::detectInLine(const std::string& text, int row) const {
    std::vector<DetectedUrl> results;

    for (size_t i = 0; i < text.size(); ) {
        bool found = false;

        // Check for scheme URLs
        for (const auto& scheme : kSchemes) {
            if (i + scheme.size() <= text.size()
                && text.compare(i, scheme.size(), scheme) == 0) {
                size_t start = i;
                size_t end = findUrlEnd(text, i + scheme.size());
                if (end > start + scheme.size()) {
                    results.push_back({
                        row,
                        static_cast<int>(start),
                        static_cast<int>(end),
                        text.substr(start, end - start)
                    });
                    i = end;
                    found = true;
                }
                break;
            }
        }

        // Check for www. URLs
        if (!found && i + 4 <= text.size()
            && text.compare(i, 4, "www.") == 0) {
            // Must be preceded by whitespace or start of line
            if (i == 0 || text[i - 1] == ' ' || text[i - 1] == '\t'
                || text[i - 1] == '(' || text[i - 1] == '<'
                || text[i - 1] == '[') {
                // Next char after www. must be alphanumeric
                if (i + 4 < text.size() && std::isalnum(static_cast<unsigned char>(text[i + 4]))) {
                    size_t start = i;
                    size_t end = findUrlEnd(text, i + 4);
                    if (end > start + 4) {
                        std::string url = "http://" + text.substr(start, end - start);
                        results.push_back({
                            row,
                            static_cast<int>(start),
                            static_cast<int>(end),
                            url
                        });
                        i = end;
                        found = true;
                    }
                }
            }
        }

        if (!found) i++;
    }

    return results;
}

std::vector<DetectedUrl> UrlDetector::detectInScreen(const Screen& screen) const {
    std::vector<DetectedUrl> results;

    for (int row = 0; row < screen.rows(); row++) {
        std::string lineText = screen.getLineText(row);
        auto lineUrls = detectInLine(lineText, row);
        results.insert(results.end(), lineUrls.begin(), lineUrls.end());
    }

    return results;
}

std::string UrlDetector::urlAt(const std::vector<DetectedUrl>& urls, int row, int col) const {
    for (const auto& u : urls) {
        if (u.row == row && col >= u.start_col && col < u.end_col) {
            return u.url;
        }
    }
    return {};
}

bool UrlDetector::isUrl(const std::string& text) {
    for (const auto& scheme : kSchemes) {
        if (text.size() >= scheme.size()
            && text.compare(0, scheme.size(), scheme) == 0) {
            return true;
        }
    }
    if (text.size() >= 4 && text.compare(0, 4, "www.") == 0) {
        return true;
    }
    return false;
}

void UrlDetector::addCustomScheme(const std::string& scheme) {
    for (const auto& s : customSchemes_) {
        if (s == scheme) return;  // already registered
    }
    customSchemes_.push_back(scheme);
}

} // namespace termcore
