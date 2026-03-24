#include "termcore/command_palette.h"

#include <algorithm>
#include <cctype>

namespace termcore {

// Human-readable names for each action.
static const struct { Action action; const char* name; const char* desc; } kActionEntries[] = {
    { Action::NewTab,            "New Tab",              "Open a new tab" },
    { Action::CloseTab,          "Close Tab",            "Close the current tab" },
    { Action::NextTab,           "Next Tab",             "Switch to the next tab" },
    { Action::PrevTab,           "Previous Tab",         "Switch to the previous tab" },
    { Action::SplitRight,        "Split Right",          "Split pane to the right" },
    { Action::SplitDown,         "Split Down",           "Split pane downward" },
    { Action::ClosePane,         "Close Pane",           "Close the current pane" },
    { Action::FocusUp,           "Focus Up",             "Move focus to pane above" },
    { Action::FocusDown,         "Focus Down",           "Move focus to pane below" },
    { Action::FocusLeft,         "Focus Left",           "Move focus to pane on the left" },
    { Action::FocusRight,        "Focus Right",          "Move focus to pane on the right" },
    { Action::Copy,              "Copy",                 "Copy selection to clipboard" },
    { Action::Paste,             "Paste",                "Paste from clipboard" },
    { Action::SelectAll,         "Select All",           "Select all text" },
    { Action::ScrollUp,          "Scroll Up",            "Scroll up a few lines" },
    { Action::ScrollDown,        "Scroll Down",          "Scroll down a few lines" },
    { Action::ScrollPageUp,      "Scroll Page Up",       "Scroll up one page" },
    { Action::ScrollPageDown,    "Scroll Page Down",     "Scroll down one page" },
    { Action::ScrollToTop,       "Scroll to Top",        "Scroll to the top of history" },
    { Action::ScrollToBottom,    "Scroll to Bottom",     "Scroll to the bottom" },
    { Action::SearchOpen,        "Find",                 "Open search bar" },
    { Action::SearchNext,        "Find Next",            "Jump to next search match" },
    { Action::SearchPrev,        "Find Previous",        "Jump to previous search match" },
    { Action::NewWindow,         "New Window",           "Open a new window" },
    { Action::CloseWindow,       "Close Window",         "Close the window" },
    { Action::ToggleFullscreen,  "Toggle Fullscreen",    "Toggle fullscreen mode" },
    { Action::FontIncrease,      "Increase Font Size",   "Make text larger" },
    { Action::FontDecrease,      "Decrease Font Size",   "Make text smaller" },
    { Action::FontReset,         "Reset Font Size",      "Reset text to default size" },
    { Action::ResetTerminal,     "Reset Terminal",       "Reset the terminal state" },
    { Action::ClearScrollback,   "Clear Scrollback",     "Clear scrollback buffer" },
    { Action::ReloadConfig,      "Reload Config",        "Reload configuration file" },
    { Action::JumpPromptUp,      "Jump to Previous Prompt", "Navigate to previous shell prompt" },
    { Action::JumpPromptDown,    "Jump to Next Prompt",  "Navigate to next shell prompt" },
    { Action::EnterCopyMode,     "Enter Copy Mode",      "Enter vi-style copy mode" },
    { Action::ToggleSidebar,     "Toggle Sidebar",       "Show or hide the sidebar" },
    { Action::OpenSettings,      "Open Settings",        "Open settings window" },
    { Action::OpenThemeHub,      "Open Theme Hub",       "Browse and apply themes" },
    { Action::OpenFontHub,       "Open Font Hub",        "Browse and apply fonts" },
};

CommandPalette::CommandPalette() {
    populateCommands();
}

void CommandPalette::populateCommands() {
    allCommands_.clear();
    for (const auto& entry : kActionEntries) {
        PaletteCommand cmd;
        cmd.name = entry.name;
        cmd.description = entry.desc;
        cmd.action = entry.action;
        allCommands_.push_back(std::move(cmd));
    }
}

void CommandPalette::open() {
    open_ = true;
    query_.clear();
    selectedIndex_ = 0;
    applyFilter();
}

void CommandPalette::close() {
    open_ = false;
    query_.clear();
    selectedIndex_ = 0;
    filtered_.clear();
}

void CommandPalette::setQuery(const std::string& query) {
    query_ = query;
    selectedIndex_ = 0;
    applyFilter();
}

void CommandPalette::selectNext() {
    if (!filtered_.empty()) {
        int maxVisible = std::min(kMaxVisibleItems, static_cast<int>(filtered_.size()));
        selectedIndex_ = (selectedIndex_ + 1) % maxVisible;
    }
}

void CommandPalette::selectPrev() {
    if (!filtered_.empty()) {
        int maxVisible = std::min(kMaxVisibleItems, static_cast<int>(filtered_.size()));
        selectedIndex_ = (selectedIndex_ - 1 + maxVisible) % maxVisible;
    }
}

Action CommandPalette::selectedAction() const {
    if (filtered_.empty()) return Action::None;
    int maxVisible = std::min(kMaxVisibleItems, static_cast<int>(filtered_.size()));
    if (selectedIndex_ >= 0 && selectedIndex_ < maxVisible) {
        return filtered_[selectedIndex_].action;
    }
    return Action::None;
}

void CommandPalette::onChar(char ch) {
    if (ch >= 0x20) { // printable
        query_ += ch;
        selectedIndex_ = 0;
        applyFilter();
    }
}

void CommandPalette::onBackspace() {
    if (!query_.empty()) {
        query_.pop_back();
        selectedIndex_ = 0;
        applyFilter();
    }
}

void CommandPalette::updateShortcuts(const KeybindingManager& mgr) {
    // Build a map from action -> shortcut string
    for (auto& cmd : allCommands_) {
        cmd.shortcut_hint.clear();
    }
    for (const auto& binding : mgr.allBindings()) {
        // Skip the command palette itself
        if (binding.action == Action::OpenCommandPalette) continue;
        if (binding.action == Action::None || binding.action == Action::Custom) continue;

        std::string hint = formatShortcut(binding.combo);
        if (hint.empty()) continue;

        for (auto& cmd : allCommands_) {
            if (cmd.action == binding.action && cmd.shortcut_hint.empty()) {
                cmd.shortcut_hint = hint;
                break;
            }
        }
    }
}

std::string CommandPalette::formatShortcut(const KeyCombo& combo) const {
    std::string result;

#if defined(__APPLE__)
    if (combo.mods & ModSuper) result += "Cmd+";
    if (combo.mods & ModCtrl)  result += "Ctrl+";
    if (combo.mods & ModAlt)   result += "Opt+";
    if (combo.mods & ModShift) result += "Shift+";
#else
    if (combo.mods & ModCtrl)  result += "Ctrl+";
    if (combo.mods & ModAlt)   result += "Alt+";
    if (combo.mods & ModShift) result += "Shift+";
    if (combo.mods & ModSuper) result += "Win+";
#endif

    uint32_t k = combo.keycode;
    if (k >= 'a' && k <= 'z') {
        result += static_cast<char>(k - 'a' + 'A');
    } else if (k >= '0' && k <= '9') {
        result += static_cast<char>(k);
    } else {
        switch (k) {
            case 0xF700: result += "Up"; break;
            case 0xF701: result += "Down"; break;
            case 0xF702: result += "Left"; break;
            case 0xF703: result += "Right"; break;
            case 0xF704: result += "Home"; break;
            case 0xF705: result += "End"; break;
            case 0xF706: result += "PgUp"; break;
            case 0xF707: result += "PgDn"; break;
            case 0xF708: result += "Tab"; break;
            case 0xF709: result += "Enter"; break;
            case 0xF70A: result += "Esc"; break;
            case 0xF70B: result += "Bksp"; break;
            case 0xF70C: result += "Space"; break;
            case 0xF70D: result += "Del"; break;
            case 0xF710: result += "F1"; break;
            case 0xF711: result += "F2"; break;
            case 0xF712: result += "F3"; break;
            case 0xF713: result += "F4"; break;
            case 0xF714: result += "F5"; break;
            case 0xF715: result += "F6"; break;
            case 0xF716: result += "F7"; break;
            case 0xF717: result += "F8"; break;
            case 0xF718: result += "F9"; break;
            case 0xF719: result += "F10"; break;
            case 0xF71A: result += "F11"; break;
            case 0xF71B: result += "F12"; break;
            default:
                if (k > 0 && k < 128) {
                    result += static_cast<char>(k);
                } else {
                    return ""; // unknown key
                }
                break;
        }
    }
    return result;
}

void CommandPalette::applyFilter() {
    filtered_.clear();

    if (query_.empty()) {
        // Show all commands (up to max)
        for (const auto& cmd : allCommands_) {
            filtered_.push_back(cmd);
        }
        return;
    }

    // Score each command and collect matches
    struct Scored {
        int score;
        size_t index;
    };
    std::vector<Scored> scored;

    for (size_t i = 0; i < allCommands_.size(); ++i) {
        int s = fuzzyScore(allCommands_[i].name, query_);
        if (s > 0) {
            scored.push_back({s, i});
        }
    }

    // Sort by score descending
    std::sort(scored.begin(), scored.end(),
              [](const Scored& a, const Scored& b) { return a.score > b.score; });

    for (const auto& s : scored) {
        filtered_.push_back(allCommands_[s.index]);
    }
}

int CommandPalette::fuzzyScore(const std::string& name, const std::string& q) const {
    // Case-insensitive substring match with prefix bonus
    std::string lowerName = name;
    std::string lowerQ = q;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::transform(lowerQ.begin(), lowerQ.end(), lowerQ.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Check if all query chars appear in order (fuzzy subsequence match)
    size_t qi = 0;
    size_t firstMatch = std::string::npos;
    for (size_t ni = 0; ni < lowerName.size() && qi < lowerQ.size(); ++ni) {
        if (lowerName[ni] == lowerQ[qi]) {
            if (qi == 0) firstMatch = ni;
            ++qi;
        }
    }

    if (qi < lowerQ.size()) return 0; // not all chars matched

    int score = 100;

    // Prefix match bonus: query matches start of name
    if (firstMatch == 0) {
        score += 50;
    }

    // Exact substring match bonus
    if (lowerName.find(lowerQ) != std::string::npos) {
        score += 30;
    }

    // Word boundary bonus: each matched char at word start gets bonus
    // (e.g. "nt" matching "New Tab" at 'N' and 'T')
    qi = 0;
    int wordBoundaryHits = 0;
    for (size_t ni = 0; ni < lowerName.size() && qi < lowerQ.size(); ++ni) {
        if (lowerName[ni] == lowerQ[qi]) {
            bool isWordStart = (ni == 0) || (name[ni - 1] == ' ') || (name[ni - 1] == '_');
            if (isWordStart) wordBoundaryHits++;
            ++qi;
        }
    }
    score += wordBoundaryHits * 10;

    // Shorter names rank higher (less noise)
    score -= static_cast<int>(lowerName.size());

    return score;
}

void CommandPalette::registerLuaCommand(const std::string& name,
                                         std::function<void()> handler,
                                         const std::string& category) {
    // Replace existing command with same name
    for (auto& cmd : luaCommands_) {
        if (cmd.name == name) {
            cmd.handler = std::move(handler);
            cmd.category = category;
            return;
        }
    }
    luaCommands_.push_back({name, category, std::move(handler)});
}

void CommandPalette::removeLuaCommand(const std::string& name) {
    luaCommands_.erase(
        std::remove_if(luaCommands_.begin(), luaCommands_.end(),
            [&name](const LuaCommand& cmd) { return cmd.name == name; }),
        luaCommands_.end());
}

} // namespace termcore
