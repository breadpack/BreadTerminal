#include "WaylandWindow.h"

#include <wayland-client.h>
#include <wayland-egl.h>
#include <xdg-shell-client-protocol.h>
#include <EGL/egl.h>
#include <xkbcommon/xkbcommon.h>

#include <cstring>
#include <cstdio>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

namespace termcore {

// ─── Registry listener ──────────────────────────────────────────────

static void registry_handle_global(void* data, wl_registry* registry,
                                   uint32_t name, const char* interface,
                                   uint32_t version) {
    auto* state = static_cast<WaylandState*>(data);

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->compositor = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, 4));
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->wm_base = static_cast<xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        state->seat = static_cast<wl_seat*>(
            wl_registry_bind(registry, name, &wl_seat_interface, 5));
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        state->output = static_cast<wl_output*>(
            wl_registry_bind(registry, name, &wl_output_interface, 2));
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->shm = static_cast<wl_shm*>(
            wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (strcmp(interface, wl_data_device_manager_interface.name) == 0) {
        state->data_device_manager = static_cast<wl_data_device_manager*>(
            wl_registry_bind(registry, name,
                             &wl_data_device_manager_interface, 3));
    }
}

static void registry_handle_global_remove(void* /*data*/,
                                          wl_registry* /*registry*/,
                                          uint32_t /*name*/) {
    // Global removal handling (e.g., hot-unplug) — not critical for MVP
}

static const wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

// ─── xdg_wm_base listener (ping/pong) ──────────────────────────────

static void xdg_wm_base_ping(void* /*data*/, xdg_wm_base* wm_base,
                              uint32_t serial) {
    xdg_wm_base_pong(wm_base, serial);
}

static const xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

// ─── xdg_surface listener ──────────────────────────────────────────

static void xdg_surface_configure(void* data, xdg_surface* surface,
                                  uint32_t serial) {
    auto* state = static_cast<WaylandState*>(data);
    xdg_surface_ack_configure(surface, serial);
    state->configured = true;
}

static const xdg_surface_listener xdg_surface_listener_impl = {
    .configure = xdg_surface_configure,
};

// ─── xdg_toplevel listener ─────────────────────────────────────────

struct ToplevelData {
    WaylandState* state;
    WaylandCallbacks* callbacks;
};

static void xdg_toplevel_configure(void* data, xdg_toplevel* /*toplevel*/,
                                   int32_t width, int32_t height,
                                   wl_array* /*states*/) {
    auto* td = static_cast<ToplevelData*>(data);
    if (width > 0 && height > 0) {
        td->state->width = width;
        td->state->height = height;
        if (td->callbacks && td->callbacks->onResize) {
            td->callbacks->onResize(width, height);
        }
    }
}

static void xdg_toplevel_close(void* data, xdg_toplevel* /*toplevel*/) {
    auto* td = static_cast<ToplevelData*>(data);
    td->state->closed = true;
    if (td->callbacks && td->callbacks->onClose) {
        td->callbacks->onClose();
    }
}

static const xdg_toplevel_listener toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

// ─── Keyboard listener ─────────────────────────────────────────────

struct KeyboardData {
    WaylandState* state;
    WaylandCallbacks* callbacks;
    uint8_t* current_modifiers;
};

static void keyboard_keymap(void* data, wl_keyboard* /*kb*/,
                            uint32_t format, int32_t fd, uint32_t size) {
    auto* kd = static_cast<KeyboardData*>(data);
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }

    char* map_str = static_cast<char*>(
        mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (map_str == MAP_FAILED) {
        close(fd);
        return;
    }

    if (kd->state->xkb_keymap) {
        xkb_keymap_unref(kd->state->xkb_keymap);
    }
    if (kd->state->xkb_state) {
        xkb_state_unref(kd->state->xkb_state);
    }

    kd->state->xkb_keymap = xkb_keymap_new_from_string(
        kd->state->xkb_ctx, map_str, XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS);

    munmap(map_str, size);
    close(fd);

    if (kd->state->xkb_keymap) {
        kd->state->xkb_state =
            xkb_state_new(kd->state->xkb_keymap);
    }
}

