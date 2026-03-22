#include "GtkPlatformHost.h"
#include "termcore/terminal_controller.h"
#include "termcore/pty.h"

#include <cstring>

GtkPlatformHost::GtkPlatformHost(GtkWidget* glArea)
    : glArea_(glArea) {}

void GtkPlatformHost::setWindow(GtkWindow* window) {
    window_ = window;
}

void GtkPlatformHost::setController(termcore::TerminalController* controller) {
    controller_ = controller;
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

void GtkPlatformHost::buildSearchBar() {
    GtkWindow* win = resolveWindow();
    if (!win) return;

    // Create a horizontal box for the search bar
    searchOverlay_ = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_halign(searchOverlay_, GTK_ALIGN_END);
    gtk_widget_set_valign(searchOverlay_, GTK_ALIGN_START);
    gtk_widget_set_margin_top(searchOverlay_, 8);
    gtk_widget_set_margin_end(searchOverlay_, 8);

    // Apply CSS styling for the search bar background
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css,
        ".search-bar { background: alpha(@theme_bg_color, 0.95); "
        "border: 1px solid @borders; border-radius: 6px; padding: 4px 8px; }"
        ".search-bar entry { min-width: 240px; }"
        ".search-bar .search-count { opacity: 0.7; font-size: 0.85em; }");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    gtk_widget_add_css_class(searchOverlay_, "search-bar");

    // Search entry
    searchEntry_ = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(searchEntry_), "Search...");
    gtk_widget_set_hexpand(searchEntry_, TRUE);
    gtk_box_append(GTK_BOX(searchOverlay_), searchEntry_);

    // Match count label
    searchLabel_ = gtk_label_new("");
    gtk_widget_add_css_class(searchLabel_, "search-count");
    gtk_box_append(GTK_BOX(searchOverlay_), searchLabel_);

    // Connect text-changed signal to fire incremental search
    g_signal_connect(searchEntry_, "changed",
        G_CALLBACK(+[](GtkEditable* editable, gpointer user_data) {
            auto* self = static_cast<GtkPlatformHost*>(user_data);
            if (self->updatingSearchText_ || !self->controller_) return;
            const char* text = gtk_editable_get_text(editable);
            self->controller_->onSearchQuery(text ? text : "");
        }),
        this);

    // Key event controller for Enter/Shift+Enter/Escape/Up/Down
    searchKeyCtrl_ = gtk_event_controller_key_new();
    g_signal_connect(searchKeyCtrl_, "key-pressed",
        G_CALLBACK(+[](GtkEventControllerKey* /*ctrl*/,
                        guint keyval, guint /*keycode*/,
                        GdkModifierType state,
                        gpointer user_data) -> gboolean {
            auto* self = static_cast<GtkPlatformHost*>(user_data);
            if (!self->controller_) return FALSE;

            if (keyval == GDK_KEY_Escape) {
                self->controller_->onSearchQuery("");
                self->hideSearchBar();
                return TRUE;
            }
            if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
                if (state & GDK_SHIFT_MASK)
                    self->controller_->onSearchPrev();
                else
                    self->controller_->onSearchNext();
                return TRUE;
            }
            if (keyval == GDK_KEY_F3) {
                if (state & GDK_SHIFT_MASK)
                    self->controller_->onSearchPrev();
                else
                    self->controller_->onSearchNext();
                return TRUE;
            }
            if (keyval == GDK_KEY_Up) {
                self->controller_->onSearchHistoryPrev();
                return TRUE;
            }
            if (keyval == GDK_KEY_Down) {
                self->controller_->onSearchHistoryNext();
                return TRUE;
            }
            return FALSE;
        }),
        this);
    gtk_widget_add_controller(searchEntry_, searchKeyCtrl_);

    // Find the GtkOverlay parent of glArea_ and add the search bar to it
    GtkWidget* parent = gtk_widget_get_parent(glArea_);
    if (parent && GTK_IS_OVERLAY(parent)) {
        gtk_overlay_add_overlay(GTK_OVERLAY(parent), searchOverlay_);
    } else {
        // Fallback: add directly to the window's title bar area
        // This shouldn't happen if main.cpp sets up the overlay correctly
        g_warning("BreadTerminal: search bar needs GtkOverlay parent");
        g_object_ref_sink(searchOverlay_);
    }
}

