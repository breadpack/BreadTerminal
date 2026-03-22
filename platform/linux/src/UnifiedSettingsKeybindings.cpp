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

    // Map keycode back to name
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
// findConflict — check if a combo conflicts with another binding
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
// GDK keyval to core keycode (same mapping as TerminalInput.cpp)
// ---------------------------------------------------------------------------

static uint32_t gdkKeyToCore(guint keyval) {
    switch (keyval) {
        case GDK_KEY_Up:        return 0xF700;
        case GDK_KEY_Down:      return 0xF701;
        case GDK_KEY_Left:      return 0xF702;
        case GDK_KEY_Right:     return 0xF703;
        case GDK_KEY_Home:      return 0xF704;
        case GDK_KEY_End:       return 0xF705;
        case GDK_KEY_Page_Up:   return 0xF706;
        case GDK_KEY_Page_Down: return 0xF707;
        case GDK_KEY_Tab:       return 0xF708;
        case GDK_KEY_ISO_Left_Tab: return 0xF708;
        case GDK_KEY_Return:    return 0xF709;
        case GDK_KEY_KP_Enter:  return 0xF709;
        case GDK_KEY_Escape:    return 0xF70A;
        case GDK_KEY_BackSpace: return 0xF70B;
        case GDK_KEY_space:     return 0xF70C;
        case GDK_KEY_Delete:    return 0xF70D;
        case GDK_KEY_F1:        return 0xF710;
        case GDK_KEY_F2:        return 0xF711;
        case GDK_KEY_F3:        return 0xF712;
        case GDK_KEY_F4:        return 0xF713;
        case GDK_KEY_F5:        return 0xF714;
        case GDK_KEY_F6:        return 0xF715;
        case GDK_KEY_F7:        return 0xF716;
        case GDK_KEY_F8:        return 0xF717;
        case GDK_KEY_F9:        return 0xF718;
        case GDK_KEY_F10:       return 0xF719;
        case GDK_KEY_F11:       return 0xF71A;
        case GDK_KEY_F12:       return 0xF71B;
        default: break;
    }
    if (keyval >= GDK_KEY_A && keyval <= GDK_KEY_Z)
        return keyval - GDK_KEY_A + 'a';
    if (keyval >= GDK_KEY_a && keyval <= GDK_KEY_z)
        return keyval;
    if (keyval >= GDK_KEY_0 && keyval <= GDK_KEY_9)
        return keyval;
    if (keyval >= 0x20 && keyval <= 0x7E)
        return keyval;
    return 0;
}

// ---------------------------------------------------------------------------
// showKeyCaptureDialog — modal dialog that grabs the next key combo
// ---------------------------------------------------------------------------

struct KeyCaptureData {
    UnifiedSettingsWindow* self;
    int bindingIdx;
    GtkWidget* dialog;
    GtkWidget* label;
    KeyCombo captured;
    bool gotKey;
};

