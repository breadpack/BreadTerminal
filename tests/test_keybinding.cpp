#include <gtest/gtest.h>
#include "termcore/keybinding.h"

using namespace termcore;

class KeybindingTest : public ::testing::Test {
protected:
    KeybindingManager mgr;
};

// 1. Default bindings exist
TEST_F(KeybindingTest, DefaultBindingsExist) {
    EXPECT_GT(mgr.count(), 0u);
}

// 2. lookup cmd+t -> NewTab
TEST_F(KeybindingTest, LookupCmdT_NewTab) {
    auto combo = KeybindingManager::parseCombo("cmd+t");
    EXPECT_EQ(mgr.lookup(combo), Action::NewTab);
}

// 3. lookup cmd+c -> Copy
TEST_F(KeybindingTest, LookupCmdC_Copy) {
    auto combo = KeybindingManager::parseCombo("cmd+c");
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
    auto combo = KeybindingManager::parseCombo("cmd+t");
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
    auto combo = KeybindingManager::parseCombo("cmd+t");
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
