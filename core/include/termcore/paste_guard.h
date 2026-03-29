#ifndef TERMCORE_PASTE_GUARD_H
#define TERMCORE_PASTE_GUARD_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace termcore {

enum class PasteDanger { Safe, Warn };

enum class PasteSignal : uint32_t {
    MultiLine        = 1 << 0,
    TrailingNewline  = 1 << 1,
    SudoCommand      = 1 << 2,
    RmRf             = 1 << 3,
    CurlPipe         = 1 << 4,
    ChmodRecursive   = 1 << 5,
    Base64Decode     = 1 << 6,
    HomeDirectoryWipe = 1 << 7,
    SudoSuRoot       = 1 << 8
};

struct PasteSpan {
    size_t offset;
    size_t length;
    PasteSignal signal;
};

struct PasteAnalysis {
    PasteDanger danger;
    uint32_t signals;
    std::vector<PasteSpan> spans;
    int line_count;
    bool ends_with_newline;
    bool bracketed;
};

class PasteGuard {
public:
    struct Config {
        enum class Mode { Never, Multiline, Always };
        Mode mode = Mode::Multiline;
        bool trust_bracketed = true;
    };

    PasteGuard();
    explicit PasteGuard(Config cfg);

    PasteAnalysis analyze(const std::string& text, bool bracketed_paste_active) const;

    /// Add a custom danger pattern with a description (Lua-configurable).
    /// Simple substring match: triggers if text.find(pattern) succeeds.
    void addCustomDanger(const std::string& pattern, const std::string& description);

    /// Add a compound danger pattern requiring BOTH patterns to match (Lua-configurable).
    /// Useful for "curl ... | ... sh" style checks where two substrings must co-occur.
    void addCompoundDanger(const std::string& pattern1, const std::string& pattern2,
                           const std::string& description);

    /// Add a pipe-to-shell danger: source command piped to sh/bash/zsh.
    /// Checks that source appears before a pipe and a shell appears after it.
    void addPipeDanger(const std::string& sourceCmd, const std::string& description);

    /// Add a whitelist pattern — pastes matching this are always Safe (Lua-configurable).
    void addWhitelist(const std::string& pattern);

    /// Set the paste guard mode from a string: "never", "multiline", "always".
    void setModeFromString(const std::string& mode);

    /// Get current config (read-only).
    const Config& config() const { return cfg_; }

private:
    Config cfg_;
    std::vector<std::pair<std::string, std::string>> customDangerPatterns_;
    std::vector<std::string> whitelistPatterns_;

    struct CompoundPattern {
        std::string pattern1;
        std::string pattern2;
        std::string description;
    };
    std::vector<CompoundPattern> compoundDangerPatterns_;

    struct PipeDangerPattern {
        std::string sourceCmd;
        std::string description;
    };
    std::vector<PipeDangerPattern> pipeDangerPatterns_;

    PasteDanger computeDanger(uint32_t signals, bool bracketed) const;

    /// Check if text matches any whitelist pattern (substring match).
    bool isWhitelisted(const std::string& text) const;

    /// Check custom danger patterns; returns true and sets a signal bit if matched.
    bool checkCustomDanger(const std::string& text, uint32_t& signals) const;
};

} // namespace termcore

#endif
