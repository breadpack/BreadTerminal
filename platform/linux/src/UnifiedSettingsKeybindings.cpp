#if defined(__linux__)

#include "UnifiedSettingsWindow.h"
#include "termcore/keybinding.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace termcore {

// ---------------------------------------------------------------------------
// Display string helpers
// ---------------------------------------------------------------------------

std::string UnifiedSettingsWindow::comboToDisplayString(const KeyCombo& combo) const {
    std::string result;

    if (combo.mods & ModCtrl)  result += "Ctrl+";
    if (combo.mods & ModAlt)   result += "Alt+";
    if (combo.mods & ModShift) result += "Shift+";
    if (combo.mods & ModSuper) result += "Super+";

    static const std::unordered_map<uint32_t, const char*> keyNames = {
        {0xF700, "Up"}, {0xF701, "Down"}, {0xF702, "Left"}, {0xF703, "Right"},
        {0xF704, "Home"}, {0xF705, "End"},
        {0xF706, "PageUp"}, {0xF707, "PageDown"},
        {0xF708, "Tab"}, {0xF709, "Enter"}, {0xF70A, "Escape"},
        {0xF70B, "Backspace"}, {0xF70C, "Space"}, {0xF70D, "Delete"},
        {0xF710, "F1"}, {0xF711, "F2"}, {0xF712, "F3"}, {0xF713, "F4"},
        {0xF714, "F5"}, {0xF715, "F6"}, {0xF716, "F7"}, {0xF717, "F8"},
        {0xF718, "F9"}, {0xF719, "F10"}, {0xF71A, "F11"}, {0xF71B, "F12"},
    };

    auto it = keyNames.find(combo.keycode);
    if (it != keyNames.end()) {
        result += it->second;
    } else if (combo.keycode >= 'a' && combo.keycode <= 'z') {
        result += static_cast<char>(combo.keycode - 'a' + 'A');
    } else if (combo.keycode >= '0' && combo.keycode <= '9') {
        result += static_cast<char>(combo.keycode);
    } else if (combo.keycode >= 0x20 && combo.keycode <= 0x7E) {
        result += static_cast<char>(combo.keycode);
    } else {
        result += "?";
    }

    return result;
}

