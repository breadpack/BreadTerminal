#include "termcore/paste_guard.h"

#include <algorithm>
#include <cctype>

namespace termcore {

// Danger pattern functions are now defined in Lua (defaults/paste_guard.lua).
// Only the customDangers_ vector (populated via Lua) is used for pattern matching.

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

    // All danger pattern detection is now handled by Lua (defaults/paste_guard.lua).
    // Only custom dangers registered via Lua are checked here.
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