void GtkPlatformHost::destroySearchBar() {
    if (searchOverlay_) {
        GtkWidget* parent = gtk_widget_get_parent(searchOverlay_);
        if (parent && GTK_IS_OVERLAY(parent)) {
            gtk_overlay_remove_overlay(GTK_OVERLAY(parent), searchOverlay_);
        }
        searchOverlay_ = nullptr;
        searchEntry_ = nullptr;
        searchLabel_ = nullptr;
        searchKeyCtrl_ = nullptr;
    }
    searchBarVisible_ = false;
}

void GtkPlatformHost::showSearchBar() {
    if (searchBarVisible_ && searchEntry_) {
        // Already visible; just focus and select all text
        gtk_widget_grab_focus(searchEntry_);
        gtk_editable_select_region(GTK_EDITABLE(searchEntry_), 0, -1);
        return;
    }

    buildSearchBar();
    searchBarVisible_ = true;

    if (searchEntry_) {
        gtk_widget_grab_focus(searchEntry_);
    }
}

void GtkPlatformHost::hideSearchBar() {
    destroySearchBar();

    // Return focus to the terminal GL area
    if (glArea_) {
        gtk_widget_grab_focus(glArea_);
    }

    // Request a redraw to clear search highlights
    invalidate();
}

void GtkPlatformHost::updateSearchResults(int current, int total) {
    if (!searchLabel_) return;

    if (total > 0) {
        // Display "current of total" (1-based for the user)
        std::string text = std::to_string(current + 1) + " of "
                         + std::to_string(total);
        gtk_label_set_text(GTK_LABEL(searchLabel_), text.c_str());
    } else {
        gtk_label_set_text(GTK_LABEL(searchLabel_), "No results");
    }

    invalidate();
}

void GtkPlatformHost::setSearchBarText(const std::string& text) {
    if (!searchEntry_) return;

    // Prevent re-entrant signal from firing onSearchQuery
    updatingSearchText_ = true;
    gtk_editable_set_text(GTK_EDITABLE(searchEntry_), text.c_str());
    updatingSearchText_ = false;
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

// --- Clipboard history ---

void GtkPlatformHost::showClipboardHistory(
        const std::vector<termcore::ClipboardEntry>& entries) {
    // TODO: Implement clipboard history popup
    (void)entries;
    g_debug("BreadTerminal: showClipboardHistory (not yet implemented)");
}

// --- Settings/Hub windows ---

void GtkPlatformHost::openSettingsWindow(const termcore::Config& config) {
#if defined(__linux__)
    if (!settingsWindow_) {
        settingsWindow_ = std::make_unique<termcore::UnifiedSettingsWindow>();
    }

    settingsWindow_->setConfig(config);
    settingsWindow_->setSaveCallback([this](const termcore::Config& newConfig) {
        if (controller_) {
            controller_->onConfigChanged(newConfig);
        }
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

// --- URL opening ---

void GtkPlatformHost::openUrl(const std::string& url) {
    std::string cmd = "xdg-open \"" + url + "\" &";
    (void)system(cmd.c_str());
}

void GtkPlatformHost::setMouseCursor(CursorType cursor) {
    if (!glArea_) return;
    GdkCursor* gdkCursor = gdk_cursor_new_from_name(
        cursor == CursorType::Hand ? "pointer" : "default", nullptr);
    gtk_widget_set_cursor(glArea_, gdkCursor);
    if (gdkCursor) g_object_unref(gdkCursor);
}

// --- PTY factory ---

std::unique_ptr<termcore::Pty> GtkPlatformHost::createPty(
        const termcore::Profile& profile, int rows, int cols) {
    auto pty = termcore::createPty();
    if (!pty->spawn(profile.command, profile.args, profile.working_dir, rows, cols)) {
        g_warning("BreadTerminal: failed to spawn shell for pane");
    }
    return pty;
}
