#ifndef BREADTERMINAL_MAC_PLATFORM_HOST_H
#define BREADTERMINAL_MAC_PLATFORM_HOST_H

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "termcore/platform_host.h"
#include "termcore/pty.h"

#include <memory>
#include <string>

@class TerminalView;

namespace termcore {
class MetalTextRenderer;
}

/// macOS implementation of IPlatformHost.
/// Bridges TerminalController callbacks to Cocoa/AppKit APIs.
class MacPlatformHost : public termcore::IPlatformHost {
public:
    MacPlatformHost(TerminalView* view, NSWindow* window);

    // --- Rendering ---
    void invalidate() override;
    void getViewportSize(int& w, int& h) override;

    // --- Clipboard ---
    std::string getClipboardText() override;
    void setClipboardText(const std::string& text) override;

    // --- Window ---
    void setWindowTitle(const std::string& title) override;
    void toggleFullscreen() override;
    void closeWindow() override;
    void showConfirmDialog(const std::string& msg,
                           std::function<void(bool)> cb) override;

    // --- Search UI ---
    void showSearchBar() override;
    void hideSearchBar() override;
    void updateSearchResults(int current, int total) override;

    // --- IME ---
    void positionIME(int x, int y, int height) override;

    // --- Font/color update notifications ---
    void onFontChanged(float cellW, float cellH) override;
    void onColorsChanged() override;
    void onGridSizeChanged(int rows, int cols) override;

    // --- Notifications ---
    void showNotification(const std::string& title,
                          const std::string& body) override;

    // --- Settings/Hub windows ---
    void openSettingsWindow(const termcore::Config& config) override;
    void openThemeHub(const termcore::Config& config) override;
    void openFontHub(const termcore::Config& config) override;

    // --- DPI ---
    float dpiScale() override;

    // --- URL opening ---
    void openUrl(const std::string& url) override;
    void setMouseCursor(CursorType cursor) override;

    // --- PTY factory ---
    std::unique_ptr<termcore::Pty> createPty(const std::string& shell,
                                              int rows, int cols) override;

    // --- Accessors for updating references ---
    void setWindow(NSWindow* window) { window_ = window; }

private:
    __weak TerminalView* view_;
    __weak NSWindow* window_;
};

#endif // BREADTERMINAL_MAC_PLATFORM_HOST_H
