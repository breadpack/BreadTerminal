#ifndef TERMCORE_PASTE_GUARD_H
#define TERMCORE_PASTE_GUARD_H

#include <cstdint>
#include <string>
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

private:
    Config cfg_;

    PasteDanger computeDanger(uint32_t signals, bool bracketed) const;
};

} // namespace termcore

#endif
