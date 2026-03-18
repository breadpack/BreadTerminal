#import <WebKit/WebKit.h>
#include "WKWebViewBridge.h"

// Navigation delegate that forwards events to the C++ callback
@interface BreadWebViewDelegate : NSObject <WKNavigationDelegate>
@property (nonatomic, assign) termcore::WebViewEventCallback* callback;
@end

@implementation BreadWebViewDelegate

- (void)webView:(WKWebView*)webView
    didStartProvisionalNavigation:(WKNavigation*)navigation {
    if (_callback && *_callback) {
        NSString* url = webView.URL.absoluteString ?: @"";
        (*_callback)(termcore::WebViewEvent::NavigationStarted, url.UTF8String);
    }
}

- (void)webView:(WKWebView*)webView
    didFinishNavigation:(WKNavigation*)navigation {
    if (_callback && *_callback) {
        NSString* url = webView.URL.absoluteString ?: @"";
        (*_callback)(termcore::WebViewEvent::NavigationCompleted, url.UTF8String);
    }
}

- (void)webView:(WKWebView*)webView
    didFailNavigation:(WKNavigation*)navigation
            withError:(NSError*)error {
    if (_callback && *_callback) {
        NSString* desc = error.localizedDescription ?: @"Unknown error";
        (*_callback)(termcore::WebViewEvent::NavigationFailed, desc.UTF8String);
    }
}

- (void)webView:(WKWebView*)webView
    didFailProvisionalNavigation:(WKNavigation*)navigation
                       withError:(NSError*)error {
    if (_callback && *_callback) {
        NSString* desc = error.localizedDescription ?: @"Unknown error";
        (*_callback)(termcore::WebViewEvent::NavigationFailed, desc.UTF8String);
    }
}

@end

namespace termcore {

struct WKWebViewImpl::Impl {
    WKWebView* webView = nil;
    BreadWebViewDelegate* delegate = nil;
    WebViewEventCallback callback;
    id titleObserver = nil;

    Impl() {
        WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
        webView = [[WKWebView alloc] initWithFrame:NSZeroRect configuration:config];

        delegate = [[BreadWebViewDelegate alloc] init];
        delegate.callback = &callback;
        webView.navigationDelegate = delegate;
    }

    ~Impl() {
        if (titleObserver) {
            [webView removeObserver:titleObserver forKeyPath:@"title"];
        }
        webView.navigationDelegate = nil;
        webView = nil;
        delegate = nil;
    }
};

WKWebViewImpl::WKWebViewImpl() : impl_(std::make_unique<Impl>()) {}

WKWebViewImpl::~WKWebViewImpl() = default;

void WKWebViewImpl::navigate(const std::string& url) {
    NSString* urlStr = [NSString stringWithUTF8String:url.c_str()];
    NSURL* nsUrl = [NSURL URLWithString:urlStr];
    if (nsUrl) {
        [impl_->webView loadRequest:[NSURLRequest requestWithURL:nsUrl]];
    }
}

void WKWebViewImpl::goBack() {
    [impl_->webView goBack];
}

void WKWebViewImpl::goForward() {
    [impl_->webView goForward];
}

void WKWebViewImpl::reload() {
    [impl_->webView reload];
}

void WKWebViewImpl::stop() {
    [impl_->webView stopLoading];
}

std::string WKWebViewImpl::currentUrl() const {
    NSString* url = impl_->webView.URL.absoluteString;
    return url ? std::string(url.UTF8String) : std::string();
}

std::string WKWebViewImpl::title() const {
    NSString* t = impl_->webView.title;
    return t ? std::string(t.UTF8String) : std::string();
}

bool WKWebViewImpl::canGoBack() const {
    return impl_->webView.canGoBack;
}

bool WKWebViewImpl::canGoForward() const {
    return impl_->webView.canGoForward;
}

bool WKWebViewImpl::isLoading() const {
    return impl_->webView.isLoading;
}

void WKWebViewImpl::executeJavaScript(
    const std::string& script,
    std::function<void(const std::string&)> callback) {
    NSString* js = [NSString stringWithUTF8String:script.c_str()];
    [impl_->webView evaluateJavaScript:js
                     completionHandler:^(id result, NSError* error) {
        if (callback) {
            std::string res;
            if (result) {
                res = [NSString stringWithFormat:@"%@", result].UTF8String;
            }
            callback(res);
        }
    }];
}

void WKWebViewImpl::setEventCallback(WebViewEventCallback cb) {
    impl_->callback = std::move(cb);
}

void WKWebViewImpl::resize(int width, int height) {
    impl_->webView.frame = NSMakeRect(0, 0, width, height);
}

WKWebView* WKWebViewImpl::nativeView() const {
    return impl_->webView;
}

std::unique_ptr<IWebView> createWebView() {
    return std::make_unique<WKWebViewImpl>();
}

} // namespace termcore
