#include "termcore/paste_guard.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace termcore {

namespace {

// Split a string into whitespace-delimited tokens, preserving pipe '|' as a separator.
std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string cur;
    for (char c : line) {
        if (c == '|') {
            if (!cur.empty()) {
                tokens.push_back(cur);
                cur.clear();
            }
            tokens.push_back("|");
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) {
                tokens.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    return tokens;
}

// Check if token matches a word exactly (case-sensitive).
bool isWord(const std::vector<std::string>& tokens, const std::string& word) {
    for (auto& t : tokens) {
        if (t == word) return true;
    }
    return false;
}

// Find the byte offset of a word-token in the line.
size_t findWordOffset(const std::string& line, const std::string& word) {
    size_t pos = 0;
    while (pos < line.size()) {
        auto found = line.find(word, pos);
        if (found == std::string::npos) return std::string::npos;
        // Check word boundary: char before and after must be non-alnum.
        bool left_ok = (found == 0) ||
                       !std::isalnum(static_cast<unsigned char>(line[found - 1]));
        bool right_ok = (found + word.size() >= line.size()) ||
                        !std::isalnum(static_cast<unsigned char>(line[found + word.size()]));
        if (left_ok && right_ok) return found;
        pos = found + 1;
    }
    return std::string::npos;
}

// Check if any token in the list appears after a pipe token and matches one of the targets.
bool hasPipeTo(const std::vector<std::string>& tokens,
               const std::vector<std::string>& sources,
               const std::vector<std::string>& targets) {
    for (size_t i = 0; i + 2 < tokens.size(); ++i) {
        bool source_match = false;
        for (auto& s : sources) {
            if (tokens[i] == s) { source_match = true; break; }
        }
        if (!source_match) continue;
        // Look for pipe after this token.
        for (size_t j = i + 1; j < tokens.size(); ++j) {
            if (tokens[j] == "|") {
                // Next non-pipe token is the target command.
                if (j + 1 < tokens.size()) {
                    for (auto& t : targets) {
                        if (tokens[j + 1] == t) return true;
                    }
                }
                break;
            }
        }
    }
    return false;
}

// Check if rm has dangerous -r/-R/-rf flags.
bool hasRmDangerousFlags(const std::vector<std::string>& tokens) {
    bool has_rm = false;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i] == "rm") {
            has_rm = true;
            // Check subsequent tokens for flags.
            for (size_t j = i + 1; j < tokens.size(); ++j) {
                if (tokens[j].empty() || tokens[j][0] != '-') continue;
                if (tokens[j] == "|") break;
                const auto& flag = tokens[j];
                // Check for -r, -R, -rf, -fr, or combined flags containing 'r'/'R' and 'f'
                if (flag == "-r" || flag == "-R" || flag == "-rf" || flag == "-Rf" ||
                    flag == "-fr" || flag == "-fR") {
                    return true;
                }
                // Check combined short flags like -rfi, etc.
                if (flag.size() > 1 && flag[0] == '-' && flag[1] != '-') {
                    bool has_r = false;
                    for (size_t k = 1; k < flag.size(); ++k) {
                        if (flag[k] == 'r' || flag[k] == 'R') has_r = true;
                    }
                    if (has_r) return true;
                }
            }
        }
    }
    return false;
}

