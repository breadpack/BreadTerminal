#include "TerminalWidget.h"
#include "FreeTypeRasterizer.h"
#include "FontconfigDiscovery.h"
#include "GLTextRenderer.h"

#include "termcore/screen.h"
#include "termcore/vt_parser.h"
#include "termcore/pty.h"
#include "termcore/mouse.h"
#include "termcore/font/font_collection.h"

#include <algorithm>
#include "termcore/font/font_shaper.h"
#include "termcore/font/glyph_atlas.h"
#include "termcore/font/glyph_cache.h"

#include <memory>
#include <cstring>
#include <cmath>

using namespace termcore;

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
};

G_DEFINE_TYPE(TerminalWidget, terminal_widget, GTK_TYPE_GL_AREA)

// Forward declarations
static void terminal_widget_realize(GtkWidget* widget);
static void terminal_widget_unrealize(GtkWidget* widget);
static gboolean terminal_widget_render(GtkGLArea* area, GdkGLContext* context);
static void terminal_widget_resize(GtkGLArea* area, int width, int height);
static gboolean on_key_pressed(GtkEventControllerKey* controller,
                                guint keyval, guint keycode,
                                GdkModifierType state, gpointer user_data);
static gboolean on_pty_read(GIOChannel* source, GIOCondition condition,
                             gpointer user_data);
static gboolean on_render_tick(gpointer user_data);

static void terminal_widget_dispose(GObject* object) {
    TerminalWidget* self = TERMINAL_WIDGET(object);

    if (self->render_timer_id) {
        g_source_remove(self->render_timer_id);
        self->render_timer_id = 0;
    }
    if (self->pty_watch_id) {
        g_source_remove(self->pty_watch_id);
        self->pty_watch_id = 0;
    }
    if (self->pty_channel) {
        g_io_channel_unref(self->pty_channel);
        self->pty_channel = nullptr;
    }

    // Destroy C++ objects
    self->renderer.reset();
    self->cache.reset();
    self->atlas.reset();
    self->fontCollection.reset();
    self->shaper.reset();
    self->discovery.reset();
    self->rasterizer.reset();
    self->pty.reset();
    self->parser.reset();
    self->screen.reset();

    G_OBJECT_CLASS(terminal_widget_parent_class)->dispose(object);
}

static void terminal_widget_class_init(TerminalWidgetClass* klass) {
    GObjectClass* obj_class = G_OBJECT_CLASS(klass);
    obj_class->dispose = terminal_widget_dispose;

    GtkWidgetClass* widget_class = GTK_WIDGET_CLASS(klass);
    widget_class->realize = terminal_widget_realize;
    widget_class->unrealize = terminal_widget_unrealize;

    GtkGLAreaClass* gl_class = GTK_GL_AREA_CLASS(klass);
    gl_class->render = terminal_widget_render;
}

static void terminal_widget_init(TerminalWidget* self) {
    // Placement-new for C++ objects in the GObject struct
    new (&self->screen) std::unique_ptr<Screen>();
    new (&self->parser) std::unique_ptr<VtParser>();
    new (&self->pty) std::unique_ptr<Pty>();
    new (&self->rasterizer) std::unique_ptr<IFontRasterizer>();
    new (&self->discovery) std::unique_ptr<IFontDiscovery>();
    new (&self->shaper) std::unique_ptr<FontShaper>();
    new (&self->fontCollection) std::unique_ptr<FontCollection>();
    new (&self->atlas) std::unique_ptr<GlyphAtlas>();
    new (&self->cache) std::unique_ptr<GlyphCache>();
    new (&self->renderer) std::unique_ptr<GLTextRenderer>();

    self->pty_channel = nullptr;
    self->pty_watch_id = 0;
    self->render_timer_id = 0;
    self->cell_width = 8.0f;
    self->cell_height = 16.0f;
    self->term_rows = 24;
    self->term_cols = 80;
    self->needs_render = false;

    gtk_gl_area_set_required_version(GTK_GL_AREA(self), 3, 3);
    gtk_widget_set_focusable(GTK_WIDGET(self), TRUE);
    gtk_widget_set_can_focus(GTK_WIDGET(self), TRUE);

    // Key event controller
    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed",
                     G_CALLBACK(on_key_pressed), self);
    gtk_widget_add_controller(GTK_WIDGET(self), key_ctrl);

    // Resize signal
    g_signal_connect(self, "resize", G_CALLBACK(terminal_widget_resize), nullptr);
}