void UnifiedSettingsWindow::showKeyCaptureDialog(int idx) {
    const auto& bindings = keybindMgr_.allBindings();
    if (idx < 0 || idx >= (int)bindings.size()) return;

    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Press a key combination");
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), window_);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 400, 150);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);

    std::string actionName = actionToDisplayString(bindings[idx].action);
    std::string promptText = "Rebinding: " + actionName +
                             "\nPress the new key combination, or Escape to cancel.";
    GtkWidget* label = gtk_label_new(promptText.c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.5);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget* comboLabel = gtk_label_new("Waiting...");
    gtk_widget_add_css_class(comboLabel, "setting-label");
    gtk_label_set_xalign(GTK_LABEL(comboLabel), 0.5);
    gtk_box_append(GTK_BOX(box), comboLabel);

    gtk_window_set_child(GTK_WINDOW(dialog), box);

    auto* data = new KeyCaptureData{this, idx, dialog, comboLabel, {}, false};
    g_object_set_data(G_OBJECT(dialog), "capture-data", data);

    // Key event controller
    GtkEventController* keyCtrl = gtk_event_controller_key_new();
    g_signal_connect(keyCtrl, "key-pressed",
        G_CALLBACK(+[](GtkEventControllerKey* ctrl,
                        guint keyval, guint /*keycode*/,
                        GdkModifierType state,
                        gpointer userData) -> gboolean {
            auto* data = static_cast<KeyCaptureData*>(userData);

            // Ignore lone modifier keys
            if (keyval == GDK_KEY_Shift_L || keyval == GDK_KEY_Shift_R ||
                keyval == GDK_KEY_Control_L || keyval == GDK_KEY_Control_R ||
                keyval == GDK_KEY_Alt_L || keyval == GDK_KEY_Alt_R ||
                keyval == GDK_KEY_Super_L || keyval == GDK_KEY_Super_R ||
                keyval == GDK_KEY_Meta_L || keyval == GDK_KEY_Meta_R) {
                return TRUE;
            }

            // Escape without modifiers = cancel
            if (keyval == GDK_KEY_Escape &&
                !(state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK))) {
                gtk_window_destroy(GTK_WINDOW(data->dialog));
                delete data;
                return TRUE;
            }

            // Build KeyCombo
            KeyCombo combo;
            combo.keycode = gdkKeyToCore(keyval);
            combo.mods = ModNone;
            if (state & GDK_SHIFT_MASK)   combo.mods |= ModShift;
            if (state & GDK_CONTROL_MASK) combo.mods |= ModCtrl;
            if (state & GDK_ALT_MASK)     combo.mods |= ModAlt;
            if (state & GDK_SUPER_MASK)   combo.mods |= ModSuper;

            if (combo.keycode == 0) return TRUE;  // Unknown key

            data->captured = combo;
            data->gotKey = true;

            std::string display = data->self->comboToDisplayString(combo);

            // Check for conflicts
            int conflict = data->self->findConflict(combo, data->bindingIdx);
            if (conflict >= 0) {
                const auto& bindings = data->self->keybindMgr_.allBindings();
                std::string conflictAction =
                    data->self->actionToDisplayString(bindings[conflict].action);
                display += "  (conflicts with: " + conflictAction + ")";
            }

            gtk_label_set_text(GTK_LABEL(data->label), display.c_str());

            // Apply the binding
            auto& bindings = data->self->keybindMgr_.allBindings();
            if (data->bindingIdx < (int)bindings.size()) {
                // Remove conflicting binding if any
                if (conflict >= 0) {
                    data->self->keybindMgr_.unbind(combo);
                }
                // Update the binding
                Action action = bindings[data->bindingIdx].action;
                std::string custom = bindings[data->bindingIdx].custom_action;
                data->self->keybindMgr_.unbind(
                    bindings[data->bindingIdx].combo);
                data->self->keybindMgr_.bind(combo, action, custom);

                // Update config keybindings
                data->self->syncKeybindingsToConfig();
                data->self->notifySave();
            }

            // Close after a short delay to show the result
            auto* dlg = data->dialog;
            auto* captureData = data;
            g_timeout_add(400, +[](gpointer d) -> gboolean {
                auto* cd = static_cast<KeyCaptureData*>(d);
                gtk_window_destroy(GTK_WINDOW(cd->dialog));
                cd->self->showCategoryContent(cd->self->selectedCategoryId_);
                delete cd;
                return G_SOURCE_REMOVE;
            }, captureData);

            return TRUE;
        }), data);

    gtk_widget_add_controller(dialog, keyCtrl);
    gtk_widget_set_focusable(dialog, TRUE);
    gtk_window_present(GTK_WINDOW(dialog));
}

