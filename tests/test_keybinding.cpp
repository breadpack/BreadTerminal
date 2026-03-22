#include <gtest/gtest.h>
#include "termcore/keybinding.h"

using namespace termcore;

// Platform-adaptive modifier: cmd on macOS, ctrl on Windows/Linux
#if defined(__APPLE__)
#define PM "cmd"
#define PMS "cmd+shift"
#else
#define PM "ctrl"
#define PMS "ctrl+shift"
#endif

class KeybindingTest : public ::testing::Test {
protected:
    KeybindingManager mgr;
};

// 1. Default bindings exist
TEST_F(KeybindingTest, DefaultBindingsExist) {
    EXPECT_GT(mgr.count(), 0u);
}

// 2. lookup platform_mod+t -> NewTab
TEST_F(KeybindingTest, LookupCmdT_NewTab) {
    auto combo = KeybindingManager::parseCombo(std::string(PM) + "+t");
    EXPECT_EQ(mgr.lookup(combo), Action::NewTab);
}

// 3. lookup platform_mod+c -> Copy
TEST_F(KeybindingTest, LookupCmdC_Copy) {
    auto combo = KeybindingManager::parseCombo(std::string(PM) + "+c");
    EXPECT_EQ(mgr.lookup(combo), Action::Copy);
}

// 4. lookup unbound key -> None
TEST_F(KeybindingTest, LookupUnboundKey_None) {
    auto combo = KeybindingManager::parseCombo("ctrl+alt+z");
    EXPECT_EQ(mgr.lookup(combo), Action::None);
}

// 5. bind new key -> lookup returns it
TEST_F(KeybindingTest, BindNewKey) {
    auto combo = KeybindingManager::parseCombo("ctrl+alt+n");
    mgr.bind(combo, Action::NewWindow);
    EXPECT_EQ(mgr.lookup(combo), Action::NewWindow);
}

// 6. unbind -> lookup returns None
TEST_F(KeybindingTest, Unbind) {
    auto combo = KeybindingManager::parseCombo(std::string(PM) + "+t");
    EXPECT_EQ(mgr.lookup(combo), Action::NewTab);
    mgr.unbind(combo);
    EXPECT_EQ(mgr.lookup(combo), Action::None);
}

// 7. parseCombo "cmd+t" -> correct mods and key
TEST_F(KeybindingTest, ParseCombo_CmdT) {
    auto combo = KeybindingManager::parseCombo("cmd+t");
    EXPECT_EQ(combo.keycode, static_cast<uint32_t>('t'));
    EXPECT_EQ(combo.mods, ModSuper);
}

// 8. parseCombo "ctrl+shift+c" -> correct mods
TEST_F(KeybindingTest, ParseCombo_CtrlShiftC) {
    auto combo = KeybindingManager::parseCombo("ctrl+shift+c");
    EXPECT_EQ(combo.keycode, static_cast<uint32_t>('c'));
    EXPECT_EQ(combo.mods, ModCtrl | ModShift);
}

// 9. parseAction "new_tab" -> NewTab
TEST_F(KeybindingTest, ParseAction_NewTab) {
    EXPECT_EQ(KeybindingManager::parseAction("new_tab"), Action::NewTab);
}

// 10. parseAction "copy" -> Copy
TEST_F(KeybindingTest, ParseAction_Copy) {
    EXPECT_EQ(KeybindingManager::parseAction("copy"), Action::Copy);
}

// 11. parseAction "unknown" -> None
TEST_F(KeybindingTest, ParseAction_Unknown) {
    EXPECT_EQ(KeybindingManager::parseAction("unknown"), Action::None);
}

// 12. loadFromConfig -> bindings loaded
TEST_F(KeybindingTest, LoadFromConfig) {
    size_t before = mgr.count();
    std::vector<std::pair<std::string, std::string>> config = {
        {"ctrl+alt+x", "close_window"},
        {"ctrl+alt+r", "reset_terminal"},
    };
    mgr.loadFromConfig(config);
    EXPECT_EQ(mgr.count(), before + 2);

    auto combo1 = KeybindingManager::parseCombo("ctrl+alt+x");
    EXPECT_EQ(mgr.lookup(combo1), Action::CloseWindow);

    auto combo2 = KeybindingManager::parseCombo("ctrl+alt+r");
    EXPECT_EQ(mgr.lookup(combo2), Action::ResetTerminal);
}

// 13. resetDefaults -> restores defaults
TEST_F(KeybindingTest, ResetDefaults) {
    size_t defaultCount = mgr.count();
    mgr.bind(KeybindingManager::parseCombo("ctrl+alt+z"), Action::NewWindow);
    EXPECT_EQ(mgr.count(), defaultCount + 1);
    mgr.resetDefaults();
    EXPECT_EQ(mgr.count(), defaultCount);
}

