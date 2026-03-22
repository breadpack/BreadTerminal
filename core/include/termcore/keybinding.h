#ifndef TERMCORE_KEYBINDING_H
#define TERMCORE_KEYBINDING_H

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace termcore {

/// Modifier key flags
enum KeyMod : uint8_t {
    ModNone  = 0,
    ModShift = 1,
    ModCtrl  = 2,
    ModAlt   = 4,
    ModSuper = 8,  // Cmd on macOS, Win on Windows
};

/// Terminal actions that keybindings can trigger
enum class Action : uint16_t {
    None,
    // Tab/Pane
    NewTab, CloseTab, NextTab, PrevTab,
    SplitRight, SplitDown, ClosePane,
    FocusUp, FocusDown, FocusLeft, FocusRight,
    // Clipboard
    Copy, Paste, SelectAll,
    // Scroll
    ScrollUp, ScrollDown, ScrollPageUp, ScrollPageDown, ScrollToTop, ScrollToBottom,
    // Search
    SearchOpen, SearchNext, SearchPrev, SearchClose,
    // Window
    NewWindow, CloseWindow, ToggleFullscreen,
    // Font
    FontIncrease, FontDecrease, FontReset,
    // Misc
    ResetTerminal, ClearScrollback, ShowNotifications, ReloadConfig,
    // Prompt navigation (OSC 133)
    JumpPromptUp, JumpPromptDown,
    // Copy mode (vi-style navigation)
    EnterCopyMode,
    // Sidebar / Workspace
    ToggleSidebar,
    SwitchWorkspace1, SwitchWorkspace2, SwitchWorkspace3, SwitchWorkspace4,
    SwitchWorkspace5, SwitchWorkspace6, SwitchWorkspace7, SwitchWorkspace8,
    // Tab switching by number (1-9)
    SwitchTab1, SwitchTab2, SwitchTab3, SwitchTab4, SwitchTab5,
    SwitchTab6, SwitchTab7, SwitchTab8, SwitchTab9,
    // Hub / Settings windows
    OpenSettings, OpenThemeHub, OpenFontHub,
    // Profile shortcuts (open new tab with a specific profile)
    NewTabProfile1, NewTabProfile2, NewTabProfile3,
    NewTabProfile4, NewTabProfile5, NewTabProfile6,
    NewTabProfile7, NewTabProfile8, NewTabProfile9,
    ShowProfileDropdown,
    // Custom (string-based action)
    Custom,
};

/// A key combination (trigger)
struct KeyCombo {
    uint32_t keycode;  // Virtual keycode or Unicode codepoint
    uint8_t mods;      // KeyMod flags combined

    bool operator==(const KeyCombo& o) const {
        return keycode == o.keycode && mods == o.mods;
    }
};

/// A single keybinding
struct Keybinding {
    KeyCombo combo;
    Action action;
    std::string custom_action;  // For Action::Custom
};

/// Available keybinding presets for easy migration from other terminals
enum class KeymapPreset {
    Default,           // BreadTerminal native keybindings
    Ghostty,           // Ghostty-style keybindings
    Kitty,             // Kitty-style keybindings
    Tmux,              // tmux-style keybindings (Ctrl+B prefix emulated)
    Warp,              // Warp-style keybindings
    WindowsTerminal,   // Windows Terminal-style keybindings
    Alacritty,         // Alacritty-style keybindings
    ITerm2,            // iTerm2-style keybindings (macOS)
};

/// Parse a preset name string. Returns Default if unrecognized.
KeymapPreset parseKeymapPreset(const std::string& name);

/// Get the display name of a preset
std::string keymapPresetName(KeymapPreset preset);

/// List all available preset names
std::vector<std::string> listKeymapPresets();

/// Get the keybinding definitions for a preset (trigger, action pairs)
std::vector<std::pair<std::string, std::string>> keymapPresetBindings(KeymapPreset preset);

/// Manages keybindings with default and user-configurable mappings
class KeybindingManager {
public:
    KeybindingManager();
    ~KeybindingManager() = default;

    /// Add or override a keybinding
    void bind(const KeyCombo& combo, Action action, const std::string& custom = "");

    /// Remove a keybinding
    void unbind(const KeyCombo& combo);

    /// Look up action for a key combination. Returns Action::None if unbound.
    Action lookup(const KeyCombo& combo) const;

    /// Get custom action string (for Action::Custom)
    std::string lookupCustom(const KeyCombo& combo) const;

    /// Parse a trigger string like "cmd+t", "ctrl+shift+c" into KeyCombo
    static KeyCombo parseCombo(const std::string& trigger);

    /// Parse an action string like "new_tab", "copy" into Action
    static Action parseAction(const std::string& action_str);

    /// Load keybindings from config-style strings: "trigger=action"
    void loadFromConfig(const std::vector<std::pair<std::string, std::string>>& bindings);

    /// Load a preset, replacing all current bindings
    void loadPreset(KeymapPreset preset);

    /// Get all current bindings
    const std::vector<Keybinding>& allBindings() const { return bindings_; }

    /// Reset to defaults
    void resetDefaults();

    /// Number of bindings
    size_t count() const { return bindings_.size(); }

private:
    void initDefaults();
    std::vector<Keybinding> bindings_;
};

} // namespace termcore

// Hash for KeyCombo
template<>
struct std::hash<termcore::KeyCombo> {
    size_t operator()(const termcore::KeyCombo& k) const noexcept {
        return std::hash<uint32_t>{}(k.keycode) ^ (std::hash<uint8_t>{}(k.mods) << 16);
    }
};

#endif
