#include "termcore/command_palette.h"

#include <algorithm>
#include <cctype>

namespace termcore {

CommandPalette::CommandPalette() {
    // No built-in entries — all commands come from Lua
}

void CommandPalette::populateCommands() {
    allCommands_.clear();
    // Include Lua-registered commands in the searchable set
    for (const auto& cmd : luaCommands_) {
        PaletteCommand pc;
        pc.name = cmd.name;
        pc.description = "";
        pc.action = Action::None;
        pc.lua_handler = cmd.handler;
        allCommands_.push_back(std::move(pc));
    }
}

void CommandPalette::open() {
    open_ = true;
    query_.clear();
    selectedIndex_ = 0;
    populateCommands();  // Refresh Lua commands
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

Action CommandPalette::executeSelected() {
    if (filtered_.empty()) return Action::None;
    int maxVisible = std::min(kMaxVisibleItems, static_cast<int>(filtered_.size()));
    if (selectedIndex_ < 0 || selectedIndex_ >= maxVisible) return Action::None;

    const auto& cmd = filtered_[selectedIndex_];
    if (cmd.lua_handler) {
        cmd.lua_handler();
        return Action::None;
    }
    return cmd.action;
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