static void terminal_widget_realize(GtkWidget* widget) {
    GTK_WIDGET_CLASS(terminal_widget_parent_class)->realize(widget);

    TerminalWidget* self = TERMINAL_WIDGET(widget);

    gtk_gl_area_make_current(GTK_GL_AREA(widget));
    if (gtk_gl_area_get_error(GTK_GL_AREA(widget)) != nullptr) return;

    // Font stack
    self->rasterizer = createFreeTypeRasterizer();
    self->discovery = createFontconfigDiscovery();
    self->shaper = std::make_unique<FontShaper>();
    self->fontCollection = std::make_unique<FontCollection>(
        *self->rasterizer, *self->discovery, *self->shaper);
    self->fontCollection->setPrimaryFont("monospace", 14.0f);
    self->atlas = std::make_unique<GlyphAtlas>();
    self->cache = std::make_unique<GlyphCache>();

    // Cell dimensions
    auto metrics = self->fontCollection->primaryMetrics();
    self->cell_width = metrics.cell_width > 0 ? metrics.cell_width : 8.0f;
    self->cell_height = metrics.cell_height > 0 ? metrics.cell_height : 16.0f;

    // Screen + Parser
    self->screen = std::make_unique<Screen>(self->term_rows, self->term_cols);
    self->parser = std::make_unique<VtParser>(*self->screen);

    // Renderer
    self->renderer = std::make_unique<GLTextRenderer>();
    self->renderer->initialize();
    self->renderer->setFontStack(
        self->fontCollection.get(), self->cache.get(),
        self->atlas.get(), self->rasterizer.get());

    // Render timer (~60 fps)
    self->render_timer_id =
        g_timeout_add(16, on_render_tick, self);
}

static void terminal_widget_unrealize(GtkWidget* widget) {
    TerminalWidget* self = TERMINAL_WIDGET(widget);

    if (self->render_timer_id) {
        g_source_remove(self->render_timer_id);
        self->render_timer_id = 0;
    }

    gtk_gl_area_make_current(GTK_GL_AREA(widget));
    self->renderer.reset();

    GTK_WIDGET_CLASS(terminal_widget_parent_class)->unrealize(widget);
}

static gboolean terminal_widget_render(GtkGLArea* area,
                                        GdkGLContext* /*context*/) {
    TerminalWidget* self = TERMINAL_WIDGET(area);
    if (self->renderer && self->screen) {
        self->renderer->render(*self->screen);
    }
    return TRUE;
}

static void terminal_widget_resize(GtkGLArea* area, int width, int height) {
    TerminalWidget* self = TERMINAL_WIDGET(area);

    if (self->renderer) {
        self->renderer->resize(static_cast<float>(width),
                                static_cast<float>(height));
    }

    if (self->cell_width > 0 && self->cell_height > 0) {
        int cols = std::max(1, static_cast<int>(width / self->cell_width));
        int rows = std::max(1, static_cast<int>(height / self->cell_height));

        if (rows != self->term_rows || cols != self->term_cols) {
            self->term_rows = rows;
            self->term_cols = cols;
            if (self->screen) self->screen->resize(rows, cols);
            if (self->pty && self->pty->isAlive()) {
                self->pty->resize(rows, cols);
            }
            self->needs_render = true;
        }
    }
}

static void send_pty_data(TerminalWidget* self, const char* data,
                           size_t len) {
    if (self->pty && self->pty->isAlive()) {
        self->pty->write(data, len);
    }
}

