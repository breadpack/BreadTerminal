#ifndef BREAD_GTK_PLATFORM_HOST_H
#define BREAD_GTK_PLATFORM_HOST_H

#include "termcore/platform_host.h"
#include "termcore/config.h"
#include "UnifiedSettingsWindow.h"
#include <gtk/gtk.h>
#include <functional>
#include <memory>
#include <string>

namespace termcore {
class TerminalController;
}

/// IPlatformHost implementation for GTK4/Linux.
/// Bridges TerminalController with GTK4 widgets.
class GtkPlatformHost : public termcore::IPlatformHost {
public:
    explicit GtkPlatformHost(GtkWidget* glArea);

    // Set the parent window (resolved lazily from glArea)
    void setWindow(GtkWindow* window);

    // Set the controller reference (needed for search bar callbacks)
    void setController(termcore::TerminalController* controller);

    // --- IPlatformHost interface ---
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

    void showClipboardHistory(
        const std::vector<termcore::ClipboardEntry>& entries) override;

    void openSettingsWindow(const termcore::Config& config) override;

    float dpiScale() override;

    void openUrl(const std::string& url) override;
    void setMouseCursor(CursorType cursor) override;

    std::unique_ptr<termcore::Pty> createPty(const termcore::Profile& profile,
                                              int rows, int cols) override;

private:
    GtkWindow* resolveWindow();
    void buildSearchBar();
    void destroySearchBar();

    GtkWidget* glArea_ = nullptr;
    GtkWindow* window_ = nullptr;
    termcore::TerminalController* controller_ = nullptr;
    bool isFullscreen_ = false;

    // Synchronous clipboard text (filled by async read, used by getClipboardText)
    std::string pendingClipboardText_;

    // Search bar widgets
    GtkWidget* searchOverlay_ = nullptr;   // GtkBox container
    GtkWidget* searchEntry_ = nullptr;     // GtkEntry for search text
    GtkWidget* searchLabel_ = nullptr;     // GtkLabel for "N of M"
    GtkEventController* searchKeyCtrl_ = nullptr;
    bool searchBarVisible_ = false;
    bool updatingSearchText_ = false;      // guard against re-entrant signal

    // Unified settings window
    std::unique_ptr<termcore::UnifiedSettingsWindow> settingsWindow_;
};

#endif // BREAD_GTK_PLATFORM_HOST_H
