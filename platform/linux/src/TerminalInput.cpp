#include "TerminalWidgetPrivate.h"

#include <algorithm>
#include <cstring>
#include <cmath>

using namespace termcore;

// ---- GDK keyval to core keycode mapping ----

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

    if (keyval >= GDK_KEY_A && keyval <= GDK_KEY_Z) {
        return keyval - GDK_KEY_A + 'a';
    }
    if (keyval >= GDK_KEY_a && keyval <= GDK_KEY_z) {
        return keyval;
    }
    if (keyval >= GDK_KEY_0 && keyval <= GDK_KEY_9) {
        return keyval;
    }
    if (keyval >= 0x20 && keyval <= 0x7E) {
        return keyval;
    }
    return 0;
}

static uint8_t gdkModsToCore(GdkModifierType state) {
    uint8_t mods = ModNone;
    if (state & GDK_SHIFT_MASK)   mods |= ModShift;
    if (state & GDK_CONTROL_MASK) mods |= ModCtrl;
    if (state & GDK_ALT_MASK)     mods |= ModAlt;
    if (state & GDK_SUPER_MASK)   mods |= ModSuper;
    return mods;
}

// ---- Font size helpers ----

static void change_font_size(TerminalWidget* self, float delta) {
    if (!self->fontCollection) return;

    float current = self->config.font_size;
    float newSize = std::max(6.0f, std::min(72.0f, current + delta));
    if (newSize == current) return;

    self->config.font_size = newSize;
    self->fontCollection->setPrimaryFont(self->config.font_family, newSize);
    terminal_widget_recalculate_grid(self);
}

static void reset_font_size(TerminalWidget* self) {
    if (!self->fontCollection) return;
    float defaultSize = 14.0f;
    self->config.font_size = defaultSize;
    self->fontCollection->setPrimaryFont(self->config.font_family, defaultSize);
    terminal_widget_recalculate_grid(self);
}

// ---- Clipboard helpers ----

static void paste_callback(GObject* source, GAsyncResult* result,
                            gpointer user_data) {
    TerminalWidget* self = TERMINAL_WIDGET(user_data);
    GdkClipboard* clipboard = GDK_CLIPBOARD(source);

    GError* error = nullptr;
    char* text = gdk_clipboard_read_text_finish(clipboard, result, &error);
    if (text) {
        terminal_widget_send_pty_data(self, text, strlen(text));
        g_free(text);
    }
    if (error) {
        g_error_free(error);
    }
}

static void paste_from_clipboard(TerminalWidget* self) {
    GdkDisplay* display = gtk_widget_get_display(GTK_WIDGET(self));
    GdkClipboard* clipboard = gdk_display_get_clipboard(display);
    gdk_clipboard_read_text_async(clipboard, nullptr, paste_callback, self);
}

static void copy_to_clipboard(TerminalWidget* self, const char* text) {
    if (!text || !*text) return;
    GdkDisplay* display = gtk_widget_get_display(GTK_WIDGET(self));
    GdkClipboard* clipboard = gdk_display_get_clipboard(display);
    gdk_clipboard_set_text(clipboard, text);
}

// ---- Keybinding action handler ----

static GtkWindow* get_parent_window(TerminalWidget* self) {
    GtkWidget* toplevel = GTK_WIDGET(self);
    while (toplevel) {
        if (GTK_IS_WINDOW(toplevel)) return GTK_WINDOW(toplevel);
        toplevel = gtk_widget_get_parent(toplevel);
    }
    return nullptr;
}

static bool handle_action(TerminalWidget* self, Action action) {
    switch (action) {
        case Action::Copy:
            // Selection not yet implemented; placeholder
            g_debug("BreadTerminal: Copy action (selection not yet implemented)");
            return true;

        case Action::Paste:
            paste_from_clipboard(self);
            return true;

        case Action::FontIncrease:
            change_font_size(self, 1.0f);
            return true;

        case Action::FontDecrease:
            change_font_size(self, -1.0f);
            return true;

        case Action::FontReset:
            reset_font_size(self);
            return true;

        case Action::SearchOpen:
            self->search_open = true;
            g_debug("BreadTerminal: Search open (UI not yet implemented)");
            return true;

        case Action::SearchClose:
            self->search_open = false;
            return true;

        case Action::ToggleFullscreen: {
            GtkWindow* win = get_parent_window(self);
            if (win) {
                if (gtk_window_is_fullscreen(win))
                    gtk_window_unfullscreen(win);
                else
                    gtk_window_fullscreen(win);
            }
            return true;
        }

        case Action::ScrollPageUp:
            if (self->screen) {
                self->screen->scrollViewportUp(self->term_rows);
                self->needs_render = true;
            }
            return true;

        case Action::ScrollPageDown:
            if (self->screen) {
                self->screen->scrollViewportDown(self->term_rows);
                self->needs_render = true;
            }
            return true;

        case Action::ScrollToTop:
            if (self->screen) {
                self->screen->scrollViewportToTop();
                self->needs_render = true;
            }
            return true;

        case Action::ScrollToBottom:
            if (self->screen) {
                self->screen->scrollViewportToBottom();
                self->needs_render = true;
            }
            return true;

        case Action::NewTab:
            // TODO: Tab management not yet implemented on Linux
            g_debug("BreadTerminal: NewTab (not yet implemented)");
            return true;

        case Action::CloseTab:
            // TODO: Tab management not yet implemented on Linux
            g_debug("BreadTerminal: CloseTab (not yet implemented)");
            return true;

        case Action::NewWindow:
            g_debug("BreadTerminal: NewWindow (not yet implemented)");
            return true;

        case Action::CloseWindow: {
            GtkWindow* win = get_parent_window(self);
            if (win) gtk_window_close(win);
            return true;
        }

        default:
            break;
    }
    return false;
}

