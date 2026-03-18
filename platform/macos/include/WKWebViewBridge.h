#ifndef WKWEBVIEW_BRIDGE_H
#define WKWEBVIEW_BRIDGE_H

#include "termcore/webview.h"

#ifdef __OBJC__
#import <WebKit/WebKit.h>
#endif

namespace termcore {

/// macOS WKWebView-based implementation of IWebView
class WKWebViewImpl : public IWebView {
public:
    WKWebViewImpl();
    ~WKWebViewImpl() override;

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

#ifdef __OBJC__
    /// Access the underlying WKWebView for embedding in an NSView hierarchy
    WKWebView* nativeView() const;
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termcore

#endif // WKWEBVIEW_BRIDGE_H