// 14. Override existing binding
TEST_F(KeybindingTest, OverrideExistingBinding) {
    auto combo = KeybindingManager::parseCombo(std::string(PM) + "+t");
    EXPECT_EQ(mgr.lookup(combo), Action::NewTab);
    size_t before = mgr.count();
    mgr.bind(combo, Action::CloseTab);
    EXPECT_EQ(mgr.lookup(combo), Action::CloseTab);
    EXPECT_EQ(mgr.count(), before);  // Count unchanged (overridden, not added)
}

// 15. Custom action binding
TEST_F(KeybindingTest, CustomActionBinding) {
    auto combo = KeybindingManager::parseCombo("ctrl+alt+p");
    mgr.bind(combo, Action::Custom, "run_my_script");
    EXPECT_EQ(mgr.lookup(combo), Action::Custom);
    EXPECT_EQ(mgr.lookupCustom(combo), "run_my_script");
}

// loadFromConfig with unrecognized action -> treated as Custom
TEST_F(KeybindingTest, LoadFromConfig_CustomAction) {
    std::vector<std::pair<std::string, std::string>> config = {
        {"ctrl+alt+m", "my_custom_action"},
    };
    mgr.loadFromConfig(config);
    auto combo = KeybindingManager::parseCombo("ctrl+alt+m");
    EXPECT_EQ(mgr.lookup(combo), Action::Custom);
    EXPECT_EQ(mgr.lookupCustom(combo), "my_custom_action");
}

// --- Keymap Preset Tests ---

// parseKeymapPreset: known names
TEST(KeymapPresetTest, ParseKnownPresets) {
    EXPECT_EQ(parseKeymapPreset("Ghostty"), KeymapPreset::Ghostty);
    EXPECT_EQ(parseKeymapPreset("ghostty"), KeymapPreset::Ghostty);
    EXPECT_EQ(parseKeymapPreset("Kitty"), KeymapPreset::Kitty);
    EXPECT_EQ(parseKeymapPreset("tmux"), KeymapPreset::Tmux);
    EXPECT_EQ(parseKeymapPreset("Warp"), KeymapPreset::Warp);
    EXPECT_EQ(parseKeymapPreset("Windows Terminal"), KeymapPreset::WindowsTerminal);
    EXPECT_EQ(parseKeymapPreset("windows_terminal"), KeymapPreset::WindowsTerminal);
    EXPECT_EQ(parseKeymapPreset("wt"), KeymapPreset::WindowsTerminal);
    EXPECT_EQ(parseKeymapPreset("Alacritty"), KeymapPreset::Alacritty);
    EXPECT_EQ(parseKeymapPreset("iTerm2"), KeymapPreset::ITerm2);
    EXPECT_EQ(parseKeymapPreset("iterm"), KeymapPreset::ITerm2);
}

// parseKeymapPreset: unknown falls back to Default
TEST(KeymapPresetTest, ParseUnknownReturnsDefault) {
    EXPECT_EQ(parseKeymapPreset("unknown_terminal"), KeymapPreset::Default);
    EXPECT_EQ(parseKeymapPreset(""), KeymapPreset::Default);
}

// keymapPresetName: round-trip
TEST(KeymapPresetTest, PresetNameRoundTrip) {
    EXPECT_EQ(keymapPresetName(KeymapPreset::Default), "Default");
    EXPECT_EQ(keymapPresetName(KeymapPreset::Ghostty), "Ghostty");
    EXPECT_EQ(keymapPresetName(KeymapPreset::Tmux), "tmux");
    EXPECT_EQ(keymapPresetName(KeymapPreset::WindowsTerminal), "Windows Terminal");
}

// listKeymapPresets: includes all presets
TEST(KeymapPresetTest, ListPresets) {
    auto presets = listKeymapPresets();
    EXPECT_EQ(presets.size(), 8u);
    // Check first and last
    EXPECT_EQ(presets[0], "Default");
    EXPECT_EQ(presets[7], "iTerm2");
}

// keymapPresetBindings: non-empty for all non-Default presets
TEST(KeymapPresetTest, BindingsNonEmptyForAllPresets) {
    EXPECT_TRUE(keymapPresetBindings(KeymapPreset::Default).empty());
    EXPECT_GT(keymapPresetBindings(KeymapPreset::Ghostty).size(), 10u);
    EXPECT_GT(keymapPresetBindings(KeymapPreset::Kitty).size(), 10u);
    EXPECT_GT(keymapPresetBindings(KeymapPreset::Tmux).size(), 10u);
    EXPECT_GT(keymapPresetBindings(KeymapPreset::Warp).size(), 10u);
    EXPECT_GT(keymapPresetBindings(KeymapPreset::WindowsTerminal).size(), 10u);
    EXPECT_GT(keymapPresetBindings(KeymapPreset::Alacritty).size(), 10u);
    EXPECT_GT(keymapPresetBindings(KeymapPreset::ITerm2).size(), 10u);
}