// ---------------------------------------------------------------------------
// showKeybindingList — build the GTK keybinding editor UI
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
    addHeaderCol("", 80);  // Edit button column

    gtk_box_append(GTK_BOX(contentBox_), headerRow);

    // Separator
    GtkWidget* sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_bottom(sep, 4);
    gtk_box_append(GTK_BOX(contentBox_), sep);

    // Keybinding rows
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

        // Alternating background
        if (i % 2 == 0) {
            gtk_widget_add_css_class(row, "card-grid");
        }

        // Action name
        std::string actionStr = actionToDisplayString(kb.action);
        if (kb.action == Action::Custom && !kb.custom_action.empty()) {
            actionStr = kb.custom_action;
        }
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

// ---------------------------------------------------------------------------
// onKeybindingEdit — called on double-click
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::onKeybindingEdit(int idx) {
    showKeyCaptureDialog(idx);
}

// ---------------------------------------------------------------------------
// comboToTriggerString — convert KeyCombo to config trigger string
// ---------------------------------------------------------------------------

static std::string comboToTriggerString(const KeyCombo& combo) {
    std::string result;

    if (combo.mods & ModCtrl)  result += "ctrl+";
    if (combo.mods & ModAlt)   result += "alt+";
    if (combo.mods & ModShift) result += "shift+";
    if (combo.mods & ModSuper) result += "super+";

    static const std::unordered_map<uint32_t, const char*> keyNames = {
        {0xF700, "up"}, {0xF701, "down"}, {0xF702, "left"}, {0xF703, "right"},
        {0xF704, "home"}, {0xF705, "end"},
        {0xF706, "pageup"}, {0xF707, "pagedown"},
        {0xF708, "tab"}, {0xF709, "enter"}, {0xF70A, "escape"},
        {0xF70B, "backspace"}, {0xF70C, "space"}, {0xF70D, "delete"},
        {0xF710, "f1"}, {0xF711, "f2"}, {0xF712, "f3"}, {0xF713, "f4"},
        {0xF714, "f5"}, {0xF715, "f6"}, {0xF716, "f7"}, {0xF717, "f8"},
        {0xF718, "f9"}, {0xF719, "f10"}, {0xF71A, "f11"}, {0xF71B, "f12"},
    };

    auto it = keyNames.find(combo.keycode);
    if (it != keyNames.end()) {
        result += it->second;
    } else if (combo.keycode >= 'a' && combo.keycode <= 'z') {
        result += static_cast<char>(combo.keycode);
    } else if (combo.keycode >= '0' && combo.keycode <= '9') {
        result += static_cast<char>(combo.keycode);
    } else if (combo.keycode >= 0x20 && combo.keycode <= 0x7E) {
        result += static_cast<char>(combo.keycode);
    } else {
        result += "unknown";
    }

    return result;
}

// ---------------------------------------------------------------------------
// actionToConfigString — convert Action enum to config action string
// ---------------------------------------------------------------------------

