#ifndef WEBKIT_WEBVIEW_BRIDGE_H
#define WEBKIT_WEBVIEW_BRIDGE_H

#include "termcore/webview.h"

namespace termcore {

/// Linux WebKitGTK-based implementation of IWebView.
/// Requires webkit2gtk-4.1 (or compatible) to be installed.
class WebKitWebViewBridge : public IWebView {
public:
    WebKitWebViewBridge();
    ~WebKitWebViewBridge() override;

    void navigate(const std::string& url) override;
    void goBack() override;
    void goForward() override;
    void reload() override;
    void stop() override;

    std::string currentUrl() const override;
    std::string title() const override;

    bool canGoBack() const override;
    bool canGoForward() const override;
    bool isLoading() const override;

    void executeJavaScript(const std::string& script,
                            std::function<void(const std::string& result)> callback) override;

    void setEventCallback(WebViewEventCallback cb) override;
    void resize(int width, int height) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termcore

#endif // WEBKIT_WEBVIEW_BRIDGE_H