std::string UnifiedSettingsWindow::actionToDisplayString(Action action) const {
    static const std::unordered_map<Action, const char*> names = {
        {Action::None, "None"},
        {Action::NewTab, "New Tab"}, {Action::CloseTab, "Close Tab"},
        {Action::NextTab, "Next Tab"}, {Action::PrevTab, "Previous Tab"},
        {Action::SplitRight, "Split Right"}, {Action::SplitDown, "Split Down"},
        {Action::ClosePane, "Close Pane"},
        {Action::FocusUp, "Focus Up"}, {Action::FocusDown, "Focus Down"},
        {Action::FocusLeft, "Focus Left"}, {Action::FocusRight, "Focus Right"},
        {Action::Copy, "Copy"}, {Action::Paste, "Paste"},
        {Action::PasteFromHistory, "Paste from History"},
        {Action::SelectAll, "Select All"},
        {Action::ScrollUp, "Scroll Up"}, {Action::ScrollDown, "Scroll Down"},
        {Action::ScrollPageUp, "Scroll Page Up"},
        {Action::ScrollPageDown, "Scroll Page Down"},
        {Action::ScrollToTop, "Scroll to Top"},
        {Action::ScrollToBottom, "Scroll to Bottom"},
        {Action::SearchOpen, "Open Search"}, {Action::SearchNext, "Search Next"},
        {Action::SearchPrev, "Search Previous"},
        {Action::SearchClose, "Close Search"},
        {Action::NewWindow, "New Window"}, {Action::CloseWindow, "Close Window"},
        {Action::ToggleFullscreen, "Toggle Fullscreen"},
        {Action::FontIncrease, "Increase Font Size"},
        {Action::FontDecrease, "Decrease Font Size"},
        {Action::FontReset, "Reset Font Size"},
        {Action::ResetTerminal, "Reset Terminal"},
        {Action::ClearScrollback, "Clear Scrollback"},
        {Action::ShowNotifications, "Show Notifications"},
        {Action::ReloadConfig, "Reload Config"},
        {Action::JumpPromptUp, "Jump Prompt Up"},
        {Action::JumpPromptDown, "Jump Prompt Down"},
        {Action::EnterCopyMode, "Enter Copy Mode"},
        {Action::ToggleSidebar, "Toggle Sidebar"},
        {Action::SwitchWorkspace1, "Workspace 1"},
        {Action::SwitchWorkspace2, "Workspace 2"},
        {Action::SwitchWorkspace3, "Workspace 3"},
        {Action::SwitchWorkspace4, "Workspace 4"},
        {Action::SwitchWorkspace5, "Workspace 5"},
        {Action::SwitchWorkspace6, "Workspace 6"},
        {Action::SwitchWorkspace7, "Workspace 7"},
        {Action::SwitchWorkspace8, "Workspace 8"},
        {Action::SwitchTab1, "Switch to Tab 1"},
        {Action::SwitchTab2, "Switch to Tab 2"},
        {Action::SwitchTab3, "Switch to Tab 3"},
        {Action::SwitchTab4, "Switch to Tab 4"},
        {Action::SwitchTab5, "Switch to Tab 5"},
        {Action::SwitchTab6, "Switch to Tab 6"},
        {Action::SwitchTab7, "Switch to Tab 7"},
        {Action::SwitchTab8, "Switch to Tab 8"},
        {Action::SwitchTab9, "Switch to Tab 9"},
        {Action::OpenSettings, "Open Settings"},
        {Action::OpenThemeHub, "Open Theme Hub"},
        {Action::OpenFontHub, "Open Font Hub"},
        {Action::OpenCommandPalette, "Command Palette"},
        {Action::SshConnect, "SSH Connect"},
        {Action::ShowProfileDropdown, "Profile Dropdown"},
        {Action::NewTabProfile1, "New Tab (Profile 1)"},
        {Action::NewTabProfile2, "New Tab (Profile 2)"},
        {Action::NewTabProfile3, "New Tab (Profile 3)"},
        {Action::NewTabProfile4, "New Tab (Profile 4)"},
        {Action::NewTabProfile5, "New Tab (Profile 5)"},
        {Action::NewTabProfile6, "New Tab (Profile 6)"},
        {Action::NewTabProfile7, "New Tab (Profile 7)"},
        {Action::NewTabProfile8, "New Tab (Profile 8)"},
        {Action::NewTabProfile9, "New Tab (Profile 9)"},
    };

    auto it = names.find(action);
    return it != names.end() ? it->second : "Custom";
}

// ---------------------------------------------------------------------------
// findConflict
// ---------------------------------------------------------------------------

