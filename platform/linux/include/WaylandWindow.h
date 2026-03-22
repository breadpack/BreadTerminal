#ifndef TERMCORE_WAYLAND_WINDOW_H
#define TERMCORE_WAYLAND_WINDOW_H

#include "termcore/platform_host.h"
#include <functional>
#include <memory>
#include <string>
#include <cstdint>

// Forward declarations for Wayland types
struct wl_display;
struct wl_registry;
struct wl_compositor;
struct wl_surface;
struct wl_seat;
struct wl_keyboard;
struct wl_pointer;
struct wl_output;
struct wl_shm;
struct xdg_wm_base;
struct xdg_surface;
struct xdg_toplevel;
struct wl_data_device_manager;
struct wl_data_device;

// text-input-v3 (IME) forward declarations
struct zwp_text_input_manager_v3;
struct zwp_text_input_v3;

// Forward declarations for EGL
typedef void* EGLDisplay;
typedef void* EGLContext;
typedef void* EGLSurface;
typedef void* EGLConfig;
struct wl_egl_window;

// Forward declaration for xkbcommon
struct xkb_context;
struct xkb_keymap;
struct xkb_state;

namespace termcore {

/// Aggregated Wayland compositor state.
/// Populated during registry binding after connecting to the display.
struct WaylandState {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_compositor* compositor = nullptr;
    xdg_wm_base* wm_base = nullptr;
    wl_seat* seat = nullptr;
    wl_keyboard* keyboard = nullptr;
    wl_pointer* pointer = nullptr;
    wl_output* output = nullptr;
    wl_shm* shm = nullptr;
    wl_data_device_manager* data_device_manager = nullptr;
    wl_data_device* data_device = nullptr;

    // text-input-v3 (IME positioning)
    zwp_text_input_manager_v3* text_input_manager = nullptr;
    zwp_text_input_v3* text_input = nullptr;
    bool text_input_focused = false;

    // Surface hierarchy
    wl_surface* surface = nullptr;
    xdg_surface* xdg_surface = nullptr;
    xdg_toplevel* toplevel = nullptr;

    // EGL state
    EGLDisplay egl_display = nullptr;
    EGLContext egl_context = nullptr;
    EGLSurface egl_surface = nullptr;
    EGLConfig egl_config = nullptr;
    wl_egl_window* egl_window = nullptr;

    // xkbcommon keyboard state
    xkb_context* xkb_ctx = nullptr;
    xkb_keymap* xkb_keymap = nullptr;
    xkb_state* xkb_state = nullptr;

    // Window geometry
    int width = 800;
    int height = 600;
    int scale_factor = 1;
    bool configured = false;
    bool closed = false;
    bool fullscreen = false;
};

/// Callbacks from WaylandWindow to the host layer.
struct WaylandCallbacks {
    std::function<void(const KeyEvent&)> onKey;
    std::function<void(const InputMouseEvent&)> onMouse;
    std::function<void(int width, int height)> onResize;
    std::function<void()> onClose;
    std::function<void()> onRedraw;
};

/// Native Wayland window with EGL context for OpenGL rendering.
/// Creates a Wayland surface + xdg_toplevel and sets up EGL for
/// the existing GLTextRenderer to draw into.
class WaylandWindow {
public:
    WaylandWindow();
    ~WaylandWindow();

    WaylandWindow(const WaylandWindow&) = delete;
    WaylandWindow& operator=(const WaylandWindow&) = delete;

    /// Connect to Wayland compositor, bind globals, create surface.
    /// Returns false if Wayland is not available.
    bool init();

    /// Create EGL context (OpenGL 3.3 core) on the Wayland surface.
    /// Must be called after init().
    bool createEGLContext();

    /// Make the EGL context current on this thread.
    bool makeCurrent();

    /// Swap EGL buffers (present frame).
    void swapBuffers();

    /// Dispatch pending Wayland events (non-blocking).
    /// Returns false if the display connection is lost.
    bool processEvents();

    /// Handle xdg_toplevel configure (resize).
    void resize(int width, int height);

    /// Set the window title via xdg_toplevel.
    void setTitle(const std::string& title);

    /// Toggle fullscreen state.
    void toggleFullscreen();

    /// Request window close.
    void close();

    /// Get current window dimensions.
    void getSize(int& width, int& height) const;

    /// Get the Wayland output scale factor.
    int scaleFactor() const;

    /// Whether the window has been configured and is ready to render.
    bool isReady() const;

    /// Whether the window close has been requested.
    bool isClosed() const;

    /// Set event callbacks.
    void setCallbacks(const WaylandCallbacks& callbacks);

    /// Set the IME cursor rectangle so the compositor positions
    /// the candidate window near the text cursor.
    void setIMECursorRect(int x, int y, int width, int height);

    /// Access the raw Wayland state (for clipboard, etc.).
    const WaylandState& state() const;
    WaylandState& state();

private:
    // Wayland listener setup
    void setupRegistryListener();
    void setupSeatListeners();
    void setupKeyboardListeners();
    void setupPointerListeners();
    void setupXdgListeners();

    // xkbcommon key translation
    KeyEvent translateKey(uint32_t key, uint32_t state_val);
    uint8_t translateModifiers(uint32_t mods_depressed, uint32_t mods_latched,
                               uint32_t mods_locked, uint32_t group);

    struct ListenerData;
    std::unique_ptr<ListenerData> listener_data_;

    WaylandState state_;
    WaylandCallbacks callbacks_;
    uint8_t current_modifiers_ = 0;
};

} // namespace termcore

#endif // TERMCORE_WAYLAND_WINDOW_H
