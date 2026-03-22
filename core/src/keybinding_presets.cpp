#include "termcore/keybinding.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace termcore {

// Platform-adaptive modifier strings
#if defined(__APPLE__)
static constexpr const char* kMod  = "cmd";
static constexpr const char* kModS = "cmd+shift";
#else
static constexpr const char* kMod  = "ctrl";
static constexpr const char* kModS = "ctrl+shift";
#endif

using BindingList = std::vector<std::pair<std::string, std::string>>;

static std::string mk(const char* mod, const char* key) {
    return std::string(mod) + "+" + key;
}

// --- Common bindings shared by most GUI terminals ---
static BindingList commonGuiBindings() {
    return {
        {mk(kMod, "c"),       "copy"},
        {mk(kMod, "v"),       "paste"},
        {mk(kMod, "a"),       "select_all"},
        {mk(kMod, "f"),       "search_open"},
        {mk(kMod, "="),       "font_increase"},
        {mk(kMod, "-"),       "font_decrease"},
        {mk(kMod, "0"),       "font_reset"},
        {"shift+pageup",      "scroll_page_up"},
        {"shift+pagedown",    "scroll_page_down"},
        {"shift+home",        "scroll_to_top"},
        {"shift+end",         "scroll_to_bottom"},
        // Profile shortcuts: Mod+Shift+1~9 open new tab with profile N
        {mk(kModS, "1"),      "new_tab_profile1"},
        {mk(kModS, "2"),      "new_tab_profile2"},
        {mk(kModS, "3"),      "new_tab_profile3"},
        {mk(kModS, "4"),      "new_tab_profile4"},
        {mk(kModS, "5"),      "new_tab_profile5"},
        {mk(kModS, "6"),      "new_tab_profile6"},
        {mk(kModS, "7"),      "new_tab_profile7"},
        {mk(kModS, "8"),      "new_tab_profile8"},
        {mk(kModS, "9"),      "new_tab_profile9"},
    };
}

// --- Ghostty preset ---
// Ghostty uses Cmd (macOS) / Ctrl (Linux) + standard shortcuts.
// Split: Cmd+D / Cmd+Shift+D, pane nav: Cmd+[/]
static BindingList ghosttyBindings() {
    auto b = commonGuiBindings();
    // Tabs
    b.push_back({mk(kMod, "t"),       "new_tab"});
    b.push_back({mk(kMod, "w"),       "close_tab"});
    b.push_back({mk(kModS, "]"),      "next_tab"});
    b.push_back({mk(kModS, "["),      "prev_tab"});
    for (int i = 1; i <= 9; ++i)
        b.push_back({mk(kMod, std::to_string(i).c_str()), "switch_tab_" + std::to_string(i)});
    // Pane
    b.push_back({mk(kMod, "d"),       "split_right"});
    b.push_back({mk(kModS, "d"),      "split_down"});
    b.push_back({mk(kMod, "["),       "focus_left"});
    b.push_back({mk(kMod, "]"),       "focus_right"});
    // Window
    b.push_back({mk(kMod, "n"),       "new_window"});
    b.push_back({mk(kMod, "enter"),   "toggle_fullscreen"});
    // Misc
    b.push_back({mk(kMod, "k"),       "clear_scrollback"});
    b.push_back({mk(kMod, ","),       "open_settings"});
    b.push_back({mk(kModS, ","),      "reload_config"});
    // Search
    b.push_back({mk(kMod, "g"),       "search_next"});
    b.push_back({mk(kModS, "g"),      "search_prev"});
    return b;
}

