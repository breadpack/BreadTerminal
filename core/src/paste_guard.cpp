#include "termcore/paste_guard.h"

#include <string>

namespace termcore {

PasteGuard::PasteGuard() : cfg_() {}

PasteGuard::PasteGuard(Config cfg) : cfg_(cfg) {}

void PasteGuard::addCustomDanger(const std::string& pattern, const std::string& description) {
    customDangerPatterns_.emplace_back(pattern, description);
}

void PasteGuard::addCompoundDanger(const std::string& pattern1, const std::string& pattern2,
                                   const std::string& description) {
    compoundDangerPatterns_.push_back({pattern1, pattern2, description});
}

void PasteGuard::addPipeDanger(const std::string& sourceCmd, const std::string& description) {
    pipeDangerPatterns_.push_back({sourceCmd, description});
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

    // Simple single-pattern substring match
    for (const auto& [pattern, desc] : customDangerPatterns_) {
        if (text.find(pattern) != std::string::npos) {
            signals |= (1u << 16);
            found = true;
        }
    }

    // Compound patterns: both substrings must appear in the text
    for (const auto& cp : compoundDangerPatterns_) {
        if (text.find(cp.pattern1) != std::string::npos &&
            text.find(cp.pattern2) != std::string::npos) {
            signals |= (1u << 16);
            found = true;
        }
    }

    // Pipe-to-shell patterns: sourceCmd before pipe, shell (sh/bash/zsh) after pipe
    for (const auto& pp : pipeDangerPatterns_) {
        auto srcPos = text.find(pp.sourceCmd);
        if (srcPos == std::string::npos) continue;

        auto pipePos = text.find("|", srcPos);
        while (pipePos != std::string::npos) {
            std::string afterPipe = text.substr(pipePos + 1);
            if (afterPipe.find("sh") != std::string::npos ||
                afterPipe.find("bash") != std::string::npos ||
                afterPipe.find("zsh") != std::string::npos) {
                signals |= (1u << 16);
                found = true;
                break;
            }
            pipePos = text.find("|", pipePos + 1);
        }
    }

    return found;
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

    // Danger patterns registered via Lua (or programmatic API)
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
