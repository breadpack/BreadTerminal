#include "GtkPlatformHost.h"
#include "termcore/pty.h"

#include <cstring>

GtkPlatformHost::GtkPlatformHost(GtkWidget* glArea)
    : glArea_(glArea) {}

void GtkPlatformHost::setWindow(GtkWindow* window) {
    window_ = window;
}

GtkWindow* GtkPlatformHost::resolveWindow() {
    if (window_) return window_;
    GtkWidget* w = glArea_;
    while (w) {
        if (GTK_IS_WINDOW(w)) {
            window_ = GTK_WINDOW(w);
            return window_;
        }
        w = gtk_widget_get_parent(w);
    }
    return nullptr;
}

// --- Rendering ---

void GtkPlatformHost::invalidate() {
    if (glArea_) {
        gtk_gl_area_queue_render(GTK_GL_AREA(glArea_));
    }
}

void GtkPlatformHost::getViewportSize(int& w, int& h) {
    if (glArea_) {
        w = gtk_widget_get_width(glArea_);
        h = gtk_widget_get_height(glArea_);
    } else {
        w = 0;
        h = 0;
    }
}

// --- Clipboard ---

std::string GtkPlatformHost::getClipboardText() {
    // GTK4 clipboard is async; we do a synchronous-ish read using the
    // GLib main context iteration approach.  This is called from the
    // controller on the main thread, so we spin until the async completes.
    if (!glArea_) return "";

    GdkDisplay* display = gtk_widget_get_display(glArea_);
    GdkClipboard* clipboard = gdk_display_get_clipboard(display);

    struct ReadCtx {
        std::string text;
        bool done = false;
    } ctx;

    gdk_clipboard_read_text_async(
        clipboard, nullptr,
        [](GObject* source, GAsyncResult* result, gpointer user_data) {
            auto* c = static_cast<ReadCtx*>(user_data);
            GError* error = nullptr;
            char* text = gdk_clipboard_read_text_finish(
                GDK_CLIPBOARD(source), result, &error);
            if (text) {
                c->text = text;
                g_free(text);
            }
            if (error) g_error_free(error);
            c->done = true;
        },
        &ctx);

    // Spin the main loop until the async operation completes
    GMainContext* mainCtx = g_main_context_default();
    while (!ctx.done) {
        g_main_context_iteration(mainCtx, TRUE);
    }

    return ctx.text;
}

void GtkPlatformHost::setClipboardText(const std::string& text) {
    if (text.empty() || !glArea_) return;
    GdkDisplay* display = gtk_widget_get_display(glArea_);
    GdkClipboard* clipboard = gdk_display_get_clipboard(display);
    gdk_clipboard_set_text(clipboard, text.c_str());
}

// --- Window ---

void GtkPlatformHost::setWindowTitle(const std::string& title) {
    GtkWindow* win = resolveWindow();
    if (win) {
        gtk_window_set_title(win, title.c_str());
    }
}

void GtkPlatformHost::toggleFullscreen() {
    GtkWindow* win = resolveWindow();
    if (!win) return;

    if (isFullscreen_) {
        gtk_window_unfullscreen(win);
    } else {
        gtk_window_fullscreen(win);
    }
    isFullscreen_ = !isFullscreen_;
}

void GtkPlatformHost::closeWindow() {
    GtkWindow* win = resolveWindow();
    if (win) {
        gtk_window_close(win);
    }
}

void GtkPlatformHost::showConfirmDialog(const std::string& msg,
                                         std::function<void(bool)> cb) {
    GtkWindow* win = resolveWindow();
    if (!win) {
        if (cb) cb(false);
        return;
    }

    // Use a GtkAlertDialog (GTK 4.10+) or fall back to simple approach
    GtkAlertDialog* dialog = gtk_alert_dialog_new("%s", msg.c_str());
    gtk_alert_dialog_set_buttons(dialog,
        (const char* const[]){"Cancel", "OK", nullptr});
    gtk_alert_dialog_set_default_button(dialog, 1);
    gtk_alert_dialog_set_cancel_button(dialog, 0);

    // Capture callback
    auto* captured = new std::function<void(bool)>(std::move(cb));
    gtk_alert_dialog_choose(
        dialog, win, nullptr,
        [](GObject* source, GAsyncResult* result, gpointer user_data) {
            auto* fn = static_cast<std::function<void(bool)>*>(user_data);
            GError* error = nullptr;
            int chosen = gtk_alert_dialog_choose_finish(
                GTK_ALERT_DIALOG(source), result, &error);
            if (*fn) (*fn)(chosen == 1);
            if (error) g_error_free(error);
            delete fn;
        },
        captured);

    g_object_unref(dialog);
}

// --- Search UI ---

void GtkPlatformHost::showSearchBar() {
    // TODO: Implement GTK search bar widget
    g_debug("BreadTerminal: showSearchBar (not yet implemented)");
}

void GtkPlatformHost::hideSearchBar() {
    // TODO: Implement GTK search bar widget
    g_debug("BreadTerminal: hideSearchBar (not yet implemented)");
}

void GtkPlatformHost::updateSearchResults(int current, int total) {
    (void)current;
    (void)total;
}

// --- IME ---

void GtkPlatformHost::positionIME(int x, int y, int height) {
    // GTK4 handles IME positioning via the input method context;
    // no manual positioning needed in most cases.
    (void)x;
    (void)y;
    (void)height;
}

// --- Font/color notifications ---

void GtkPlatformHost::onFontChanged(float cellW, float cellH) {
    (void)cellW;
    (void)cellH;
    // The render tick will pick up changes; just request a redraw
    if (glArea_) {
        gtk_gl_area_queue_render(GTK_GL_AREA(glArea_));
    }
}

void GtkPlatformHost::onColorsChanged() {
    if (glArea_) {
        gtk_gl_area_queue_render(GTK_GL_AREA(glArea_));
    }
}

void GtkPlatformHost::onGridSizeChanged(int rows, int cols) {
    (void)rows;
    (void)cols;
    if (glArea_) {
        gtk_gl_area_queue_render(GTK_GL_AREA(glArea_));
    }
}

// --- Notifications ---

void GtkPlatformHost::showNotification(const std::string& title,
                                        const std::string& body) {
    // TODO: Use GNotification for desktop notifications
    g_debug("BreadTerminal: notification: %s - %s", title.c_str(), body.c_str());
}

// --- Settings/Hub windows ---

void GtkPlatformHost::openSettingsWindow(const termcore::Config& config) {
#if defined(__linux__)
    if (!settingsWindow_) {
        settingsWindow_ = std::make_unique<termcore::UnifiedSettingsWindow>();
    }

    settingsWindow_->setConfig(config);
    settingsWindow_->setSaveCallback([this](const termcore::Config& /*newConfig*/) {
        // TODO: Wire up ConfigApplier to apply changes to the terminal
        g_debug("BreadTerminal: settings saved");
    });

    GtkWindow* parent = resolveWindow();
    settingsWindow_->show(parent);
#else
    (void)config;
#endif
}

// --- DPI ---

float GtkPlatformHost::dpiScale() {
    if (glArea_) {
        return static_cast<float>(
            gtk_widget_get_scale_factor(glArea_));
    }
    return 1.0f;
}

// --- PTY factory ---

std::unique_ptr<termcore::Pty> GtkPlatformHost::createPty(
        const std::string& shell, int rows, int cols) {
    auto pty = termcore::createPty();
    if (!pty->spawn(shell, {}, "", rows, cols)) {
        g_warning("BreadTerminal: failed to spawn shell for pane");
    }
    return pty;
}
