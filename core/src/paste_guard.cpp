#include "termcore/paste_guard.h"

#include <algorithm>
#include <cctype>

namespace termcore {

PasteGuard::PasteGuard() : cfg_() {}

PasteGuard::PasteGuard(Config cfg) : cfg_(cfg) {}

void PasteGuard::addCustomDanger(const std::string& pattern, const std::string& description) {
    customDangerPatterns_.emplace_back(pattern, description);
}

void PasteGuard::addWhitelist(const std::string& pattern) {
    whitelistPatterns_.push_back(pattern);
}

void PasteGuard::setModeFromString(const std::string& mode) {
    if (mode == "never") {
        cfg_.mode = Config::Mode::Never;
    } else if (mode == "multiline") {
        cfg_.mode = Config::Mode::Multiline;
    } else if (mode == "always") {
        cfg_.mode = Config::Mode::Always;
    }
}

bool PasteGuard::isWhitelisted(const std::string& text) const {
    for (const auto& pattern : whitelistPatterns_) {
        if (text.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool PasteGuard::checkCustomDanger(const std::string& text, uint32_t& signals) const {
    bool found = false;
    for (const auto& [pattern, desc] : customDangerPatterns_) {
        if (text.find(pattern) != std::string::npos) {
            // Use a spare high bit for custom danger signals
            signals |= (1u << 16);
            found = true;
        }
    }
    return found;
}

// Check built-in danger patterns and populate signals + spans
static void checkBuiltinDangers(const std::string& text, uint32_t& signals,
                                std::vector<PasteSpan>& spans) {
    // --- sudo command ---
    // "sudo " detects general sudo usage.
    // Must not match inside words like "pseudocode": "sudo " requires a space after.
    // We also need to check that "sudo " appears at start-of-line or after a space/pipe/newline
    // to avoid false positives. But test "pseudocode" has "sudoc" so "sudo " won't match anyway.
    {
        std::string::size_type pos = 0;
        bool found = false;
        while ((pos = text.find("sudo ", pos)) != std::string::npos) {
            // Check it's at start or preceded by a non-alpha character
            if (pos == 0 || !std::isalpha(static_cast<unsigned char>(text[pos - 1]))) {
                if (!found) {
                    signals |= static_cast<uint32_t>(PasteSignal::SudoCommand);
                    found = true;
                }
                spans.push_back({pos, 5, PasteSignal::SudoCommand});
            }
            pos += 5;
        }
    }

    // --- sudo su (root shell) ---
    {
        auto pos = text.find("sudo su");
        if (pos != std::string::npos) {
            if (pos == 0 || !std::isalpha(static_cast<unsigned char>(text[pos - 1]))) {
                signals |= static_cast<uint32_t>(PasteSignal::SudoSuRoot);
                spans.push_back({pos, 7, PasteSignal::SudoSuRoot});
            }
        }
    }

    // --- sudo -i (root shell) ---
    {
        auto pos = text.find("sudo -i");
        if (pos != std::string::npos) {
            if (pos == 0 || !std::isalpha(static_cast<unsigned char>(text[pos - 1]))) {
                signals |= static_cast<uint32_t>(PasteSignal::SudoSuRoot);
                spans.push_back({pos, 7, PasteSignal::SudoSuRoot});
                // Also set SudoCommand if not already set
                signals |= static_cast<uint32_t>(PasteSignal::SudoCommand);
            }
        }
    }

    // --- rm -rf / rm -r / rm -R ---
    {
        bool rmFound = false;
        const char* rm_patterns[] = {"rm -rf", "rm -r ", "rm -R "};
        for (const auto& pat : rm_patterns) {
            std::string needle(pat);
            auto pos = text.find(needle);
            if (pos != std::string::npos) {
                if (!rmFound) {
                    signals |= static_cast<uint32_t>(PasteSignal::RmRf);
                    rmFound = true;
                }
                spans.push_back({pos, needle.size(), PasteSignal::RmRf});
            }
        }
    }

    // --- Home directory wipe: rm -rf with ~ or $HOME ---
    {
        // Check if text has rm -rf (or rm -r / rm -R) AND targets ~ or $HOME
        bool hasRm = (text.find("rm -rf") != std::string::npos ||
                      text.find("rm -r ") != std::string::npos ||
                      text.find("rm -R ") != std::string::npos);
        if (hasRm) {
            bool hasTilde = (text.find("~") != std::string::npos);
            bool hasHome = (text.find("$HOME") != std::string::npos);
            if (hasTilde || hasHome) {
                signals |= static_cast<uint32_t>(PasteSignal::HomeDirectoryWipe);
                // Add span for the home reference
                if (hasTilde) {
                    auto pos = text.find("~");
                    spans.push_back({pos, 1, PasteSignal::HomeDirectoryWipe});
                }
                if (hasHome) {
                    auto pos = text.find("$HOME");
                    spans.push_back({pos, 5, PasteSignal::HomeDirectoryWipe});
                }
            }
        }
    }

    // --- chmod -R 777 ---
    {
        auto pos = text.find("chmod -R 777");
        if (pos != std::string::npos) {
            signals |= static_cast<uint32_t>(PasteSignal::ChmodRecursive);
            spans.push_back({pos, 12, PasteSignal::ChmodRecursive});
        }
    }

    // --- curl/wget piped to shell ---
    // Detect: curl ... | ... sh/bash/zsh  or  wget ... | ... sh/bash/zsh
    {
        bool hasPipe = (text.find("|") != std::string::npos);
        if (hasPipe) {
            bool hasCurl = (text.find("curl") != std::string::npos);
            bool hasWget = (text.find("wget") != std::string::npos);
            if (hasCurl || hasWget) {
                // Check if there's a shell command after the pipe
                auto pipePos = text.find("|");
                while (pipePos != std::string::npos) {
                    std::string afterPipe = text.substr(pipePos + 1);
                    bool hasShell = (afterPipe.find("sh") != std::string::npos ||
                                    afterPipe.find("bash") != std::string::npos ||
                                    afterPipe.find("zsh") != std::string::npos);
                    if (hasShell) {
                        // Check curl/wget appears before this pipe
                        std::string beforePipe = text.substr(0, pipePos);
                        if (beforePipe.find("curl") != std::string::npos ||
                            beforePipe.find("wget") != std::string::npos) {
                            signals |= static_cast<uint32_t>(PasteSignal::CurlPipe);
                            // Span covers from curl/wget to end of the shell command
                            size_t startPos = std::string::npos;
                            auto cPos = beforePipe.find("curl");
                            auto wPos = beforePipe.find("wget");
                            if (cPos != std::string::npos && wPos != std::string::npos) {
                                startPos = std::min(cPos, wPos);
                            } else if (cPos != std::string::npos) {
                                startPos = cPos;
                            } else {
                                startPos = wPos;
                            }
                            spans.push_back({startPos, text.size() - startPos, PasteSignal::CurlPipe});
                            break;
                        }
                    }
                    pipePos = text.find("|", pipePos + 1);
                }
            }
        }
    }

    // --- base64 decode piped to shell ---
    // Detect: base64 -d/-D ... | ... sh/bash/zsh
    {
        bool hasBase64 = (text.find("base64") != std::string::npos);
        if (hasBase64) {
            bool hasDecode = (text.find("-d") != std::string::npos ||
                              text.find("-D") != std::string::npos);
            bool hasPipe = (text.find("|") != std::string::npos);
            if (hasDecode && hasPipe) {
                // Check for shell after pipe that comes after base64
                auto base64Pos = text.find("base64");
                auto pipePos = text.find("|", base64Pos);
                while (pipePos != std::string::npos) {
                    std::string afterPipe = text.substr(pipePos + 1);
                    if (afterPipe.find("sh") != std::string::npos ||
                        afterPipe.find("bash") != std::string::npos ||
                        afterPipe.find("zsh") != std::string::npos) {
                        signals |= static_cast<uint32_t>(PasteSignal::Base64Decode);
                        spans.push_back({base64Pos, text.size() - base64Pos, PasteSignal::Base64Decode});
                        break;
                    }
                    pipePos = text.find("|", pipePos + 1);
                }
            }
        }
    }
}

PasteAnalysis PasteGuard::analyze(const std::string& text, bool bracketed_paste_active) const {
    PasteAnalysis result{};
    result.signals = 0;
    result.bracketed = bracketed_paste_active;

    if (text.empty()) {
        result.danger = PasteDanger::Safe;
        result.line_count = 0;
        result.ends_with_newline = false;
        return result;
    }

    // Count lines.
    int newline_count = 0;
    for (char c : text) {
        if (c == '\n') ++newline_count;
    }
    result.line_count = newline_count + 1; // lines = newlines + 1

    // Check trailing newline.
    char last = text.back();
    result.ends_with_newline = (last == '\n' || last == '\r');

    // Multi-line detection.
    if (newline_count > 0) {
        result.signals |= static_cast<uint32_t>(PasteSignal::MultiLine);
    }
    if (result.ends_with_newline) {
        result.signals |= static_cast<uint32_t>(PasteSignal::TrailingNewline);
    }

    // Built-in danger pattern detection
    checkBuiltinDangers(text, result.signals, result.spans);

    // Custom dangers registered via Lua
    checkCustomDanger(text, result.signals);

    // Whitelist overrides everything
    if (isWhitelisted(text)) {
        result.danger = PasteDanger::Safe;
    } else {
        result.danger = computeDanger(result.signals, bracketed_paste_active);
    }
    return result;
}

PasteDanger PasteGuard::computeDanger(uint32_t signals, bool bracketed) const {
    if (cfg_.mode == Config::Mode::Never) {
        return PasteDanger::Safe;
    }

    if (bracketed && cfg_.trust_bracketed) {
        return PasteDanger::Safe;
    }

    // Dangerous command signals (anything beyond MultiLine/TrailingNewline).
    const uint32_t danger_mask = ~(static_cast<uint32_t>(PasteSignal::MultiLine) |
                                    static_cast<uint32_t>(PasteSignal::TrailingNewline));
    if (signals & danger_mask) {
        return PasteDanger::Warn;
    }

    if (cfg_.mode == Config::Mode::Always) {
        if (signals & static_cast<uint32_t>(PasteSignal::MultiLine)) {
            return PasteDanger::Warn;
        }
    }

    if (cfg_.mode == Config::Mode::Multiline) {
        if ((signals & static_cast<uint32_t>(PasteSignal::MultiLine)) ||
            (signals & static_cast<uint32_t>(PasteSignal::TrailingNewline))) {
            return PasteDanger::Warn;
        }
    }

    return PasteDanger::Safe;
}

} // namespace termcore