// loadPreset: Ghostty preset has NewTab binding
TEST_F(KeybindingTest, LoadPresetGhostty) {
    mgr.loadPreset(KeymapPreset::Ghostty);
    EXPECT_GT(mgr.count(), 0u);
    // Ghostty uses Mod+T for new tab
    auto combo = KeybindingManager::parseCombo(std::string(PM) + "+t");
    EXPECT_EQ(mgr.lookup(combo), Action::NewTab);
}

// loadPreset: tmux preset has Ctrl+Alt based bindings
TEST_F(KeybindingTest, LoadPresetTmux) {
    mgr.loadPreset(KeymapPreset::Tmux);
    EXPECT_GT(mgr.count(), 0u);
    // tmux: Ctrl+Alt+C for new tab (emulated prefix)
    auto combo = KeybindingManager::parseCombo("ctrl+alt+c");
    EXPECT_EQ(mgr.lookup(combo), Action::NewTab);
    // tmux: Ctrl+Alt+N for next tab
    auto comboN = KeybindingManager::parseCombo("ctrl+alt+n");
    EXPECT_EQ(mgr.lookup(comboN), Action::NextTab);
}

// loadPreset: Windows Terminal uses Ctrl+Shift bindings
TEST_F(KeybindingTest, LoadPresetWindowsTerminal) {
    mgr.loadPreset(KeymapPreset::WindowsTerminal);
    EXPECT_GT(mgr.count(), 0u);
    // WT: Ctrl+Shift+T for new tab
    auto combo = KeybindingManager::parseCombo("ctrl+shift+t");
    EXPECT_EQ(mgr.lookup(combo), Action::NewTab);
    // WT: Alt+Shift+D for split
    auto comboS = KeybindingManager::parseCombo("alt+shift+d");
    EXPECT_EQ(mgr.lookup(comboS), Action::SplitRight);
}

// loadPreset: Default resets to initDefaults
TEST_F(KeybindingTest, LoadPresetDefault) {
    size_t defaultCount = mgr.count();
    mgr.loadPreset(KeymapPreset::Ghostty);
    size_t ghosttyCount = mgr.count();
    // May differ from default count
    mgr.loadPreset(KeymapPreset::Default);
    EXPECT_EQ(mgr.count(), defaultCount);
}

// loadPreset then loadFromConfig: custom overrides on top of preset
TEST_F(KeybindingTest, PresetWithCustomOverride) {
    mgr.loadPreset(KeymapPreset::Ghostty);
    // Override Mod+T to CloseTab
    std::vector<std::pair<std::string, std::string>> overrides = {
        {std::string(PM) + "+t", "close_tab"},
    };
    mgr.loadFromConfig(overrides);
    auto combo = KeybindingManager::parseCombo(std::string(PM) + "+t");
    EXPECT_EQ(mgr.lookup(combo), Action::CloseTab);  // Overridden
}

// All presets produce valid bindings (no Action::None for valid action strings)
TEST(KeymapPresetTest, AllPresetsProduceValidBindings) {
    KeymapPreset presets[] = {
        KeymapPreset::Ghostty, KeymapPreset::Kitty, KeymapPreset::Tmux,
        KeymapPreset::Warp, KeymapPreset::WindowsTerminal,
        KeymapPreset::Alacritty, KeymapPreset::ITerm2,
    };
    for (auto preset : presets) {
        auto bindings = keymapPresetBindings(preset);
        for (const auto& [trigger, action] : bindings) {
            auto parsed = KeybindingManager::parseAction(action);
            EXPECT_NE(parsed, Action::None)
                << "Preset " << keymapPresetName(preset)
                << " has invalid action: " << action
                << " for trigger: " << trigger;
        }
    }
}

// --- New profile action tests ---

TEST(ActionParseTest, ProfileActions) {
    EXPECT_EQ(KeybindingManager::parseAction("new_tab_profile1"), Action::NewTabProfile1);
    EXPECT_EQ(KeybindingManager::parseAction("new_tab_profile9"), Action::NewTabProfile9);
    EXPECT_EQ(KeybindingManager::parseAction("show_profile_dropdown"), Action::ShowProfileDropdown);
}

TEST_F(KeybindingTest, PresetResolvesProfileShortcuts) {
    // All non-Default presets include commonGuiBindings which has Mod+Shift+1~9
    mgr.loadPreset(KeymapPreset::Ghostty);
    auto combo = KeybindingManager::parseCombo(std::string(PMS) + "+1");
    EXPECT_EQ(mgr.lookup(combo), Action::NewTabProfile1);
    auto combo9 = KeybindingManager::parseCombo(std::string(PMS) + "+9");
    EXPECT_EQ(mgr.lookup(combo9), Action::NewTabProfile9);
}