// ---- Key press handler (called from TerminalWidget.cpp signal) ----

gboolean terminal_widget_on_key_pressed(GtkEventControllerKey* /*controller*/,
                                         guint keyval, guint /*keycode*/,
                                         GdkModifierType state,
                                         gpointer user_data) {
    TerminalWidget* self = TERMINAL_WIDGET(user_data);
    if (!self->pty) return FALSE;

    // --- Keybinding lookup (before sending raw keys to PTY) ---
    if (self->keybindings) {
        uint32_t coreKey = gdkKeyToCore(keyval);
        uint8_t coreMods = gdkModsToCore(state);

        if (coreKey != 0) {
            KeyCombo combo{coreKey, coreMods};
            Action action = self->keybindings->lookup(combo);

            // On Linux, also try with Ctrl remapped to Super (Cmd).
            // Default keybindings use "cmd" (ModSuper) on macOS.
            if (action == Action::None && (coreMods & ModCtrl)) {
                uint8_t superMods = (coreMods & ~ModCtrl) | ModSuper;
                KeyCombo superCombo{coreKey, superMods};
                action = self->keybindings->lookup(superCombo);
            }

            if (action != Action::None) {
                if (handle_action(self, action))
                    return TRUE;
            }
        }
    }

    // --- Terminal-convention Ctrl+Shift+C/V for copy/paste ---
    if ((state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) ==
        (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) {
        if (keyval == GDK_KEY_C || keyval == GDK_KEY_c) {
            g_debug("BreadTerminal: Ctrl+Shift+C copy (selection not yet implemented)");
            return TRUE;
        }
        if (keyval == GDK_KEY_V || keyval == GDK_KEY_v) {
            paste_from_clipboard(self);
            return TRUE;
        }
    }

    // --- Raw key passthrough to PTY ---
    bool app_cursor = self->screen && self->screen->appCursorKeys();
    const char* pfx = app_cursor ? "\x1bO" : "\x1b[";

    switch (keyval) {
        case GDK_KEY_Up:    { char s[3]={pfx[0],pfx[1],'A'}; terminal_widget_send_pty_data(self,s,3); return TRUE; }
        case GDK_KEY_Down:  { char s[3]={pfx[0],pfx[1],'B'}; terminal_widget_send_pty_data(self,s,3); return TRUE; }
        case GDK_KEY_Right: { char s[3]={pfx[0],pfx[1],'C'}; terminal_widget_send_pty_data(self,s,3); return TRUE; }
        case GDK_KEY_Left:  { char s[3]={pfx[0],pfx[1],'D'}; terminal_widget_send_pty_data(self,s,3); return TRUE; }
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            terminal_widget_send_pty_data(self, "\r", 1); return TRUE;
        case GDK_KEY_BackSpace:
            terminal_widget_send_pty_data(self, "\x7f", 1); return TRUE;
        case GDK_KEY_Tab:
            terminal_widget_send_pty_data(self, "\t", 1); return TRUE;
        case GDK_KEY_Escape:
            terminal_widget_send_pty_data(self, "\x1b", 1); return TRUE;
        case GDK_KEY_Home:
            terminal_widget_send_pty_data(self, "\x1b[H", 3); return TRUE;
        case GDK_KEY_End:
            terminal_widget_send_pty_data(self, "\x1b[F", 3); return TRUE;
        case GDK_KEY_Page_Up:
            terminal_widget_send_pty_data(self, "\x1b[5~", 4); return TRUE;
        case GDK_KEY_Page_Down:
            terminal_widget_send_pty_data(self, "\x1b[6~", 4); return TRUE;
        case GDK_KEY_Delete:
            terminal_widget_send_pty_data(self, "\x1b[3~", 4); return TRUE;
        case GDK_KEY_F1:
            terminal_widget_send_pty_data(self, "\x1bOP", 3); return TRUE;
        case GDK_KEY_F2:
            terminal_widget_send_pty_data(self, "\x1bOQ", 3); return TRUE;
        case GDK_KEY_F3:
            terminal_widget_send_pty_data(self, "\x1bOR", 3); return TRUE;
        case GDK_KEY_F4:
            terminal_widget_send_pty_data(self, "\x1bOS", 3); return TRUE;
        default:
            break;
    }

    // Handle Ctrl+key
    if (state & GDK_CONTROL_MASK) {
        if (keyval >= 'a' && keyval <= 'z') {
            char c = static_cast<char>(keyval - 'a' + 1);
            terminal_widget_send_pty_data(self, &c, 1);
            return TRUE;
        }
    }

    // Regular character input
    gunichar ch = gdk_keyval_to_unicode(keyval);
    if (ch != 0 && g_unichar_isprint(ch)) {
        char utf8[6];
        int len = g_unichar_to_utf8(ch, utf8);
        if (len > 0) {
            terminal_widget_send_pty_data(self, utf8, static_cast<size_t>(len));
            return TRUE;
        }
    }

    return FALSE;
}

// ---- Mouse scroll handler ----

gboolean terminal_widget_on_scroll(GtkEventControllerScroll* /*controller*/,
                                    double /*dx*/, double dy,
                                    gpointer user_data) {
    TerminalWidget* self = TERMINAL_WIDGET(user_data);
    if (!self->screen) return FALSE;

    int lines = 3;  // standard scroll amount

    if (dy < 0) {
        self->screen->scrollViewportUp(lines);
        self->needs_render = true;
    } else if (dy > 0) {
        self->screen->scrollViewportDown(lines);
        self->needs_render = true;
    }

    return TRUE;
}
