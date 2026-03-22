#include "termcore/keybinding.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace termcore {

// Special key codes for non-printable keys (sequential IDs starting at 0xF700)
static constexpr uint32_t kKeyUp        = 0xF700;
static constexpr uint32_t kKeyDown      = 0xF701;
static constexpr uint32_t kKeyLeft      = 0xF702;
static constexpr uint32_t kKeyRight     = 0xF703;
static constexpr uint32_t kKeyHome      = 0xF704;
static constexpr uint32_t kKeyEnd       = 0xF705;
static constexpr uint32_t kKeyPageUp    = 0xF706;
static constexpr uint32_t kKeyPageDown  = 0xF707;
static constexpr uint32_t kKeyTab       = 0xF708;
static constexpr uint32_t kKeyEnter     = 0xF709;
static constexpr uint32_t kKeyEscape    = 0xF70A;
static constexpr uint32_t kKeyBackspace = 0xF70B;
static constexpr uint32_t kKeySpace     = 0xF70C;
static constexpr uint32_t kKeyDelete    = 0xF70D;
static constexpr uint32_t kKeyF1        = 0xF710;
static constexpr uint32_t kKeyF2        = 0xF711;
static constexpr uint32_t kKeyF3        = 0xF712;
static constexpr uint32_t kKeyF4        = 0xF713;
static constexpr uint32_t kKeyF5        = 0xF714;
static constexpr uint32_t kKeyF6        = 0xF715;
static constexpr uint32_t kKeyF7        = 0xF716;
static constexpr uint32_t kKeyF8        = 0xF717;
static constexpr uint32_t kKeyF9        = 0xF718;
static constexpr uint32_t kKeyF10       = 0xF719;
static constexpr uint32_t kKeyF11       = 0xF71A;
static constexpr uint32_t kKeyF12       = 0xF71B;

static std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

static const std::unordered_map<std::string, uint32_t>& keyNameMap() {
    static const std::unordered_map<std::string, uint32_t> map = {
        {"up", kKeyUp}, {"down", kKeyDown}, {"left", kKeyLeft}, {"right", kKeyRight},
        {"home", kKeyHome}, {"end", kKeyEnd},
        {"pageup", kKeyPageUp}, {"pagedown", kKeyPageDown},
        {"tab", kKeyTab}, {"enter", kKeyEnter}, {"return", kKeyEnter},
        {"escape", kKeyEscape}, {"esc", kKeyEscape},
        {"backspace", kKeyBackspace}, {"space", kKeySpace}, {"delete", kKeyDelete},
        {"f1", kKeyF1}, {"f2", kKeyF2}, {"f3", kKeyF3}, {"f4", kKeyF4},
        {"f5", kKeyF5}, {"f6", kKeyF6}, {"f7", kKeyF7}, {"f8", kKeyF8},
        {"f9", kKeyF9}, {"f10", kKeyF10}, {"f11", kKeyF11}, {"f12", kKeyF12},
    };
    return map;
}

