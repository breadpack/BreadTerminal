#ifndef TERMCORE_WAYLAND_PLATFORM_HOST_H
#define TERMCORE_WAYLAND_PLATFORM_HOST_H

#include "termcore/platform_host.h"
#include "termcore/config.h"
#include "WaylandWindow.h"
#include "WaylandClipboard.h"
#include <memory>
#include <string>

namespace termcore {

/// IPlatformHost implementation for native Wayland.
///
/// Bridges the TerminalController with a WaylandWindow (for surface/EGL
/// management and input events) and WaylandClipboard.  Reuses the
/// existing GLTextRenderer for OpenGL rendering via EGL.
class WaylandPlatformHost : public IPlatformHost {
public:
    WaylandPlatformHost();
    ~WaylandPlatformHost() override;

    WaylandPlatformHost(const WaylandPlatformHost&) = delete;
    WaylandPlatformHost& operator=(const WaylandPlatformHost&) = delete;

    /// Initialize Wayland display, surface, and EGL context.
    /// Returns false if Wayland is not available.
    bool init();

    /// Access the underlying window (e.g., for the main loop).
    WaylandWindow& window();
    const WaylandWindow& window() const;

    // ─── IPlatformHost interface ────────────────────────────────

    void invalidate() override;
    void getViewportSize(int& w, int& h) override;

    std::string getClipboardText() override;
    void setClipboardText(const std::string& text) override;

    void setWindowTitle(const std::string& title) override;
    void toggleFullscreen() override;
    void closeWindow() override;
    void showConfirmDialog(const std::string& msg,
                           std::function<void(bool)> cb) override;

    void showSearchBar() override;
    void hideSearchBar() override;
    void updateSearchResults(int current, int total) override;
    void setSearchBarText(const std::string& text) override;

    void positionIME(int x, int y, int height) override;

    void onFontChanged(float cellW, float cellH) override;
    void onColorsChanged() override;
    void onGridSizeChanged(int rows, int cols) override;

    void showNotification(const std::string& title,
                          const std::string& body) override;

    void openSettingsWindow(const Config& config) override;

    float dpiScale() override;

    std::unique_ptr<Pty> createPty(const Profile& profile,
                                   int rows, int cols) override;

private:
    WaylandWindow window_;
    WaylandClipboard clipboard_;
    bool needsRedraw_ = false;
};

} // namespace termcore

#endif // TERMCORE_WAYLAND_PLATFORM_HOST_H