int UnifiedSettingsWindow::findConflict(const KeyCombo& combo,
                                         int excludeIdx) const {
    const auto& bindings = keybindMgr_.allBindings();
    for (int i = 0; i < (int)bindings.size(); ++i) {
        if (i == excludeIdx) continue;
        if (bindings[i].combo == combo) return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// showKeybindingList
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::showKeybindingList() {
    // Load keybindings from config into the manager
    keybindMgr_.resetDefaults();
    if (!config_.keybindings.empty()) {
        std::vector<std::pair<std::string, std::string>> pairs;
        for (const auto& kb : config_.keybindings) {
            pairs.emplace_back(kb.trigger, kb.action);
        }
        keybindMgr_.loadFromConfig(pairs);
    }

    const auto& bindings = keybindMgr_.allBindings();

    // Description
    GtkWidget* desc = gtk_label_new(
        "Double-click a row or click Edit to change a keybinding.");
    gtk_label_set_xalign(GTK_LABEL(desc), 0.0);
    gtk_widget_add_css_class(desc, "setting-description");
    gtk_widget_set_margin_bottom(desc, 12);
    gtk_box_append(GTK_BOX(contentBox_), desc);

    // Button bar: Reset to Defaults
    GtkWidget* btnBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_bottom(btnBar, 12);

    GtkWidget* resetBtn = gtk_button_new_with_label("Reset to Defaults");
    g_object_set_data(G_OBJECT(resetBtn), "settings-window", this);
    g_signal_connect(resetBtn, "clicked",
        G_CALLBACK(+[](GtkButton* btn, gpointer) {
            auto* self = static_cast<UnifiedSettingsWindow*>(
                g_object_get_data(G_OBJECT(btn), "settings-window"));
            if (self) {
                self->keybindMgr_.resetDefaults();
                self->syncKeybindingsToConfig();
                self->notifySave();
                self->showCategoryContent(self->selectedCategoryId_);
            }
        }), nullptr);
    gtk_box_append(GTK_BOX(btnBar), resetBtn);
    gtk_box_append(GTK_BOX(contentBox_), btnBar);

    // Column headers
    GtkWidget* headerRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(headerRow, "setting-label");
    gtk_widget_set_margin_bottom(headerRow, 4);

    auto addHeaderCol = [&](const char* text, int width) {
        GtkWidget* lbl = gtk_label_new(text);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
        gtk_widget_set_size_request(lbl, width, -1);
        gtk_box_append(GTK_BOX(headerRow), lbl);
    };
    addHeaderCol("Action", 220);
    addHeaderCol("Shortcut", 200);
    addHeaderCol("", 80);

    gtk_box_append(GTK_BOX(contentBox_), headerRow);

    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_bottom(sep, 4);
    gtk_box_append(GTK_BOX(contentBox_), sep);

    if (bindings.empty()) {
        GtkWidget* emptyLabel = gtk_label_new("No keybindings configured.");
        gtk_widget_add_css_class(emptyLabel, "setting-description");
        gtk_box_append(GTK_BOX(contentBox_), emptyLabel);
        return;
    }

    for (int i = 0; i < (int)bindings.size(); ++i) {
        const auto& kb = bindings[i];

        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_margin_bottom(row, 2);
        if (i % 2 == 0)
            gtk_widget_add_css_class(row, "card-grid");

        // Action name
        std::string actionStr = actionToDisplayString(kb.action);
        if (kb.action == Action::Custom && !kb.custom_action.empty())
            actionStr = kb.custom_action;

        GtkWidget* actionLabel = gtk_label_new(actionStr.c_str());
        gtk_label_set_xalign(GTK_LABEL(actionLabel), 0.0);
        gtk_widget_set_size_request(actionLabel, 220, 32);
        gtk_widget_set_valign(actionLabel, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(row), actionLabel);

        // Shortcut badge
        std::string comboStr = comboToDisplayString(kb.combo);
        GtkWidget* comboLabel = gtk_label_new(comboStr.c_str());
        gtk_label_set_xalign(GTK_LABEL(comboLabel), 0.0);
        gtk_widget_set_size_request(comboLabel, 200, 32);
        gtk_widget_set_valign(comboLabel, GTK_ALIGN_CENTER);
        gtk_widget_add_css_class(comboLabel, "filter-button");
        gtk_box_append(GTK_BOX(row), comboLabel);

        // Edit button
        GtkWidget* editBtn = gtk_button_new_with_label("Edit");
        gtk_widget_set_valign(editBtn, GTK_ALIGN_CENTER);
        int* idxPtr = new int(i);
        g_object_set_data(G_OBJECT(editBtn), "settings-window", this);
        g_signal_connect(editBtn, "clicked",
            G_CALLBACK(+[](GtkButton* btn, gpointer data) {
                int idx = *static_cast<int*>(data);
                auto* self = static_cast<UnifiedSettingsWindow*>(
                    g_object_get_data(G_OBJECT(btn), "settings-window"));
                if (self) self->showKeyCaptureDialog(idx);
            }), idxPtr);
        g_signal_connect(editBtn, "destroy",
            G_CALLBACK(+[](GtkWidget*, gpointer data) {
                delete static_cast<int*>(data);
            }), idxPtr);
        gtk_box_append(GTK_BOX(row), editBtn);

        gtk_box_append(GTK_BOX(contentBox_), row);
    }
}

void UnifiedSettingsWindow::onKeybindingEdit(int idx) {
    showKeyCaptureDialog(idx);
}

} // namespace termcore

#endif // __linux__