// Check for rm -rf targeting ~ or / or $HOME.
bool hasHomeOrRootWipe(const std::vector<std::string>& tokens) {
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i] == "rm") {
            bool has_rf = false;
            for (size_t j = i + 1; j < tokens.size(); ++j) {
                if (tokens[j] == "|") break;
                const auto& t = tokens[j];
                if (t.size() > 1 && t[0] == '-') {
                    bool has_r = false, has_f = false;
                    for (size_t k = 1; k < t.size(); ++k) {
                        if (t[k] == 'r' || t[k] == 'R') has_r = true;
                        if (t[k] == 'f') has_f = true;
                    }
                    if (has_r && has_f) has_rf = true;
                    if (has_r) has_rf = true; // rm -r ~ is also dangerous
                }
                if (has_rf || hasRmDangerousFlags(tokens)) {
                    if (t == "~" || t == "/" || t == "/*" || t == "~/*" ||
                        t == "$HOME" || t == "$HOME/") {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

enum class WipeTarget { Home, Root, None };

WipeTarget detectWipeTarget(const std::vector<std::string>& tokens) {
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i] != "rm") continue;
        bool has_r = false;
        for (size_t j = i + 1; j < tokens.size(); ++j) {
            if (tokens[j] == "|") break;
            const auto& t = tokens[j];
            if (t.size() > 1 && t[0] == '-') {
                for (size_t k = 1; k < t.size(); ++k) {
                    if (t[k] == 'r' || t[k] == 'R') has_r = true;
                }
            }
            if (has_r) {
                if (t == "~" || t == "~/*" || t == "$HOME" || t == "$HOME/") {
                    return WipeTarget::Home;
                }
                if (t == "/" || t == "/*") {
                    return WipeTarget::Root;
                }
            }
        }
    }
    return WipeTarget::None;
}

// Check for "chmod -R 777" pattern.
bool hasChmodRecursive777(const std::vector<std::string>& tokens) {
    for (size_t i = 0; i + 2 < tokens.size(); ++i) {
        if (tokens[i] == "chmod") {
            bool has_R = false;
            bool has_777 = false;
            for (size_t j = i + 1; j < tokens.size(); ++j) {
                if (tokens[j] == "|") break;
                if (tokens[j] == "-R") has_R = true;
                if (tokens[j] == "777") has_777 = true;
            }
            if (has_R && has_777) return true;
        }
    }
    return false;
}

// Check for "base64 -d" or "base64 --decode" piped to shell.
bool hasBase64DecodePipe(const std::vector<std::string>& tokens) {
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i] == "base64" &&
            (i + 1 < tokens.size() && (tokens[i + 1] == "-d" || tokens[i + 1] == "--decode" || tokens[i + 1] == "-D"))) {
            // Check for pipe to shell after.
            for (size_t j = i + 2; j < tokens.size(); ++j) {
                if (tokens[j] == "|") {
                    if (j + 1 < tokens.size()) {
                        auto& t = tokens[j + 1];
                        if (t == "sh" || t == "bash" || t == "zsh" || t == "eval") {
                            return true;
                        }
                    }
                    break;
                }
            }
            // Even without pipe, base64 decode piped is suspicious.
            // Actually, per spec, only flag if piped to shell.
            // But let's also flag "base64 -d" followed by pipe.
        }
    }
    // Also check: echo ... | base64 -d | sh pattern
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i] == "base64" &&
            (i + 1 < tokens.size() && (tokens[i + 1] == "-d" || tokens[i + 1] == "--decode" || tokens[i + 1] == "-D"))) {
            for (size_t j = i; j < tokens.size(); ++j) {
                if (tokens[j] == "|" && j + 1 < tokens.size()) {
                    auto& t = tokens[j + 1];
                    if (t == "sh" || t == "bash" || t == "zsh" || t == "eval") {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// Check for "sudo su" or "sudo -i".
bool hasSudoSuRoot(const std::vector<std::string>& tokens) {
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i] == "sudo") {
            if (tokens[i + 1] == "su" || tokens[i + 1] == "-i") {
                return true;
            }
        }
    }
    return false;
}

} // namespace

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

    // Scan each line for dangerous patterns.
    std::istringstream stream(text);
    std::string line;
    size_t line_offset = 0;

    while (std::getline(stream, line)) {
        // Remove trailing \r.
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        auto tokens = tokenize(line);

        // sudo command detection.
        if (isWord(tokens, "sudo")) {
            // Check it's not sudo su (handled separately).
            if (!hasSudoSuRoot(tokens)) {
                result.signals |= static_cast<uint32_t>(PasteSignal::SudoCommand);
                auto off = findWordOffset(line, "sudo");
                if (off != std::string::npos) {
                    result.spans.push_back({line_offset + off, 4, PasteSignal::SudoCommand});
                }
            } else {
                // sudo su / sudo -i: flag both SudoCommand and SudoSuRoot.
                result.signals |= static_cast<uint32_t>(PasteSignal::SudoCommand);
                result.signals |= static_cast<uint32_t>(PasteSignal::SudoSuRoot);
                auto off = findWordOffset(line, "sudo");
                if (off != std::string::npos) {
                    // Span covers "sudo su" or "sudo -i".
                    size_t span_end = off + 4;
                    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
                        if (tokens[i] == "sudo" && (tokens[i + 1] == "su" || tokens[i + 1] == "-i")) {
                            auto su_off = findWordOffset(line, tokens[i + 1]);
                            if (su_off != std::string::npos) {
                                span_end = su_off + tokens[i + 1].size();
                            }
                            break;
                        }
                    }
                    result.spans.push_back({line_offset + off, span_end - off, PasteSignal::SudoSuRoot});
                }
            }
        }

        // rm -rf detection.
        if (hasRmDangerousFlags(tokens)) {
            auto wipe = detectWipeTarget(tokens);
            if (wipe == WipeTarget::Home) {
                result.signals |= static_cast<uint32_t>(PasteSignal::RmRf);
                result.signals |= static_cast<uint32_t>(PasteSignal::HomeDirectoryWipe);
                auto off = findWordOffset(line, "rm");
                if (off != std::string::npos) {
                    result.spans.push_back({line_offset + off, line.size() - off, PasteSignal::HomeDirectoryWipe});
                }
            } else if (wipe == WipeTarget::Root) {
                result.signals |= static_cast<uint32_t>(PasteSignal::RmRf);
                auto off = findWordOffset(line, "rm");
                if (off != std::string::npos) {
                    result.spans.push_back({line_offset + off, line.size() - off, PasteSignal::RmRf});
                }
            } else {
                result.signals |= static_cast<uint32_t>(PasteSignal::RmRf);
                auto off = findWordOffset(line, "rm");
                if (off != std::string::npos) {
                    result.spans.push_back({line_offset + off, line.size() - off, PasteSignal::RmRf});
                }
            }
        }

        // curl/wget piped to shell.
        if (hasPipeTo(tokens, {"curl", "wget"}, {"sh", "bash", "zsh"})) {
            result.signals |= static_cast<uint32_t>(PasteSignal::CurlPipe);
            // Find curl or wget offset.
            auto off = findWordOffset(line, "curl");
            if (off == std::string::npos) off = findWordOffset(line, "wget");
            if (off != std::string::npos) {
                result.spans.push_back({line_offset + off, line.size() - off, PasteSignal::CurlPipe});
            }
        }

        // chmod -R 777.
        if (hasChmodRecursive777(tokens)) {
            result.signals |= static_cast<uint32_t>(PasteSignal::ChmodRecursive);
            auto off = findWordOffset(line, "chmod");
            if (off != std::string::npos) {
                result.spans.push_back({line_offset + off, line.size() - off, PasteSignal::ChmodRecursive});
            }
        }

        // base64 decode piped to shell.
        if (hasBase64DecodePipe(tokens)) {
            result.signals |= static_cast<uint32_t>(PasteSignal::Base64Decode);
            auto off = findWordOffset(line, "base64");
            if (off != std::string::npos) {
                result.spans.push_back({line_offset + off, line.size() - off, PasteSignal::Base64Decode});
            }
        }

        // Advance offset: line length + 1 for the newline character.
        line_offset += line.size() + 1; // +1 for '\n' consumed by getline
    }

    // Check custom danger patterns
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
