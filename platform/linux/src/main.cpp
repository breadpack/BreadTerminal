#include "TerminalWidget.h"
#include <gtk/gtk.h>

static void on_activate(GtkApplication* app, gpointer /*user_data*/) {
    GtkWidget* window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "BreadTerminal");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);

    GtkWidget* terminal = terminal_widget_new();
    gtk_window_set_child(GTK_WINDOW(window), terminal);

    gtk_window_present(GTK_WINDOW(window));

    // Start shell after the widget is realized
    g_signal_connect_swapped(terminal, "realize",
                              G_CALLBACK(terminal_widget_start_shell),
                              terminal);
}

int main(int argc, char* argv[]) {
    GtkApplication* app = gtk_application_new(
        "com.breadterminal.app", G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);
    return status;
}
