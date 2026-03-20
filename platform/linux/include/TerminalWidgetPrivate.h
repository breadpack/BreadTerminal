#ifndef TERMCORE_TERMINAL_WIDGET_PRIVATE_H
#define TERMCORE_TERMINAL_WIDGET_PRIVATE_H

#include "TerminalWidget.h"
#include "GtkPlatformHost.h"
#include "termcore/terminal_controller.h"
#include "termcore/config.h"
#include "termcore/font/font_collection.h"
#include "termcore/font/font_shaper.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/glyph_cache.h"

#include "GLTextRenderer.h"
#include <memory>

using namespace termcore;

/// Private implementation struct for TerminalWidget.
/// Shared between TerminalWidget.cpp and TerminalInput.cpp.
struct _TerminalWidget {
    GtkGLArea parent_instance;

    // Platform host (bridges controller to GTK)
    std::unique_ptr<GtkPlatformHost> platformHost;

    // Core controller -- owns all terminal state (Screen, VtParser, Pty, tabs, etc.)
    std::unique_ptr<termcore::TerminalController> controller;

    // Font stack (platform-owned, shared with controller)
    std::unique_ptr<IFontRasterizer> rasterizer;
    std::unique_ptr<IFontDiscovery> discovery;
    std::unique_ptr<FontShaper> shaper;
    std::unique_ptr<FontCollection> fontCollection;
    std::unique_ptr<GlyphAtlas> atlas;
    std::unique_ptr<GlyphCache> cache;

    // Renderer
    std::unique_ptr<GLTextRenderer> renderer;

    // Config
    Config config;
    bool config_loaded;

    // Render / poll timer
    guint render_timer_id;
};

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
