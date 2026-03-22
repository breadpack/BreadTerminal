#ifndef WEBVIEW2_HOST_H
#define WEBVIEW2_HOST_H

#include "termcore/webview.h"

namespace termcore {

/// Windows WebView2-based implementation of IWebView.
/// Requires the Microsoft Edge WebView2 runtime to be installed.
class WebView2Host : public IWebView {
public:
    WebView2Host();
    ~WebView2Host() override;

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

    /// Returns true if the WebView2 environment was successfully created
    bool isReady() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termcore

#endif // WEBVIEW2_HOST_H