// --- Kitty preset ---
// Kitty uses Cmd (macOS) / Ctrl+Shift (Linux) for most actions.
// Split: Cmd+Enter (new OS window), Ctrl+Shift+Enter (new window in tab)
static BindingList kittyBindings() {
    auto b = commonGuiBindings();
#if defined(__APPLE__)
    const char* km = "cmd";
    const char* kms = "cmd+shift";
#else
    const char* km = "ctrl+shift";
    const char* kms = "ctrl+shift";
#endif
    // Tabs
    b.push_back({mk(km, "t"),        "new_tab"});
    b.push_back({mk(km, "w"),        "close_tab"});
    b.push_back({mk(km, "right"),    "next_tab"});
    b.push_back({mk(km, "left"),     "prev_tab"});
    // Kitty uses Alt+1~9 for tab switching on Linux
#if defined(__APPLE__)
    for (int i = 1; i <= 9; ++i)
        b.push_back({mk("cmd", std::to_string(i).c_str()), "switch_tab_" + std::to_string(i)});
#else
    for (int i = 1; i <= 9; ++i)
        b.push_back({mk("alt", std::to_string(i).c_str()), "switch_tab_" + std::to_string(i)});
#endif
    // Pane (Kitty calls them "windows")
    b.push_back({mk(km, "enter"),    "split_right"});
    b.push_back({mk(km, "n"),        "new_window"});
    b.push_back({mk(km, "["),        "focus_left"});
    b.push_back({mk(km, "]"),        "focus_right"});
    b.push_back({mk(kms, "up"),      "focus_up"});
    b.push_back({mk(kms, "down"),    "focus_down"});
    // Window
    b.push_back({"f11",              "toggle_fullscreen"});
    // Misc
    b.push_back({mk(km, "delete"),   "clear_scrollback"});
    b.push_back({mk(km, "f5"),       "reload_config"});
    b.push_back({mk(km, "f2"),       "open_settings"});
    // Search
    b.push_back({mk(km, "g"),        "search_next"});
    b.push_back({mk(kms, "g"),       "search_prev"});
    // Copy mode (Kitty uses hints)
    b.push_back({mk(km, "h"),        "enter_copy_mode"});
    return b;
}

// --- tmux-style preset ---
// Emulates tmux prefix key (Ctrl+B) as Ctrl+B, followed by the action key.
// Since BreadTerminal doesn't support 2-key sequences, we map Ctrl+B+key
// as Ctrl+Alt+key for single-chord approximation.
static BindingList tmuxBindings() {
    auto b = commonGuiBindings();
    // Use Ctrl+Alt as prefix substitute (Ctrl+B → Ctrl+Alt)
    const char* px = "ctrl+alt";

    // Tabs (tmux "windows")
    b.push_back({mk(px, "c"),        "new_tab"});
    b.push_back({mk(px, "x"),        "close_tab"});
    b.push_back({mk(px, "n"),        "next_tab"});
    b.push_back({mk(px, "p"),        "prev_tab"});
    for (int i = 1; i <= 9; ++i)
        b.push_back({mk(px, std::to_string(i).c_str()), "switch_tab_" + std::to_string(i)});
    // Pane split
    b.push_back({mk(px, "%"),        "split_right"});    // Ctrl+Alt+5 (% = Shift+5)
    b.push_back({mk(px, "\""),       "split_down"});     // Ctrl+Alt+" not practical...
    // Practical alternatives
    b.push_back({mk(px, "v"),        "split_right"});    // vim-tmux-navigator style
    b.push_back({mk(px, "s"),        "split_down"});
    // Pane navigation (vim-style)
    b.push_back({mk(px, "h"),        "focus_left"});
    b.push_back({mk(px, "j"),        "focus_down"});
    b.push_back({mk(px, "k"),        "focus_up"});
    b.push_back({mk(px, "l"),        "focus_right"});
    // Also arrow keys
    b.push_back({mk(px, "up"),       "focus_up"});
    b.push_back({mk(px, "down"),     "focus_down"});
    b.push_back({mk(px, "left"),     "focus_left"});
    b.push_back({mk(px, "right"),    "focus_right"});
    // Window
    b.push_back({mk(px, "f"),        "toggle_fullscreen"});
    // Copy mode (tmux: prefix + [)
    b.push_back({mk(px, "["),        "enter_copy_mode"});
    // Misc
    b.push_back({mk(px, "r"),        "reload_config"});
    b.push_back({mk(px, ","),        "open_settings"});
    b.push_back({mk(px, "z"),        "toggle_fullscreen"});  // tmux zoom
    return b;
}