static const std::unordered_map<std::string, Action>& actionNameMap() {
    static const std::unordered_map<std::string, Action> map = {
        {"none", Action::None},
        {"new_tab", Action::NewTab}, {"close_tab", Action::CloseTab},
        {"next_tab", Action::NextTab}, {"prev_tab", Action::PrevTab},
        {"split_right", Action::SplitRight}, {"split_down", Action::SplitDown},
        {"close_pane", Action::ClosePane},
        {"focus_up", Action::FocusUp}, {"focus_down", Action::FocusDown},
        {"focus_left", Action::FocusLeft}, {"focus_right", Action::FocusRight},
        {"copy", Action::Copy}, {"paste", Action::Paste},
        {"paste_from_history", Action::PasteFromHistory}, {"select_all", Action::SelectAll},
        {"scroll_up", Action::ScrollUp}, {"scroll_down", Action::ScrollDown},
        {"scroll_page_up", Action::ScrollPageUp},
        {"scroll_page_down", Action::ScrollPageDown},
        {"scroll_to_top", Action::ScrollToTop},
        {"scroll_to_bottom", Action::ScrollToBottom},
        {"search_open", Action::SearchOpen}, {"search_next", Action::SearchNext},
        {"search_prev", Action::SearchPrev}, {"search_close", Action::SearchClose},
        {"new_window", Action::NewWindow}, {"close_window", Action::CloseWindow},
        {"toggle_fullscreen", Action::ToggleFullscreen},
        {"font_increase", Action::FontIncrease}, {"font_decrease", Action::FontDecrease},
        {"font_reset", Action::FontReset},
        {"reset_terminal", Action::ResetTerminal},
        {"clear_scrollback", Action::ClearScrollback},
        {"show_notifications", Action::ShowNotifications},
        {"reload_config", Action::ReloadConfig},
        {"jump_prompt_up", Action::JumpPromptUp},
        {"jump_prompt_down", Action::JumpPromptDown},
        {"enter_copy_mode", Action::EnterCopyMode},
        {"toggle_sidebar", Action::ToggleSidebar},
        {"switch_workspace_1", Action::SwitchWorkspace1},
        {"switch_workspace_2", Action::SwitchWorkspace2},
        {"switch_workspace_3", Action::SwitchWorkspace3},
        {"switch_workspace_4", Action::SwitchWorkspace4},
        {"switch_workspace_5", Action::SwitchWorkspace5},
        {"switch_workspace_6", Action::SwitchWorkspace6},
        {"switch_workspace_7", Action::SwitchWorkspace7},
        {"switch_workspace_8", Action::SwitchWorkspace8},
        {"switch_tab_1", Action::SwitchTab1}, {"switch_tab_2", Action::SwitchTab2},
        {"switch_tab_3", Action::SwitchTab3}, {"switch_tab_4", Action::SwitchTab4},
        {"switch_tab_5", Action::SwitchTab5}, {"switch_tab_6", Action::SwitchTab6},
        {"switch_tab_7", Action::SwitchTab7}, {"switch_tab_8", Action::SwitchTab8},
        {"switch_tab_9", Action::SwitchTab9},
        {"open_settings", Action::OpenSettings},
        {"open_theme_hub", Action::OpenThemeHub},
        {"open_font_hub", Action::OpenFontHub},
        {"custom", Action::Custom},
    };
    return map;
}

static uint32_t keyFromName(const std::string& name) {
    auto lower = toLower(name);
    auto& map = keyNameMap();
    auto it = map.find(lower);
    if (it != map.end()) return it->second;
    // Single character: use its lowercase ASCII value
    if (lower.size() == 1) return static_cast<uint32_t>(lower[0]);
    return 0;
}

// --- KeybindingManager ---

KeybindingManager::KeybindingManager() {
    initDefaults();
}

void KeybindingManager::bind(const KeyCombo& combo, Action action, const std::string& custom) {
    // Override existing binding for same combo
    for (auto& b : bindings_) {
        if (b.combo == combo) {
            b.action = action;
            b.custom_action = custom;
            return;
        }
    }
    bindings_.push_back({combo, action, custom});
}

void KeybindingManager::unbind(const KeyCombo& combo) {
    bindings_.erase(
        std::remove_if(bindings_.begin(), bindings_.end(),
                       [&](const Keybinding& b) { return b.combo == combo; }),
        bindings_.end());
}

Action KeybindingManager::lookup(const KeyCombo& combo) const {
    for (const auto& b : bindings_) {
        if (b.combo == combo) return b.action;
    }
    return Action::None;
}

std::string KeybindingManager::lookupCustom(const KeyCombo& combo) const {
    for (const auto& b : bindings_) {
        if (b.combo == combo) return b.custom_action;
    }
    return "";
}

KeyCombo KeybindingManager::parseCombo(const std::string& trigger) {
    KeyCombo combo{0, ModNone};
    std::string lower = toLower(trigger);

    // Split on '+'
    std::vector<std::string> parts;
    std::istringstream ss(lower);
    std::string part;
    while (std::getline(ss, part, '+')) {
        // Trim whitespace
        auto start = part.find_first_not_of(" \t");
        auto end = part.find_last_not_of(" \t");
        if (start != std::string::npos) {
            parts.push_back(part.substr(start, end - start + 1));
        }
    }

    if (parts.empty()) return combo;

    // Last part is the key, rest are modifiers
    for (size_t i = 0; i < parts.size() - 1; ++i) {
        const auto& mod = parts[i];
        if (mod == "cmd" || mod == "super" || mod == "win") {
            combo.mods |= ModSuper;
        } else if (mod == "ctrl" || mod == "control") {
            combo.mods |= ModCtrl;
        } else if (mod == "alt" || mod == "opt" || mod == "option") {
            combo.mods |= ModAlt;
        } else if (mod == "shift") {
            combo.mods |= ModShift;
        }
    }

    combo.keycode = keyFromName(parts.back());
    return combo;
}

