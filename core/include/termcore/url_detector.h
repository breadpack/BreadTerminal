#ifndef TERMCORE_URL_DETECTOR_H
#define TERMCORE_URL_DETECTOR_H

#include <cstdint>
#include <string>
#include <vector>

namespace termcore {

/// A detected URL in terminal text
struct DetectedUrl {
    int row;
    int start_col;
    int end_col;     // exclusive
    std::string url;
};

/// Detects URLs in terminal text
class UrlDetector {
public:
    UrlDetector();

    ~UrlDetector() = default;

    /// Detect URLs in a single line of text.
    std::vector<DetectedUrl> detectInLine(const std::string& text, int row) const;

    /// Detect URLs across all visible screen rows.
    std::vector<DetectedUrl> detectInScreen(const class Screen& screen) const;

    /// Check if a position (row, col) is within a detected URL.
    /// Returns the URL or empty string.
    std::string urlAt(const std::vector<DetectedUrl>& urls, int row, int col) const;

    /// Check if a string looks like a URL
    static bool isUrl(const std::string& text);

    /// Add a custom URL scheme (e.g. "magnet", "obsidian").
    void addCustomScheme(const std::string& scheme);

    /// Get all custom schemes registered by Lua plugins.
    const std::vector<std::string>& customSchemes() const { return customSchemes_; }

    /// Set characters that terminate a URL (e.g. space, tab, newline, angle brackets).
    /// Default: " \t\n\r<>\"'`"
    void setTerminators(const std::string& chars);
    const std::string& terminators() const { return terminators_; }

    /// Set characters stripped from the end of a detected URL.
    /// Default: ".,:;!?"
    void setTrailingPunctuation(const std::string& chars);
    const std::string& trailingPunctuation() const { return trailingPunctuation_; }

private:
    /// Find the end position of a URL starting at pos
    size_t findUrlEnd(const std::string& text, size_t pos) const;

    bool isTerminator(char c) const;
    bool isTrailing(char c) const;

    std::vector<std::string> builtinSchemes_;
    std::vector<std::string> customSchemes_;
    std::string terminators_ = " \t\n\r<>\"'`";
    std::string trailingPunctuation_ = ".,:;!?";
};

} // namespace termcore
#endif