// --- Warp preset ---
// Warp uses Cmd (macOS) / Ctrl (other) and focuses on block-based navigation.
// Prompt navigation is a core feature.
static BindingList warpBindings() {
    auto b = commonGuiBindings();
    // Tabs
    b.push_back({mk(kMod, "t"),       "new_tab"});
    b.push_back({mk(kMod, "w"),       "close_tab"});
    b.push_back({mk(kModS, "]"),      "next_tab"});
    b.push_back({mk(kModS, "["),      "prev_tab"});
    for (int i = 1; i <= 9; ++i)
        b.push_back({mk(kMod, std::to_string(i).c_str()), "switch_tab_" + std::to_string(i)});
    // Pane — Warp uses Cmd+D / Cmd+Shift+D
    b.push_back({mk(kMod, "d"),       "split_right"});
    b.push_back({mk(kModS, "d"),      "split_down"});
    // Pane navigation
    b.push_back({mk(kMod, "["),       "focus_left"});
    b.push_back({mk(kMod, "]"),       "focus_right"});
    // Window
    b.push_back({mk(kMod, "n"),       "new_window"});
    b.push_back({mk(kMod, "enter"),   "toggle_fullscreen"});
    // Prompt navigation (Warp's signature feature)
    b.push_back({mk(kMod, "up"),      "jump_prompt_up"});
    b.push_back({mk(kMod, "down"),    "jump_prompt_down"});
    // Misc
    b.push_back({mk(kMod, "k"),       "clear_scrollback"});
    b.push_back({mk(kMod, ","),       "open_settings"});
    b.push_back({mk(kMod, "p"),       "open_font_hub"});  // Warp command palette
    // Search
    b.push_back({mk(kMod, "g"),       "search_next"});
    b.push_back({mk(kModS, "g"),      "search_prev"});
    return b;
}

// --- Windows Terminal preset ---
// Uses Ctrl+Shift for most actions (avoids conflicts with shell shortcuts).
static BindingList windowsTerminalBindings() {
    auto b = commonGuiBindings();
    // Tabs
    b.push_back({"ctrl+shift+t",      "new_tab"});
    b.push_back({"ctrl+shift+w",      "close_tab"});
    b.push_back({"ctrl+tab",          "next_tab"});
    b.push_back({"ctrl+shift+tab",    "prev_tab"});
    for (int i = 1; i <= 9; ++i)
        b.push_back({mk("ctrl+alt", std::to_string(i).c_str()), "switch_tab_" + std::to_string(i)});
    // Pane
    b.push_back({"alt+shift+d",       "split_right"});
    b.push_back({"alt+shift+-",       "split_down"});
    b.push_back({"alt+up",            "focus_up"});
    b.push_back({"alt+down",          "focus_down"});
    b.push_back({"alt+left",          "focus_left"});
    b.push_back({"alt+right",         "focus_right"});
    b.push_back({"ctrl+shift+w",      "close_pane"});
    // Window
    b.push_back({"f11",               "toggle_fullscreen"});
    b.push_back({"alt+f4",            "close_window"});
    // Clipboard (Windows Terminal uses Ctrl+Shift for copy/paste too)
    b.push_back({"ctrl+shift+c",      "copy"});
    b.push_back({"ctrl+shift+v",      "paste"});
    // Font
    b.push_back({"ctrl+=",            "font_increase"});
    b.push_back({"ctrl+-",            "font_decrease"});
    b.push_back({"ctrl+0",            "font_reset"});
    // Search
    b.push_back({"ctrl+shift+f",      "search_open"});
    // Misc
    b.push_back({"ctrl+shift+,",      "open_settings"});
    b.push_back({"ctrl+shift+p",      "open_font_hub"});  // Command palette
    return b;
}

// --- Alacritty preset ---
// Minimal, keyboard-driven. Uses Ctrl+Shift on Linux, Cmd on macOS.
static BindingList alacrittyBindings() {
    auto b = commonGuiBindings();
#if defined(__APPLE__)
    const char* am = "cmd";
    const char* ams = "cmd+shift";
#else
    const char* am = "ctrl+shift";
    const char* ams = "ctrl+shift";
#endif
    // Alacritty has no built-in tabs, but we map common extensions
    b.push_back({mk(am, "t"),        "new_tab"});
    b.push_back({mk(am, "w"),        "close_tab"});
    b.push_back({mk(am, "n"),        "new_window"});
    // Copy/Paste (Alacritty: Ctrl+Shift+C/V on Linux)
    b.push_back({mk(am, "c"),        "copy"});
    b.push_back({mk(am, "v"),        "paste"});
    // Font
    b.push_back({mk(am, "="),        "font_increase"});
    b.push_back({mk(am, "-"),        "font_decrease"});
    b.push_back({mk(am, "0"),        "font_reset"});
    // Search (vi-mode)
    b.push_back({mk(am, "f"),        "search_open"});
    // Fullscreen
    b.push_back({"f11",              "toggle_fullscreen"});
    // Vi mode
    b.push_back({mk(ams, "space"),   "enter_copy_mode"});
    // Misc
    b.push_back({mk(am, "k"),        "clear_scrollback"});
    return b;
}