static void keyboard_enter(void* /*data*/, wl_keyboard* /*kb*/,
                           uint32_t /*serial*/, wl_surface* /*surface*/,
                           wl_array* /*keys*/) {
    // Focus gained
}

static void keyboard_leave(void* /*data*/, wl_keyboard* /*kb*/,
                           uint32_t /*serial*/, wl_surface* /*surface*/) {
    // Focus lost
}

static void keyboard_key(void* data, wl_keyboard* /*kb*/,
                         uint32_t /*serial*/, uint32_t /*time*/,
                         uint32_t key, uint32_t state_val) {
    auto* kd = static_cast<KeyboardData*>(data);
    if (state_val != WL_KEYBOARD_KEY_STATE_PRESSED) return;
    if (!kd->state->xkb_state) return;

    // Linux evdev keycodes are offset by 8 from XKB keycodes
    uint32_t xkb_keycode = key + 8;

    KeyEvent event;
    event.keycode = xkb_keycode;
    event.modifiers = *kd->current_modifiers;

    // Get UTF-8 text for the key
    char buf[64] = {};
    int len = xkb_state_key_get_utf8(kd->state->xkb_state, xkb_keycode,
                                     buf, sizeof(buf));
    if (len > 0) {
        event.text = std::string(buf, len);
    }

    if (kd->callbacks && kd->callbacks->onKey) {
        kd->callbacks->onKey(event);
    }
}

static void keyboard_modifiers(void* data, wl_keyboard* /*kb*/,
                               uint32_t /*serial*/,
                               uint32_t mods_depressed,
                               uint32_t mods_latched,
                               uint32_t mods_locked,
                               uint32_t group) {
    auto* kd = static_cast<KeyboardData*>(data);
    if (!kd->state->xkb_state) return;

    xkb_state_update_mask(kd->state->xkb_state,
                          mods_depressed, mods_latched, mods_locked,
                          0, 0, group);

    uint8_t mods = ModNone;
    if (xkb_state_mod_name_is_active(kd->state->xkb_state,
            XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE))
        mods |= ModShift;
    if (xkb_state_mod_name_is_active(kd->state->xkb_state,
            XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE))
        mods |= ModCtrl;
    if (xkb_state_mod_name_is_active(kd->state->xkb_state,
            XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE))
        mods |= ModAlt;
    if (xkb_state_mod_name_is_active(kd->state->xkb_state,
            XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE))
        mods |= ModSuper;

    *kd->current_modifiers = mods;
}

static void keyboard_repeat_info(void* /*data*/, wl_keyboard* /*kb*/,
                                 int32_t /*rate*/, int32_t /*delay*/) {
    // Key repeat configuration — use for future auto-repeat support
}

static const wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

// ─── Pointer listener ──────────────────────────────────────────────

struct PointerData {
    WaylandState* state;
    WaylandCallbacks* callbacks;
    uint8_t* current_modifiers;
    int pointer_x = 0;
    int pointer_y = 0;
};

static void pointer_enter(void* data, wl_pointer* /*pointer*/,
                          uint32_t /*serial*/, wl_surface* /*surface*/,
                          wl_fixed_t sx, wl_fixed_t sy) {
    auto* pd = static_cast<PointerData*>(data);
    pd->pointer_x = wl_fixed_to_int(sx);
    pd->pointer_y = wl_fixed_to_int(sy);
}

static void pointer_leave(void* /*data*/, wl_pointer* /*pointer*/,
                          uint32_t /*serial*/, wl_surface* /*surface*/) {
    // Pointer left the surface
}

static void pointer_motion(void* data, wl_pointer* /*pointer*/,
                           uint32_t /*time*/,
                           wl_fixed_t sx, wl_fixed_t sy) {
    auto* pd = static_cast<PointerData*>(data);
    pd->pointer_x = wl_fixed_to_int(sx);
    pd->pointer_y = wl_fixed_to_int(sy);

    InputMouseEvent event;
    event.type = InputMouseEvent::Move;
    event.x = pd->pointer_x;
    event.y = pd->pointer_y;
    event.modifiers = *pd->current_modifiers;

    if (pd->callbacks && pd->callbacks->onMouse) {
        pd->callbacks->onMouse(event);
    }
}

