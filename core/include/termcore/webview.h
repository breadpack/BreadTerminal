#ifndef TERMCORE_WEBVIEW_H
#define TERMCORE_WEBVIEW_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace termcore {

/// WebView navigation events
enum class WebViewEvent : uint8_t {
    NavigationStarted,
    NavigationCompleted,
    NavigationFailed,
    TitleChanged,
    ContentLoaded,
};

using WebViewEventCallback = std::function<void(WebViewEvent event, const std::string& data)>;

/// Abstract WebView interface
class IWebView {
public:
    virtual ~IWebView() = default;

    /// Navigate to a URL
    virtual void navigate(const std::string& url) = 0;

    /// Navigate back/forward
    virtual void goBack() = 0;
    virtual void goForward() = 0;

    /// Reload current page
    virtual void reload() = 0;

    /// Stop loading
    virtual void stop() = 0;

    /// Get current URL
    virtual std::string currentUrl() const = 0;

    /// Get current page title
    virtual std::string title() const = 0;

    /// Check if can go back/forward
    virtual bool canGoBack() const = 0;
    virtual bool canGoForward() const = 0;

    /// Check if currently loading
    virtual bool isLoading() const = 0;

    /// Execute JavaScript (async, result via callback)
    virtual void executeJavaScript(const std::string& script,
                                    std::function<void(const std::string& result)> callback = {}) = 0;

    /// Set event callback
    virtual void setEventCallback(WebViewEventCallback cb) = 0;

    /// Resize the webview
    virtual void resize(int width, int height) = 0;
};

/// Factory function -- creates platform-specific WebView
/// Returns nullptr on unsupported platforms
std::unique_ptr<IWebView> createWebView();

} // namespace termcore
#endif