Action KeybindingManager::parseAction(const std::string& action_str) {
    auto lower = toLower(action_str);
    auto& map = actionNameMap();
    auto it = map.find(lower);
    if (it != map.end()) return it->second;
    return Action::None;
}

void KeybindingManager::loadFromConfig(
    const std::vector<std::pair<std::string, std::string>>& bindings) {
    for (const auto& [trigger, action_str] : bindings) {
        auto combo = parseCombo(trigger);
        auto action = parseAction(action_str);
        // If action string not recognized, treat as custom
        if (action == Action::None && toLower(action_str) != "none") {
            bind(combo, Action::Custom, action_str);
        } else {
            bind(combo, action);
        }
    }
}

void KeybindingManager::resetDefaults() {
    bindings_.clear();
    initDefaults();
}

void KeybindingManager::initDefaults() {
    auto b = [this](const std::string& trigger, Action action) {
        bind(parseCombo(trigger), action);
    };

    // Platform-adaptive modifier: cmd on macOS, ctrl on Windows/Linux.
    // The config file can use either "cmd+" or "ctrl+" regardless of platform;
    // initDefaults() uses the platform-native modifier for sensible defaults.
#if defined(__APPLE__)
    const char* M = "cmd";       // Cmd
    const char* MS = "cmd+shift"; // Cmd+Shift
#else
    const char* M = "ctrl";       // Ctrl
    const char* MS = "ctrl+shift"; // Ctrl+Shift
#endif
    auto mk = [&](const char* mod, const char* key) -> std::string {
        return std::string(mod) + "+" + key;
    };

    // --- Unified keybindings (same logical actions on all platforms) ---

    // Tab / Pane
    b(mk(M, "t"),       Action::NewTab);
    b(mk(M, "w"),       Action::CloseTab);
    b(mk(MS, "]"),      Action::NextTab);
    b(mk(MS, "["),      Action::PrevTab);
    b(mk(M, "d"),       Action::SplitRight);
    b(mk(MS, "d"),      Action::SplitDown);

    // Tab switching by number
    b(mk(M, "1"), Action::SwitchTab1);
    b(mk(M, "2"), Action::SwitchTab2);
    b(mk(M, "3"), Action::SwitchTab3);
    b(mk(M, "4"), Action::SwitchTab4);
    b(mk(M, "5"), Action::SwitchTab5);
    b(mk(M, "6"), Action::SwitchTab6);
    b(mk(M, "7"), Action::SwitchTab7);
    b(mk(M, "8"), Action::SwitchTab8);
    b(mk(M, "9"), Action::SwitchTab9);

    // Clipboard
    b(mk(M, "c"),       Action::Copy);
    b(mk(M, "v"),       Action::Paste);
    b(mk(MS, "v"),      Action::PasteFromHistory);
    b(mk(M, "a"),       Action::SelectAll);

    // Search
    b(mk(M, "f"),       Action::SearchOpen);
    b(mk(M, "g"),       Action::SearchNext);
    b(mk(MS, "g"),      Action::SearchPrev);

    // Font
    b(mk(M, "="),       Action::FontIncrease);
    b(mk(M, "-"),       Action::FontDecrease);
    b(mk(M, "0"),       Action::FontReset);

    // Scroll
    b("shift+pageup",   Action::ScrollPageUp);
    b("shift+pagedown", Action::ScrollPageDown);
    b("shift+home",     Action::ScrollToTop);
    b("shift+end",      Action::ScrollToBottom);

    // Window
    b(mk(M, "n"),       Action::NewWindow);
    b(mk(M, "enter"),   Action::ToggleFullscreen);

    // Misc
    b(mk(M, "k"),       Action::ClearScrollback);
    b(mk(M, "up"),      Action::JumpPromptUp);
    b(mk(M, "down"),    Action::JumpPromptDown);
    b(mk(MS, ","),      Action::ReloadConfig);
    b(mk(MS, "x"),      Action::EnterCopyMode);
    b(mk(MS, "b"),      Action::ToggleSidebar);

    // Hub / Settings
    b(mk(M, ","),       Action::OpenSettings);
    b(mk(MS, "t"),      Action::OpenThemeHub);
    b(mk(MS, "p"),      Action::OpenFontHub);
}

} // namespace termcore