static std::string actionToConfigString(Action action) {
    static const std::unordered_map<Action, const char*> names = {
        {Action::None, "none"},
        {Action::NewTab, "new_tab"}, {Action::CloseTab, "close_tab"},
        {Action::NextTab, "next_tab"}, {Action::PrevTab, "prev_tab"},
        {Action::SplitRight, "split_right"}, {Action::SplitDown, "split_down"},
        {Action::ClosePane, "close_pane"},
        {Action::FocusUp, "focus_up"}, {Action::FocusDown, "focus_down"},
        {Action::FocusLeft, "focus_left"}, {Action::FocusRight, "focus_right"},
        {Action::Copy, "copy"}, {Action::Paste, "paste"},
        {Action::PasteFromHistory, "paste_from_history"},
        {Action::SelectAll, "select_all"},
        {Action::ScrollUp, "scroll_up"}, {Action::ScrollDown, "scroll_down"},
        {Action::ScrollPageUp, "scroll_page_up"},
        {Action::ScrollPageDown, "scroll_page_down"},
        {Action::ScrollToTop, "scroll_to_top"},
        {Action::ScrollToBottom, "scroll_to_bottom"},
        {Action::SearchOpen, "search_open"}, {Action::SearchNext, "search_next"},
        {Action::SearchPrev, "search_prev"},
        {Action::SearchClose, "search_close"},
        {Action::NewWindow, "new_window"}, {Action::CloseWindow, "close_window"},
        {Action::ToggleFullscreen, "toggle_fullscreen"},
        {Action::FontIncrease, "font_increase"},
        {Action::FontDecrease, "font_decrease"},
        {Action::FontReset, "font_reset"},
        {Action::ResetTerminal, "reset_terminal"},
        {Action::ClearScrollback, "clear_scrollback"},
        {Action::ShowNotifications, "show_notifications"},
        {Action::ReloadConfig, "reload_config"},
        {Action::JumpPromptUp, "jump_prompt_up"},
        {Action::JumpPromptDown, "jump_prompt_down"},
        {Action::EnterCopyMode, "enter_copy_mode"},
        {Action::ToggleSidebar, "toggle_sidebar"},
        {Action::SwitchWorkspace1, "switch_workspace_1"},
        {Action::SwitchWorkspace2, "switch_workspace_2"},
        {Action::SwitchWorkspace3, "switch_workspace_3"},
        {Action::SwitchWorkspace4, "switch_workspace_4"},
        {Action::SwitchWorkspace5, "switch_workspace_5"},
        {Action::SwitchWorkspace6, "switch_workspace_6"},
        {Action::SwitchWorkspace7, "switch_workspace_7"},
        {Action::SwitchWorkspace8, "switch_workspace_8"},
        {Action::SwitchTab1, "switch_tab_1"}, {Action::SwitchTab2, "switch_tab_2"},
        {Action::SwitchTab3, "switch_tab_3"}, {Action::SwitchTab4, "switch_tab_4"},
        {Action::SwitchTab5, "switch_tab_5"}, {Action::SwitchTab6, "switch_tab_6"},
        {Action::SwitchTab7, "switch_tab_7"}, {Action::SwitchTab8, "switch_tab_8"},
        {Action::SwitchTab9, "switch_tab_9"},
        {Action::OpenSettings, "open_settings"},
        {Action::OpenThemeHub, "open_theme_hub"},
        {Action::OpenFontHub, "open_font_hub"},
        {Action::OpenCommandPalette, "open_command_palette"},
        {Action::SshConnect, "ssh_connect"},
        {Action::ShowProfileDropdown, "show_profile_dropdown"},
        {Action::NewTabProfile1, "new_tab_profile1"},
        {Action::NewTabProfile2, "new_tab_profile2"},
        {Action::NewTabProfile3, "new_tab_profile3"},
        {Action::NewTabProfile4, "new_tab_profile4"},
        {Action::NewTabProfile5, "new_tab_profile5"},
        {Action::NewTabProfile6, "new_tab_profile6"},
        {Action::NewTabProfile7, "new_tab_profile7"},
        {Action::NewTabProfile8, "new_tab_profile8"},
        {Action::NewTabProfile9, "new_tab_profile9"},
    };

    auto it = names.find(action);
    return it != names.end() ? it->second : "custom";
}

// ---------------------------------------------------------------------------
// syncKeybindingsToConfig
// ---------------------------------------------------------------------------

void UnifiedSettingsWindow::syncKeybindingsToConfig() {
    config_.keybindings.clear();
    for (const auto& kb : keybindMgr_.allBindings()) {
        KeyBinding cfgKb;
        cfgKb.trigger = comboToTriggerString(kb.combo);
        if (kb.action == Action::Custom && !kb.custom_action.empty()) {
            cfgKb.action = kb.custom_action;
        } else {
            cfgKb.action = actionToConfigString(kb.action);
        }
        config_.keybindings.push_back(std::move(cfgKb));
    }
}

} // namespace termcore

#endif // __linux__