static gboolean on_key_pressed(GtkEventControllerKey* /*controller*/,
                                guint keyval, guint /*keycode*/,
                                GdkModifierType state,
                                gpointer user_data) {
    TerminalWidget* self = TERMINAL_WIDGET(user_data);
    if (!self->pty) return FALSE;

    bool app_cursor = self->screen && self->screen->appCursorKeys();
    const char* pfx = app_cursor ? "\x1bO" : "\x1b[";

    switch (keyval) {
        case GDK_KEY_Up:    { char s[3]={pfx[0],pfx[1],'A'}; send_pty_data(self,s,3); return TRUE; }
        case GDK_KEY_Down:  { char s[3]={pfx[0],pfx[1],'B'}; send_pty_data(self,s,3); return TRUE; }
        case GDK_KEY_Right: { char s[3]={pfx[0],pfx[1],'C'}; send_pty_data(self,s,3); return TRUE; }
        case GDK_KEY_Left:  { char s[3]={pfx[0],pfx[1],'D'}; send_pty_data(self,s,3); return TRUE; }
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            send_pty_data(self, "\r", 1); return TRUE;
        case GDK_KEY_BackSpace:
            send_pty_data(self, "\x7f", 1); return TRUE;
        case GDK_KEY_Tab:
            send_pty_data(self, "\t", 1); return TRUE;
        case GDK_KEY_Escape:
            send_pty_data(self, "\x1b", 1); return TRUE;
        case GDK_KEY_Home:
            send_pty_data(self, "\x1b[H", 3); return TRUE;
        case GDK_KEY_End:
            send_pty_data(self, "\x1b[F", 3); return TRUE;
        case GDK_KEY_Page_Up:
            send_pty_data(self, "\x1b[5~", 4); return TRUE;
        case GDK_KEY_Page_Down:
            send_pty_data(self, "\x1b[6~", 4); return TRUE;
        case GDK_KEY_Delete:
            send_pty_data(self, "\x1b[3~", 4); return TRUE;
        case GDK_KEY_F1:
            send_pty_data(self, "\x1bOP", 3); return TRUE;
        case GDK_KEY_F2:
            send_pty_data(self, "\x1bOQ", 3); return TRUE;
        case GDK_KEY_F3:
            send_pty_data(self, "\x1bOR", 3); return TRUE;
        case GDK_KEY_F4:
            send_pty_data(self, "\x1bOS", 3); return TRUE;
        default:
            break;
    }

    // Handle Ctrl+key
    if (state & GDK_CONTROL_MASK) {
        if (keyval >= 'a' && keyval <= 'z') {
            char c = static_cast<char>(keyval - 'a' + 1);
            send_pty_data(self, &c, 1);
            return TRUE;
        }
    }

    // Regular character input
    gunichar ch = gdk_keyval_to_unicode(keyval);
    if (ch != 0 && g_unichar_isprint(ch)) {
        char utf8[6];
        int len = g_unichar_to_utf8(ch, utf8);
        if (len > 0) {
            send_pty_data(self, utf8, static_cast<size_t>(len));
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean on_pty_read(GIOChannel* source,
                             GIOCondition /*condition*/,
                             gpointer user_data) {
    TerminalWidget* self = TERMINAL_WIDGET(user_data);
    if (!self->pty) return FALSE;

    char buf[8192];
    int n = self->pty->read(buf, sizeof(buf));
    if (n > 0) {
        self->parser->feed(buf, static_cast<size_t>(n));
        self->needs_render = true;
        return TRUE;
    } else if (n < 0) {
        self->pty_watch_id = 0;
        return FALSE;
    }
    return TRUE;
}

static gboolean on_render_tick(gpointer user_data) {
    TerminalWidget* self = TERMINAL_WIDGET(user_data);
    if (self->needs_render) {
        self->needs_render = false;
        gtk_gl_area_queue_render(GTK_GL_AREA(self));
    }
    return G_SOURCE_CONTINUE;
}

// Public API

GtkWidget* terminal_widget_new(void) {
    return GTK_WIDGET(g_object_new(TERMINAL_TYPE_WIDGET, nullptr));
}

void terminal_widget_start_shell(TerminalWidget* self) {
    g_return_if_fail(TERMINAL_IS_WIDGET(self));

    self->pty = termcore::createPty();
    if (!self->pty->spawn("", {}, "", self->term_rows, self->term_cols)) {
        g_warning("BreadTerminal: failed to spawn shell");
        return;
    }

    int fd = self->pty->fd();

    self->pty_channel = g_io_channel_unix_new(fd);
    g_io_channel_set_encoding(self->pty_channel, nullptr, nullptr);
    g_io_channel_set_buffered(self->pty_channel, FALSE);
    g_io_channel_set_flags(self->pty_channel, G_IO_FLAG_NONBLOCK, nullptr);

    self->pty_watch_id = g_io_add_watch(
        self->pty_channel, G_IO_IN, on_pty_read, self);
}