// --- iTerm2 preset (macOS-focused) ---
// Uses Cmd extensively. Unique: Cmd+Shift+Enter for maximize pane.
static BindingList iterm2Bindings() {
    auto b = commonGuiBindings();
    // Always Cmd-based (iTerm2 is macOS-only, but we adapt for cross-platform)
    // Tabs
    b.push_back({mk(kMod, "t"),       "new_tab"});
    b.push_back({mk(kMod, "w"),       "close_tab"});
    b.push_back({mk(kModS, "]"),      "next_tab"});
    b.push_back({mk(kModS, "["),      "prev_tab"});
    for (int i = 1; i <= 9; ++i)
        b.push_back({mk(kMod, std::to_string(i).c_str()), "switch_tab_" + std::to_string(i)});
    // Pane
    b.push_back({mk(kMod, "d"),       "split_right"});
    b.push_back({mk(kModS, "d"),      "split_down"});
    b.push_back({mk(kMod, "["),       "focus_left"});
    b.push_back({mk(kMod, "]"),       "focus_right"});
    b.push_back({"alt+up",            "focus_up"});
    b.push_back({"alt+down",          "focus_down"});
    b.push_back({"alt+left",          "focus_left"});
    b.push_back({"alt+right",         "focus_right"});
    // Window
    b.push_back({mk(kMod, "n"),       "new_window"});
    b.push_back({mk(kMod, "enter"),   "toggle_fullscreen"});
    // Search
    b.push_back({mk(kMod, "g"),       "search_next"});
    b.push_back({mk(kModS, "g"),      "search_prev"});
    // Misc
    b.push_back({mk(kMod, "k"),       "clear_scrollback"});
    b.push_back({mk(kMod, ","),       "open_settings"});
    b.push_back({mk(kModS, ","),      "reload_config"});
    // Broadcast / sidebar (iTerm2: Cmd+Shift+I for broadcast)
    b.push_back({mk(kModS, "b"),      "toggle_sidebar"});
    return b;
}

// --- Public API ---

KeymapPreset parseKeymapPreset(const std::string& name) {
    std::string lower;
    lower.reserve(name.size());
    for (char c : name) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // Strip spaces and underscores for fuzzy match
    std::string normalized;
    for (char c : lower) {
        if (c != ' ' && c != '_' && c != '-') normalized += c;
    }

    static const std::unordered_map<std::string, KeymapPreset> map = {
        {"default",          KeymapPreset::Default},
        {"breadterminal",    KeymapPreset::Default},
        {"ghostty",          KeymapPreset::Ghostty},
        {"kitty",            KeymapPreset::Kitty},
        {"tmux",             KeymapPreset::Tmux},
        {"warp",             KeymapPreset::Warp},
        {"windowsterminal",  KeymapPreset::WindowsTerminal},
        {"wt",               KeymapPreset::WindowsTerminal},
        {"alacritty",        KeymapPreset::Alacritty},
        {"iterm2",           KeymapPreset::ITerm2},
        {"iterm",            KeymapPreset::ITerm2},
    };

    auto it = map.find(normalized);
    return (it != map.end()) ? it->second : KeymapPreset::Default;
}

std::string keymapPresetName(KeymapPreset preset) {
    switch (preset) {
        case KeymapPreset::Default:          return "Default";
        case KeymapPreset::Ghostty:          return "Ghostty";
        case KeymapPreset::Kitty:            return "Kitty";
        case KeymapPreset::Tmux:             return "tmux";
        case KeymapPreset::Warp:             return "Warp";
        case KeymapPreset::WindowsTerminal:  return "Windows Terminal";
        case KeymapPreset::Alacritty:        return "Alacritty";
        case KeymapPreset::ITerm2:           return "iTerm2";
    }
    return "Default";
}

std::vector<std::string> listKeymapPresets() {
    return {
        "Default", "Ghostty", "Kitty", "tmux",
        "Warp", "Windows Terminal", "Alacritty", "iTerm2",
    };
}

std::vector<std::pair<std::string, std::string>> keymapPresetBindings(KeymapPreset preset) {
    switch (preset) {
        case KeymapPreset::Ghostty:          return ghosttyBindings();
        case KeymapPreset::Kitty:            return kittyBindings();
        case KeymapPreset::Tmux:             return tmuxBindings();
        case KeymapPreset::Warp:             return warpBindings();
        case KeymapPreset::WindowsTerminal:  return windowsTerminalBindings();
        case KeymapPreset::Alacritty:        return alacrittyBindings();
        case KeymapPreset::ITerm2:           return iterm2Bindings();
        case KeymapPreset::Default:
        default:
            return {};  // Default uses initDefaults() path
    }
}

} // namespace termcore
