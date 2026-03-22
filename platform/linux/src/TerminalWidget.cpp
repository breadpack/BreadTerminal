#include "TerminalWidgetPrivate.h"
#include "FreeTypeRasterizer.h"
#include "FontconfigDiscovery.h"
#include "GLTextRenderer.h"

#include <algorithm>
#include <cmath>

using namespace termcore;

G_DEFINE_TYPE(TerminalWidget, terminal_widget, GTK_TYPE_GL_AREA)

// Forward declarations
static void terminal_widget_realize(GtkWidget* widget);
static void terminal_widget_unrealize(GtkWidget* widget);
static gboolean terminal_widget_render(GtkGLArea* area, GdkGLContext* context);
static void terminal_widget_resize(GtkGLArea* area, int width, int height);
static gboolean on_render_tick(gpointer user_data);

// ---- Lifecycle ----

static void terminal_widget_dispose(GObject* object) {
    TerminalWidget* self = TERMINAL_WIDGET(object);

    if (self->render_timer_id) {
        g_source_remove(self->render_timer_id);
        self->render_timer_id = 0;
    }

    // Destroy controller before platform host (controller references host)
    self->controller.reset();
    self->platformHost.reset();

    self->renderer.reset();
    self->cache.reset();
    self->atlas.reset();
    self->fontCollection.reset();
    self->shaper.reset();
    self->discovery.reset();
    self->rasterizer.reset();

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
    new (&self->platformHost) std::unique_ptr<GtkPlatformHost>();
    new (&self->controller) std::unique_ptr<termcore::TerminalController>();
    new (&self->rasterizer) std::unique_ptr<IFontRasterizer>();
    new (&self->discovery) std::unique_ptr<IFontDiscovery>();
    new (&self->shaper) std::unique_ptr<FontShaper>();
    new (&self->fontCollection) std::unique_ptr<FontCollection>();
    new (&self->atlas) std::unique_ptr<GlyphAtlas>();
    new (&self->cache) std::unique_ptr<GlyphCache>();
    new (&self->renderer) std::unique_ptr<GLTextRenderer>();
    new (&self->config) Config();

    self->config_loaded = false;
    self->render_timer_id = 0;

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

    // Renderer
    self->renderer = std::make_unique<GLTextRenderer>();
    self->renderer->initialize();
    self->renderer->setFontStack(
        self->fontCollection.get(), self->cache.get(),
        self->atlas.get(), self->rasterizer.get());

    // Platform host + controller
    self->platformHost = std::make_unique<GtkPlatformHost>(GTK_WIDGET(self));

    self->controller = std::make_unique<termcore::TerminalController>(
        self->platformHost.get(), self->config, self->fontCollection.get());

    // Wire controller back to platform host for search/config callbacks
    self->platformHost->setController(self->controller.get());

    // Render / poll timer (~60 fps)
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
    if (self->renderer && self->controller) {
        Screen* screen = self->controller->activeScreen();
        if (screen) {
            self->renderer->render(*screen);
        }
    }
    return TRUE;
}

static void terminal_widget_resize(GtkGLArea* area, int width, int height) {
    TerminalWidget* self = TERMINAL_WIDGET(area);

    if (self->renderer) {
        self->renderer->resize(static_cast<float>(width),
                                static_cast<float>(height));
    }

    // Delegate resize to controller (it recalculates grid, resizes PTY, etc.)
    if (self->controller) {
        self->controller->onResize(width, height);
    }
}

// ---- Render / poll tick ----

static gboolean on_render_tick(gpointer user_data) {
    TerminalWidget* self = TERMINAL_WIDGET(user_data);

    if (self->controller) {
        // Poll PTY data (controller feeds VtParser internally)
        self->controller->pollPty();

        // Cursor blink, resize overlay timeout, etc.
        self->controller->tick();

        // Queue a render if the controller says we need one
        if (self->controller->needsRender()) {
            self->controller->clearNeedsRender();
            gtk_gl_area_queue_render(GTK_GL_AREA(self));
        }
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

    if (self->controller) {
        self->controller->initTerminal();
    }
}