static void pointer_button(void* data, wl_pointer* /*pointer*/,
                           uint32_t /*serial*/, uint32_t /*time*/,
                           uint32_t button, uint32_t state_val) {
    auto* pd = static_cast<PointerData*>(data);

    InputMouseEvent event;
    event.type = (state_val == WL_POINTER_BUTTON_STATE_PRESSED)
                     ? InputMouseEvent::Press
                     : InputMouseEvent::Release;
    event.x = pd->pointer_x;
    event.y = pd->pointer_y;
    event.modifiers = *pd->current_modifiers;

    // Linux button codes: BTN_LEFT=272, BTN_RIGHT=273, BTN_MIDDLE=274
    switch (button) {
    case 272: event.button = 0; break; // left
    case 274: event.button = 1; break; // middle
    case 273: event.button = 2; break; // right
    default:  event.button = 0; break;
    }

    if (pd->callbacks && pd->callbacks->onMouse) {
        pd->callbacks->onMouse(event);
    }
}

static void pointer_axis(void* data, wl_pointer* /*pointer*/,
                         uint32_t /*time*/, uint32_t axis,
                         wl_fixed_t value) {
    auto* pd = static_cast<PointerData*>(data);
    if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;

    int scroll = wl_fixed_to_int(value);
    InputMouseEvent event;
    event.type = (scroll > 0) ? InputMouseEvent::ScrollDown
                              : InputMouseEvent::ScrollUp;
    event.x = pd->pointer_x;
    event.y = pd->pointer_y;
    event.modifiers = *pd->current_modifiers;
    event.scrollLines = (scroll > 0) ? scroll : -scroll;
    if (event.scrollLines == 0) event.scrollLines = 1;

    if (pd->callbacks && pd->callbacks->onMouse) {
        pd->callbacks->onMouse(event);
    }
}

static const wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
};

// ─── Seat capabilities ─────────────────────────────────────────────

struct SeatData {
    WaylandState* state;
    KeyboardData* keyboard_data;
    PointerData* pointer_data;
};

static void seat_capabilities(void* data, wl_seat* seat,
                              uint32_t caps) {
    auto* sd = static_cast<SeatData*>(data);

    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !sd->state->keyboard) {
        sd->state->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(sd->state->keyboard,
                                 &keyboard_listener,
                                 sd->keyboard_data);
    }

    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !sd->state->pointer) {
        sd->state->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(sd->state->pointer,
                                &pointer_listener,
                                sd->pointer_data);
    }
}

static void seat_name(void* /*data*/, wl_seat* /*seat*/,
                      const char* /*name*/) {
    // Seat name (informational)
}

static const wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

// ─── WaylandWindow implementation ──────────────────────────────────

// Per-instance listener data stored alongside the window.
// These are heap-allocated so their addresses remain stable for
// Wayland C callback pointers.
struct WaylandWindow::ListenerData {
    ToplevelData toplevel_data;
    KeyboardData keyboard_data;
    PointerData pointer_data;
    SeatData seat_data;
};

WaylandWindow::WaylandWindow() = default;

WaylandWindow::~WaylandWindow() {
    // Tear down EGL
    if (state_.egl_surface) {
        eglDestroySurface(state_.egl_display, state_.egl_surface);
    }
    if (state_.egl_context) {
        eglDestroyContext(state_.egl_display, state_.egl_context);
    }
    if (state_.egl_window) {
        wl_egl_window_destroy(state_.egl_window);
    }
    if (state_.egl_display) {
        eglTerminate(state_.egl_display);
    }

    // Tear down xkbcommon
    if (state_.xkb_state) xkb_state_unref(state_.xkb_state);
    if (state_.xkb_keymap) xkb_keymap_unref(state_.xkb_keymap);
    if (state_.xkb_ctx) xkb_context_unref(state_.xkb_ctx);

    // Tear down Wayland objects (reverse order of creation)
    if (state_.pointer) wl_pointer_destroy(state_.pointer);
    if (state_.keyboard) wl_keyboard_destroy(state_.keyboard);
    if (state_.data_device) wl_data_device_destroy(state_.data_device);
    if (state_.toplevel) xdg_toplevel_destroy(state_.toplevel);
    if (state_.xdg_surface) xdg_surface_destroy(state_.xdg_surface);
    if (state_.surface) wl_surface_destroy(state_.surface);
    if (state_.seat) wl_seat_destroy(state_.seat);
    if (state_.data_device_manager) {
        wl_data_device_manager_destroy(state_.data_device_manager);
    }
    if (state_.shm) wl_shm_destroy(state_.shm);
    if (state_.output) wl_output_destroy(state_.output);
    if (state_.wm_base) xdg_wm_base_destroy(state_.wm_base);
    if (state_.compositor) wl_compositor_destroy(state_.compositor);
    if (state_.registry) wl_registry_destroy(state_.registry);
    if (state_.display) wl_display_disconnect(state_.display);
}

