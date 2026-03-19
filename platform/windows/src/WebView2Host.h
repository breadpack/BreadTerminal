#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <WebView2.h>
#include <wrl/client.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

using Microsoft::WRL::ComPtr;

/// Manages a WebView2 (Chromium-based) browser control embedded in a Win32 window.
/// All COM calls must happen on the thread that created the HWND (the UI thread).
class WebView2Host {
public:
    WebView2Host() = default;
    ~WebView2Host();

    WebView2Host(const WebView2Host&) = delete;
    WebView2Host& operator=(const WebView2Host&) = delete;

    /// Create the WebView2 environment and controller.
    /// Calls `onReady` (on the UI thread) once the control is usable.
    void initialize(HWND parentHwnd, std::function<void()> onReady = nullptr);

    /// Navigate to a URL (http://, https://, file://, etc.).
    void navigate(const std::string& url);

    /// Execute JavaScript in the top-level document.
    /// `callback` receives the JSON-encoded result string.
    void executeJS(const std::string& script,
                   std::function<void(const std::string&)> callback);

    /// Synchronously return an accessibility-style snapshot of the page as JSON.
    /// Internally executes JS to walk the DOM and blocks until the result arrives
    /// (with a short timeout). Call from the UI thread only.
    std::string getAccessibilitySnapshot();

    /// Show the browser pane at the given pixel rect inside the parent HWND.
    void show(int x, int y, int width, int height);

    /// Hide the browser pane (sets controller visibility to false).
    void hide();

    /// Resize/reposition the browser pane.
    void resize(int x, int y, int width, int height);

    /// Is the browser pane currently visible?
    bool isVisible() const;

    /// Tear down COM objects. Safe to call multiple times.
    void cleanup();

private:
    void applyBounds();

    HWND parentHwnd_ = nullptr;
    ComPtr<ICoreWebView2Environment> environment_;
    ComPtr<ICoreWebView2Controller> controller_;
    ComPtr<ICoreWebView2> webview_;

    std::atomic<bool> ready_{false};
    std::atomic<bool> visible_{false};

    // Current bounds in parent-window pixels
    RECT bounds_{0, 0, 0, 0};

    std::function<void()> onReady_;
};

#endif // _WIN32
