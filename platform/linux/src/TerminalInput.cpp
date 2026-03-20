#include "TerminalWidgetPrivate.h"

#include <cstring>

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

// ---- Key press handler ----

gboolean terminal_widget_on_key_pressed(GtkEventControllerKey* /*ctrlKey*/,
                                         guint keyval, guint /*keycode*/,
                                         GdkModifierType state,
                                         gpointer user_data) {
    TerminalWidget* self = TERMINAL_WIDGET(user_data);
    if (!self->controller) return FALSE;

    // Build a KeyEvent for the controller
    KeyEvent ke;
    ke.keycode = gdkKeyToCore(keyval);
    ke.modifiers = gdkModsToCore(state);

    // On Linux, remap Ctrl to Super for keybinding lookup parity with macOS
    // defaults (which use Cmd/Super). The controller handles this internally
    // if needed, but we also set the raw modifier so it has the full picture.

    // Fill in UTF-8 text for printable characters
    gunichar ch = gdk_keyval_to_unicode(keyval);
    if (ch != 0 && g_unichar_isprint(ch)) {
        char utf8[6];
        int len = g_unichar_to_utf8(ch, utf8);
        if (len > 0) {
            ke.text.assign(utf8, static_cast<size_t>(len));
        }
    }

    // Delegate entirely to the controller
    self->controller->onKeyEvent(ke);
    return TRUE;
}

// ---- Mouse scroll handler ----

gboolean terminal_widget_on_scroll(GtkEventControllerScroll* /*ctrlScroll*/,
                                    double /*dx*/, double dy,
                                    gpointer user_data) {
    TerminalWidget* self = TERMINAL_WIDGET(user_data);
    if (!self->controller) return FALSE;

    InputMouseEvent me;
    me.scrollLines = 3;  // standard scroll amount

    if (dy < 0) {
        me.type = InputMouseEvent::ScrollUp;
    } else if (dy > 0) {
        me.type = InputMouseEvent::ScrollDown;
    } else {
        return FALSE;
    }

    self->controller->onMouseEvent(me);
    return TRUE;
}