bool WaylandWindow::init() {
    // Connect to Wayland compositor
    state_.display = wl_display_connect(nullptr);
    if (!state_.display) {
        fprintf(stderr, "WaylandWindow: failed to connect to Wayland display\n");
        return false;
    }

    // Initialize xkbcommon
    state_.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!state_.xkb_ctx) {
        fprintf(stderr, "WaylandWindow: failed to create xkb context\n");
        return false;
    }

    // Get registry and bind globals
    state_.registry = wl_display_get_registry(state_.display);
    wl_registry_add_listener(state_.registry, &registry_listener, &state_);
    wl_display_roundtrip(state_.display);

    if (!state_.compositor) {
        fprintf(stderr, "WaylandWindow: compositor not found\n");
        return false;
    }
    if (!state_.wm_base) {
        fprintf(stderr, "WaylandWindow: xdg_wm_base not found\n");
        return false;
    }

    // xdg_wm_base ping/pong
    xdg_wm_base_add_listener(state_.wm_base, &wm_base_listener, nullptr);

    // Create surface
    state_.surface = wl_compositor_create_surface(state_.compositor);
    if (!state_.surface) {
        fprintf(stderr, "WaylandWindow: failed to create surface\n");
        return false;
    }

    // Create xdg_surface and toplevel
    state_.xdg_surface =
        xdg_wm_base_get_xdg_surface(state_.wm_base, state_.surface);
    xdg_surface_add_listener(state_.xdg_surface,
                             &xdg_surface_listener_impl, &state_);

    state_.toplevel = xdg_surface_get_toplevel(state_.xdg_surface);

    // Set up listener data
    listener_data_ = std::make_unique<ListenerData>();
    listener_data_->toplevel_data = {&state_, &callbacks_};
    listener_data_->keyboard_data = {&state_, &callbacks_,
                                     &current_modifiers_};
    listener_data_->pointer_data = {&state_, &callbacks_,
                                    &current_modifiers_, 0, 0};
    listener_data_->seat_data = {&state_,
                                 &listener_data_->keyboard_data,
                                 &listener_data_->pointer_data};

    xdg_toplevel_add_listener(state_.toplevel, &toplevel_listener,
                              &listener_data_->toplevel_data);

    xdg_toplevel_set_title(state_.toplevel, "BreadTerminal");
    xdg_toplevel_set_app_id(state_.toplevel, "com.breadterminal");

    // Set up seat listeners for keyboard/pointer
    if (state_.seat) {
        wl_seat_add_listener(state_.seat, &seat_listener,
                             &listener_data_->seat_data);
    }

    // Create data device for clipboard
    if (state_.data_device_manager && state_.seat) {
        state_.data_device = wl_data_device_manager_get_data_device(
            state_.data_device_manager, state_.seat);
    }

    // Commit surface to trigger initial configure
    wl_surface_commit(state_.surface);
    wl_display_roundtrip(state_.display);

    return true;
}

