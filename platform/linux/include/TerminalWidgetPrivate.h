#ifndef TERMCORE_TERMINAL_WIDGET_PRIVATE_H
#define TERMCORE_TERMINAL_WIDGET_PRIVATE_H

#include "TerminalWidget.h"
#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include "termcore/pty.h"
#include "termcore/config.h"
#include "termcore/keybinding.h"
#include "termcore/font/font_collection.h"
#include "termcore/font/font_shaper.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/glyph_cache.h"

#include <memory>

class GLTextRenderer;

using namespace termcore;

/// Private implementation struct for TerminalWidget.
/// Shared between TerminalWidget.cpp and TerminalInput.cpp.
struct _TerminalWidget {
    GtkGLArea parent_instance;

    // Core terminal state
    std::unique_ptr<Screen> screen;
    std::unique_ptr<VtParser> parser;
    std::unique_ptr<Pty> pty;

    // Font stack
    std::unique_ptr<IFontRasterizer> rasterizer;
    std::unique_ptr<IFontDiscovery> discovery;
    std::unique_ptr<FontShaper> shaper;
    std::unique_ptr<FontCollection> fontCollection;
    std::unique_ptr<GlyphAtlas> atlas;
    std::unique_ptr<GlyphCache> cache;

    // Renderer
    std::unique_ptr<GLTextRenderer> renderer;

    // Keybinding manager
    std::unique_ptr<KeybindingManager> keybindings;

    // Config
    Config config;
    bool config_loaded;

    // PTY I/O channel
    GIOChannel* pty_channel;
    guint pty_watch_id;

    // Render timer
    guint render_timer_id;

    // State
    float cell_width;
    float cell_height;
    int term_rows;
    int term_cols;
    bool needs_render;
    bool search_open;  // placeholder for search UI
};

/// Send data to the PTY (shared helper)
void terminal_widget_send_pty_data(TerminalWidget* self, const char* data, size_t len);

/// Recalculate grid after font size change
void terminal_widget_recalculate_grid(TerminalWidget* self);

/// Key press handler (defined in TerminalInput.cpp)
gboolean terminal_widget_on_key_pressed(GtkEventControllerKey* controller,
                                         guint keyval, guint keycode,
                                         GdkModifierType state,
                                         gpointer user_data);

/// Mouse scroll handler (defined in TerminalInput.cpp)
gboolean terminal_widget_on_scroll(GtkEventControllerScroll* controller,
                                    double dx, double dy,
                                    gpointer user_data);

#endif
