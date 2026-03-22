#ifndef TERMCORE_WAYLAND_CLIPBOARD_H
#define TERMCORE_WAYLAND_CLIPBOARD_H

#include <string>

// Forward declarations
struct wl_display;
struct wl_data_device_manager;
struct wl_data_device;
struct wl_data_source;
struct wl_data_offer;
struct wl_seat;

namespace termcore {

struct WaylandState;

/// Clipboard support for Wayland using wl_data_device_manager.
///
/// Uses wl_data_source for copy (offering text/plain;charset=utf-8)
/// and wl_data_offer for paste (receiving via pipe fd).
class WaylandClipboard {
public:
    WaylandClipboard();
    ~WaylandClipboard();

    WaylandClipboard(const WaylandClipboard&) = delete;
    WaylandClipboard& operator=(const WaylandClipboard&) = delete;

    /// Initialize clipboard with Wayland state.
    /// The WaylandState must outlive this object.
    void init(WaylandState* state);

    /// Copy text to clipboard.
    /// Creates a wl_data_source offering "text/plain;charset=utf-8".
    void copy(const std::string& text);

    /// Paste text from clipboard.
    /// Reads from the current wl_data_offer via pipe.
    /// Returns empty string if no text is available.
    std::string paste();

    /// Called when a new data offer is received (from wl_data_device listener).
    void setCurrentOffer(wl_data_offer* offer);

    /// Called when selection changes (from wl_data_device.selection event).
    void onSelectionOffer(wl_data_offer* offer);

private:
    WaylandState* state_ = nullptr;
    wl_data_source* current_source_ = nullptr;
    wl_data_offer* current_offer_ = nullptr;
    std::string copy_buffer_;  // Held alive while data source is active
};

} // namespace termcore

#endif // TERMCORE_WAYLAND_CLIPBOARD_H
