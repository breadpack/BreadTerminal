#ifndef TERMCORE_TERMINAL_WIDGET_H
#define TERMCORE_TERMINAL_WIDGET_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define TERMINAL_TYPE_WIDGET (terminal_widget_get_type())
G_DECLARE_FINAL_TYPE(TerminalWidget, terminal_widget, TERMINAL, WIDGET, GtkGLArea)

/// Create a new terminal widget.
GtkWidget* terminal_widget_new(void);

/// Start the shell process inside the terminal.
void terminal_widget_start_shell(TerminalWidget* self);

G_END_DECLS

#endif