bool WaylandWindow::createEGLContext() {
    state_.egl_display = eglGetDisplay(
        reinterpret_cast<EGLNativeDisplayType>(state_.display));
    if (state_.egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "WaylandWindow: eglGetDisplay failed\n");
        return false;
    }

    EGLint major, minor;
    if (!eglInitialize(state_.egl_display, &major, &minor)) {
        fprintf(stderr, "WaylandWindow: eglInitialize failed\n");
        return false;
    }

    if (!eglBindAPI(EGL_OPENGL_API)) {
        fprintf(stderr, "WaylandWindow: eglBindAPI(EGL_OPENGL_API) failed\n");
        return false;
    }

    // Choose EGL config
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE,
    };

    EGLint num_configs;
    if (!eglChooseConfig(state_.egl_display, config_attribs,
                         &state_.egl_config, 1, &num_configs) ||
        num_configs == 0) {
        fprintf(stderr, "WaylandWindow: eglChooseConfig failed\n");
        return false;
    }

    // Create OpenGL 3.3 core context
    EGLint context_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE,
    };

    state_.egl_context = eglCreateContext(
        state_.egl_display, state_.egl_config, EGL_NO_CONTEXT,
        context_attribs);
    if (state_.egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "WaylandWindow: eglCreateContext failed\n");
        return false;
    }

    // Create wl_egl_window and EGL surface
    state_.egl_window =
        wl_egl_window_create(state_.surface, state_.width, state_.height);
    if (!state_.egl_window) {
        fprintf(stderr, "WaylandWindow: wl_egl_window_create failed\n");
        return false;
    }

    state_.egl_surface = eglCreateWindowSurface(
        state_.egl_display, state_.egl_config,
        reinterpret_cast<EGLNativeWindowType>(state_.egl_window), nullptr);
    if (state_.egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "WaylandWindow: eglCreateWindowSurface failed\n");
        return false;
    }

    return makeCurrent();
}

bool WaylandWindow::makeCurrent() {
    return eglMakeCurrent(state_.egl_display, state_.egl_surface,
                          state_.egl_surface, state_.egl_context) == EGL_TRUE;
}

void WaylandWindow::swapBuffers() {
    eglSwapBuffers(state_.egl_display, state_.egl_surface);
}

bool WaylandWindow::processEvents() {
    if (!state_.display) return false;

    // Flush outgoing requests
    wl_display_flush(state_.display);

    // Dispatch pending events (non-blocking via dispatch_pending)
    if (wl_display_dispatch_pending(state_.display) < 0) {
        return false;
    }

    // Also prepare read and poll for new events with zero timeout
    // This makes processEvents() non-blocking
    if (wl_display_prepare_read(state_.display) == 0) {
        // No events queued — try to read from the fd
        struct pollfd pfd = {
            .fd = wl_display_get_fd(state_.display),
            .events = POLLIN,
            .revents = 0,
        };
        if (poll(&pfd, 1, 0) > 0) {
            wl_display_read_events(state_.display);
            wl_display_dispatch_pending(state_.display);
        } else {
            wl_display_cancel_read(state_.display);
        }
    }

    return true;
}

void WaylandWindow::resize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    state_.width = width;
    state_.height = height;

    if (state_.egl_window) {
        wl_egl_window_resize(state_.egl_window, width, height, 0, 0);
    }
}

void WaylandWindow::setTitle(const std::string& title) {
    if (state_.toplevel) {
        xdg_toplevel_set_title(state_.toplevel, title.c_str());
    }
}

void WaylandWindow::toggleFullscreen() {
    if (!state_.toplevel) return;
    if (state_.fullscreen) {
        xdg_toplevel_unset_fullscreen(state_.toplevel);
    } else {
        xdg_toplevel_set_fullscreen(state_.toplevel, nullptr);
    }
    state_.fullscreen = !state_.fullscreen;
}

void WaylandWindow::close() {
    state_.closed = true;
}

void WaylandWindow::getSize(int& width, int& height) const {
    width = state_.width;
    height = state_.height;
}

int WaylandWindow::scaleFactor() const {
    return state_.scale_factor;
}

bool WaylandWindow::isReady() const {
    return state_.configured;
}

bool WaylandWindow::isClosed() const {
    return state_.closed;
}

void WaylandWindow::setCallbacks(const WaylandCallbacks& callbacks) {
    callbacks_ = callbacks;
}

const WaylandState& WaylandWindow::state() const {
    return state_;
}

WaylandState& WaylandWindow::state() {
    return state_;
}

} // namespace termcore
