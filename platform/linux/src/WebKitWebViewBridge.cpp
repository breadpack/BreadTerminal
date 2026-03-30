#if !defined(_WIN32) && !defined(__APPLE__)

#if TERMCORE_HAS_WEBKIT

#include "WebKitWebViewBridge.h"

#include <gtk/gtk.h>
#include <webkit/webkit.h>

#include <string>

namespace termcore {

// --- Impl (pimpl) holding the GTK/WebKit widget pointers --------------------

struct WebKitWebViewBridge::Impl {
    GtkWidget* container = nullptr;   // GtkScrolledWindow or top-level container
    WebKitWebView* webView = nullptr;
    WebViewEventCallback eventCallback;
    int width = 800;
    int height = 600;

    Impl() {
        container = gtk_scrolled_window_new();
        webView = WEBKIT_WEB_VIEW(webkit_web_view_new());
        gtk_scrolled_window_set_child(
            GTK_SCROLLED_WINDOW(container), GTK_WIDGET(webView));

        connectSignals();
    }

    ~Impl() {
        // GtkWidget pointers are ref-counted by the container hierarchy.
        // If no parent owns 'container', explicitly unref.
        if (container && !gtk_widget_get_parent(container)) {
            g_object_ref_sink(container);
            g_object_unref(container);
        }
    }

    void connectSignals();

    // Signal callbacks as static members to access private Impl
    static void onLoadChanged(WebKitWebView* wv, WebKitLoadEvent event,
                               gpointer userData) {
        auto* impl = static_cast<Impl*>(userData);
        if (!impl->eventCallback) return;

        const char* uri = webkit_web_view_get_uri(wv);
        std::string url = uri ? uri : "";

        switch (event) {
        case WEBKIT_LOAD_STARTED:
            impl->eventCallback(WebViewEvent::NavigationStarted, url);
            break;
        case WEBKIT_LOAD_COMMITTED:
            impl->eventCallback(WebViewEvent::ContentLoaded, url);
            break;
        case WEBKIT_LOAD_FINISHED:
            impl->eventCallback(WebViewEvent::NavigationCompleted, url);
            break;
        default:
            break;
        }
    }

    static gboolean onLoadFailed(WebKitWebView* /*wv*/, WebKitLoadEvent /*event*/,
                                  const gchar* /*failingUri*/, GError* error,
                                  gpointer userData) {
        auto* impl = static_cast<Impl*>(userData);
        if (impl->eventCallback) {
            std::string msg = error ? error->message : "Unknown error";
            impl->eventCallback(WebViewEvent::NavigationFailed, msg);
        }
        return FALSE; // allow default error handling
    }

    static void onTitleNotify(GObject* /*object*/, GParamSpec* /*pspec*/,
                               gpointer userData) {
        auto* impl = static_cast<Impl*>(userData);
        if (!impl->eventCallback) return;
        const char* t = webkit_web_view_get_title(impl->webView);
        impl->eventCallback(WebViewEvent::TitleChanged, t ? t : "");
    }
};

void WebKitWebViewBridge::Impl::connectSignals() {
    g_signal_connect(webView, "load-changed",
                     G_CALLBACK(onLoadChanged), this);
    g_signal_connect(webView, "load-failed",
                     G_CALLBACK(onLoadFailed), this);
    g_signal_connect(webView, "notify::title",
                     G_CALLBACK(onTitleNotify), this);
}

// --- WebKitWebViewBridge public API -----------------------------------------

WebKitWebViewBridge::WebKitWebViewBridge()
    : impl_(std::make_unique<Impl>()) {}

WebKitWebViewBridge::~WebKitWebViewBridge() = default;

void WebKitWebViewBridge::navigate(const std::string& url) {
    webkit_web_view_load_uri(impl_->webView, url.c_str());
}

void WebKitWebViewBridge::goBack() {
    webkit_web_view_go_back(impl_->webView);
}

void WebKitWebViewBridge::goForward() {
    webkit_web_view_go_forward(impl_->webView);
}

void WebKitWebViewBridge::reload() {
    webkit_web_view_reload(impl_->webView);
}

void WebKitWebViewBridge::stop() {
    webkit_web_view_stop_loading(impl_->webView);
}

std::string WebKitWebViewBridge::currentUrl() const {
    const char* uri = webkit_web_view_get_uri(impl_->webView);
    return uri ? std::string(uri) : std::string();
}

std::string WebKitWebViewBridge::title() const {
    const char* t = webkit_web_view_get_title(impl_->webView);
    return t ? std::string(t) : std::string();
}

bool WebKitWebViewBridge::canGoBack() const {
    return webkit_web_view_can_go_back(impl_->webView) != FALSE;
}

bool WebKitWebViewBridge::canGoForward() const {
    return webkit_web_view_can_go_forward(impl_->webView) != FALSE;
}

bool WebKitWebViewBridge::isLoading() const {
    return webkit_web_view_is_loading(impl_->webView) != FALSE;
}

// Callback trampoline for webkit_web_view_evaluate_javascript
static void jsFinished(GObject* source, GAsyncResult* result,
                       gpointer userData) {
    auto* cb = static_cast<std::function<void(const std::string&)>*>(userData);
    GError* error = nullptr;
    JSCValue* jsValue =
        webkit_web_view_evaluate_javascript_finish(
            WEBKIT_WEB_VIEW(source), result, &error);

    std::string value;
    if (jsValue) {
        if (jsc_value_is_string(jsValue)) {
            gchar* str = jsc_value_to_string(jsValue);
            if (str) {
                value = str;
                g_free(str);
            }
        } else {
            gchar* str = jsc_value_to_string(jsValue);
            if (str) {
                value = str;
                g_free(str);
            }
        }
        g_object_unref(jsValue);
    }
    if (error) g_error_free(error);

    if (*cb) {
        (*cb)(value);
    }
    delete cb;
}

void WebKitWebViewBridge::executeJavaScript(
    const std::string& script,
    std::function<void(const std::string&)> callback) {
    if (callback) {
        auto* cb = new std::function<void(const std::string&)>(
            std::move(callback));
        webkit_web_view_evaluate_javascript(
            impl_->webView, script.c_str(),
            static_cast<gssize>(script.size()),
            nullptr, nullptr, nullptr, jsFinished, cb);
    } else {
        webkit_web_view_evaluate_javascript(
            impl_->webView, script.c_str(),
            static_cast<gssize>(script.size()),
            nullptr, nullptr, nullptr, nullptr, nullptr);
    }
}

void WebKitWebViewBridge::setEventCallback(WebViewEventCallback cb) {
    impl_->eventCallback = std::move(cb);
}

void WebKitWebViewBridge::resize(int width, int height) {
    impl_->width = width;
    impl_->height = height;
    gtk_widget_set_size_request(GTK_WIDGET(impl_->container), width, height);
}

std::unique_ptr<IWebView> createWebView() {
    return std::make_unique<WebKitWebViewBridge>();
}

} // namespace termcore

#else // !TERMCORE_HAS_WEBKIT

#include "termcore/webview.h"

namespace termcore {

std::unique_ptr<IWebView> createWebView() {
    // WebKitGTK not available at build time
    return nullptr;
}

} // namespace termcore

#endif // TERMCORE_HAS_WEBKIT

#endif // !defined(_WIN32) && !defined(__APPLE__)
