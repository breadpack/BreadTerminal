#include "TerminalWidgetPrivate.h"
#include "FreeTypeRasterizer.h"
#include "FontconfigDiscovery.h"
#include "GLTextRenderer.h"

#include "termcore/mouse.h"

#include <algorithm>
#include <cmath>

using namespace termcore;

G_DEFINE_TYPE(TerminalWidget, terminal_widget, GTK_TYPE_GL_AREA)

// Forward declarations
static void terminal_widget_realize(GtkWidget* widget);
static void terminal_widget_unrealize(GtkWidget* widget);
static gboolean terminal_widget_render(GtkGLArea* area, GdkGLContext* context);
static void terminal_widget_resize(GtkGLArea* area, int width, int height);
static gboolean on_pty_read(GIOChannel* source, GIOCondition condition,
                             gpointer user_data);
static gboolean on_render_tick(gpointer user_data);

// ---- Shared helpers ----

void terminal_widget_send_pty_data(TerminalWidget* self, const char* data,
                                    size_t len) {
    if (self->pty && self->pty->isAlive()) {
        self->pty->write(data, len);
    }
}

void terminal_widget_recalculate_grid(TerminalWidget* self) {
    auto metrics = self->fontCollection->primaryMetrics();
    self->cell_width = metrics.cell_width > 0 ? metrics.cell_width : 8.0f;
    self->cell_height = metrics.cell_height > 0 ? metrics.cell_height : 16.0f;

    int width = gtk_widget_get_width(GTK_WIDGET(self));
    int height = gtk_widget_get_height(GTK_WIDGET(self));
    if (width > 0 && height > 0) {
        int cols = std::max(1, static_cast<int>(width / self->cell_width));
        int rows = std::max(1, static_cast<int>(height / self->cell_height));
        if (rows != self->term_rows || cols != self->term_cols) {
            self->term_rows = rows;
            self->term_cols = cols;
            if (self->screen) self->screen->resize(rows, cols);
            if (self->pty && self->pty->isAlive()) {
                self->pty->resize(rows, cols);
            }
        }
    }

    if (self->cache) self->cache->clear();

    self->needs_render = true;
}

// ---- Lifecycle ----

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
    self->keybindings.reset();

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
    new (&self->keybindings) std::unique_ptr<KeybindingManager>();
    new (&self->config) Config();

    self->config_loaded = false;
    self->pty_channel = nullptr;
    self->pty_watch_id = 0;
    self->render_timer_id = 0;
    self->cell_width = 8.0f;
    self->cell_height = 16.0f;
    self->term_rows = 24;
    self->term_cols = 80;
    self->needs_render = false;
    self->search_open = false;

    gtk_gl_area_set_required_version(GTK_GL_AREA(self), 3, 3);
    gtk_widget_set_focusable(GTK_WIDGET(self), TRUE);
    gtk_widget_set_can_focus(GTK_WIDGET(self), TRUE);

    // Key event controller
    GtkEventController* key_ctrl = gtk_event_controller_key_new();
    g_signal_connect(key_ctrl, "key-pressed",
                     G_CALLBACK(terminal_widget_on_key_pressed), self);
    gtk_widget_add_controller(GTK_WIDGET(self), key_ctrl);

    // Scroll event controller
    GtkEventController* scroll_ctrl = gtk_event_controller_scroll_new(
        GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
    g_signal_connect(scroll_ctrl, "scroll",
                     G_CALLBACK(terminal_widget_on_scroll), self);
    gtk_widget_add_controller(GTK_WIDGET(self), scroll_ctrl);

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

    if (self->config_loaded) {
        self->fontCollection->setPrimaryFont(
            self->config.font_family, self->config.font_size);
    } else {
        self->fontCollection->setPrimaryFont("monospace", 14.0f);
    }

    self->atlas = std::make_unique<GlyphAtlas>();
    self->cache = std::make_unique<GlyphCache>();

    // Cell dimensions
    auto metrics = self->fontCollection->primaryMetrics();
    self->cell_width = metrics.cell_width > 0 ? metrics.cell_width : 8.0f;
    self->cell_height = metrics.cell_height > 0 ? metrics.cell_height : 16.0f;

    // Screen + Parser
    self->screen = std::make_unique<Screen>(self->term_rows, self->term_cols);
    self->parser = std::make_unique<VtParser>(*self->screen);

    // Apply dynamic colors from config
    if (self->config_loaded) {
        self->screen->initDynamicColors(self->config);
    }

    // Keybindings
    self->keybindings = std::make_unique<KeybindingManager>();
    if (self->config_loaded && !self->config.keybindings.empty()) {
        std::vector<std::pair<std::string, std::string>> bindings;
        bindings.reserve(self->config.keybindings.size());
        for (const auto& kb : self->config.keybindings) {
            bindings.emplace_back(kb.trigger, kb.action);
        }
        self->keybindings->loadFromConfig(bindings);
    }

    // Renderer
    self->renderer = std::make_unique<GLTextRenderer>();
    self->renderer->initialize();
    self->renderer->setFontStack(
        self->fontCollection.get(), self->cache.get(),
        self->atlas.get(), self->rasterizer.get());

    // Render timer (~60 fps)
    self->render_timer_id = g_timeout_add(16, on_render_tick, self);
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

// ---- Rendering ----

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

// ---- PTY read callback ----

static gboolean on_pty_read(GIOChannel* /*source*/,
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

// ---- Render tick ----

static gboolean on_render_tick(gpointer user_data) {
    TerminalWidget* self = TERMINAL_WIDGET(user_data);
    if (self->needs_render) {
        self->needs_render = false;
        gtk_gl_area_queue_render(GTK_GL_AREA(self));
    }
    return G_SOURCE_CONTINUE;
}

// ---- Public API ----

GtkWidget* terminal_widget_new(void) {
    return GTK_WIDGET(g_object_new(TERMINAL_TYPE_WIDGET, nullptr));
}

void terminal_widget_set_config(TerminalWidget* self, const Config& config) {
    g_return_if_fail(TERMINAL_IS_WIDGET(self));
    self->config = config;
    self->config_loaded = true;
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
