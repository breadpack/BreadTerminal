#ifndef TERMCORE_PLATFORM_HOST_H
#define TERMCORE_PLATFORM_HOST_H

#include "termcore/config.h"
#include "termcore/keybinding.h"  // KeyMod (ModNone, ModShift, etc.)
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace termcore {

class Pty;

struct KeyEvent {
    uint32_t keycode = 0;
    uint8_t modifiers = ModNone;
    std::string text;         // UTF-8 text input
    bool isRepeat = false;
};

struct InputMouseEvent {
    enum Type { Press, Release, Move, DoubleClick, ScrollUp, ScrollDown };
    Type type = Press;
    int x = 0, y = 0;        // pixel coordinates
    uint8_t modifiers = ModNone;
    int button = 0;           // 0=left, 1=middle, 2=right
    int scrollLines = 0;
};

class IPlatformHost {
public:
    virtual ~IPlatformHost() = default;

    // Rendering
    virtual void invalidate() = 0;
    virtual void getViewportSize(int& w, int& h) = 0;

    // Clipboard
    virtual std::string getClipboardText() = 0;
    virtual void setClipboardText(const std::string& text) = 0;

    // Window
    virtual void setWindowTitle(const std::string& title) = 0;
    virtual void toggleFullscreen() = 0;
    virtual void closeWindow() = 0;
    virtual void showConfirmDialog(const std::string& msg,
                                   std::function<void(bool)> cb) = 0;

    // Search UI
    virtual void showSearchBar() = 0;
    virtual void hideSearchBar() = 0;
    virtual void updateSearchResults(int current, int total) = 0;

    // IME
    virtual void positionIME(int x, int y, int height) = 0;

    // Font/color update notifications
    virtual void onFontChanged(float cellW, float cellH) = 0;
    virtual void onColorsChanged() = 0;
    virtual void onGridSizeChanged(int rows, int cols) = 0;

    // Notifications
    virtual void showNotification(const std::string& title,
                                  const std::string& body) = 0;

    // Settings/Hub windows
    virtual void openSettingsWindow(const Config& config) = 0;
    virtual void openThemeHub(const Config& config) = 0;
    virtual void openFontHub(const Config& config) = 0;

    // DPI
    virtual float dpiScale() = 0;

    // URL opening (platform-specific: ShellExecute on Windows, NSWorkspace on macOS, xdg-open on Linux)
    virtual void openUrl(const std::string& url) = 0;

    // Mouse cursor style (e.g., switch to pointer hand when hovering a URL)
    enum class CursorType { Arrow, Hand };
    virtual void setMouseCursor(CursorType cursor) { (void)cursor; }

    // PTY factory - platform creates PTY since it's OS-specific
    virtual std::unique_ptr<Pty> createPty(const std::string& shell,
                                           int rows, int cols) = 0;
};

} // namespace termcore
#endif
