#if defined(_WIN32)

#if HAS_WEBVIEW2

#include "WebView2Host.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>

#include <atomic>
#include <mutex>
#include <string>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace termcore {

// Convert UTF-8 std::string to wide string
static std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                  static_cast<int>(s.size()), nullptr, 0);
    std::wstring ws(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(),
                        static_cast<int>(s.size()), ws.data(), len);
    return ws;
}

// Convert wide string to UTF-8 std::string
static std::string toUtf8(const wchar_t* ws) {
    if (!ws || !*ws) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws, -1,
                                  nullptr, 0, nullptr, nullptr);
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, s.data(), len, nullptr, nullptr);
    return s;
}

struct WebView2Host::Impl {
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;

    WebViewEventCallback eventCallback;
    std::atomic<bool> ready{false};
    std::atomic<bool> loading{false};

    std::string currentUrlStr;
    std::string titleStr;
    mutable std::mutex stateMutex;

    HWND hostWindow = nullptr;

    Impl() {
        // Create a hidden host window for the WebView2 controller
        hostWindow = CreateWindowExW(
            0, L"STATIC", L"WebView2Host",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

        initEnvironment();
    }

    ~Impl() {
        if (controller) {
            controller->Close();
        }
        if (hostWindow) {
            DestroyWindow(hostWindow);
        }
    }

    void initEnvironment() {
        HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, nullptr, nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this](HRESULT result,
                       ICoreWebView2Environment* env) -> HRESULT {
                    if (FAILED(result) || !env) return result;
                    environment = env;
                    initController();
                    return S_OK;
                })
                .Get());

        if (FAILED(hr)) {
            // WebView2 runtime not available -- stays as not-ready
            return;
        }
    }

    void initController() {
        if (!environment || !hostWindow) return;

        environment->CreateCoreWebView2Controller(
            hostWindow,
            Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [this](HRESULT result,
                       ICoreWebView2Controller* ctrl) -> HRESULT {
                    if (FAILED(result) || !ctrl) return result;
                    controller = ctrl;
                    controller->get_CoreWebView2(&webview);
                    if (!webview) return E_FAIL;
                    registerEventHandlers();
                    ready.store(true);
                    return S_OK;
                })
                .Get());
    }

    void registerEventHandlers();
};

// Event handler registration split out for readability
void WebView2Host::Impl::registerEventHandlers() {
    if (!webview) return;
    EventRegistrationToken token;

    // Navigation starting
    webview->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this](ICoreWebView2* /*sender*/,
                   ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                loading.store(true);
                wchar_t* uri = nullptr;
                args->get_Uri(&uri);
                std::string url = uri ? toUtf8(uri) : "";
                if (uri) CoTaskMemFree(uri);

                {
                    std::lock_guard lock(stateMutex);
                    currentUrlStr = url;
                }
                if (eventCallback) {
                    eventCallback(WebViewEvent::NavigationStarted, url);
                }
                return S_OK;
            })
            .Get(),
        &token);

    // Navigation completed
    webview->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this](ICoreWebView2* sender,
                   ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                loading.store(false);

                BOOL success = FALSE;
                args->get_IsSuccess(&success);

                wchar_t* uri = nullptr;
                sender->get_Source(&uri);
                std::string url = uri ? toUtf8(uri) : "";
                if (uri) CoTaskMemFree(uri);

                {
                    std::lock_guard lock(stateMutex);
                    currentUrlStr = url;
                }

                auto event = success ? WebViewEvent::NavigationCompleted
                                     : WebViewEvent::NavigationFailed;
                if (eventCallback) {
                    eventCallback(event, url);
                }
                return S_OK;
            })
            .Get(),
        &token);

    // Document title changed
    webview->add_DocumentTitleChanged(
        Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
            [this](ICoreWebView2* sender, IUnknown* /*args*/) -> HRESULT {
                wchar_t* t = nullptr;
                sender->get_DocumentTitle(&t);
                std::string newTitle = t ? toUtf8(t) : "";
                if (t) CoTaskMemFree(t);

                {
                    std::lock_guard lock(stateMutex);
                    titleStr = newTitle;
                }
                if (eventCallback) {
                    eventCallback(WebViewEvent::TitleChanged, newTitle);
                }
                return S_OK;
            })
            .Get(),
        &token);

    // Content loaded (DOM content ready)
    webview->add_ContentLoading(
        Callback<ICoreWebView2ContentLoadingEventHandler>(
            [this](ICoreWebView2* /*sender*/,
                   ICoreWebView2ContentLoadingEventArgs* /*args*/) -> HRESULT {
                if (eventCallback) {
                    std::lock_guard lock(stateMutex);
                    eventCallback(WebViewEvent::ContentLoaded, currentUrlStr);
                }
                return S_OK;
            })
            .Get(),
        &token);
}

// --- WebView2Host public API ------------------------------------------------

WebView2Host::WebView2Host() : impl_(std::make_unique<Impl>()) {}
WebView2Host::~WebView2Host() = default;

void WebView2Host::navigate(const std::string& url) {
    if (!impl_->webview) return;
    impl_->webview->Navigate(toWide(url).c_str());
}

void WebView2Host::goBack() {
    if (impl_->webview) impl_->webview->GoBack();
}

void WebView2Host::goForward() {
    if (impl_->webview) impl_->webview->GoForward();
}

void WebView2Host::reload() {
    if (impl_->webview) impl_->webview->Reload();
}

void WebView2Host::stop() {
    if (impl_->webview) impl_->webview->Stop();
}

std::string WebView2Host::currentUrl() const {
    std::lock_guard lock(impl_->stateMutex);
    return impl_->currentUrlStr;
}

std::string WebView2Host::title() const {
    std::lock_guard lock(impl_->stateMutex);
    return impl_->titleStr;
}

bool WebView2Host::canGoBack() const {
    if (!impl_->webview) return false;
    BOOL val = FALSE;
    impl_->webview->get_CanGoBack(&val);
    return val != FALSE;
}

bool WebView2Host::canGoForward() const {
    if (!impl_->webview) return false;
    BOOL val = FALSE;
    impl_->webview->get_CanGoForward(&val);
    return val != FALSE;
}

bool WebView2Host::isLoading() const {
    return impl_->loading.load();
}

void WebView2Host::executeJavaScript(
    const std::string& script,
    std::function<void(const std::string&)> callback) {
    if (!impl_->webview) {
        if (callback) callback("");
        return;
    }

    impl_->webview->ExecuteScript(
        toWide(script).c_str(),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [cb = std::move(callback)](HRESULT hr,
                                       LPCWSTR result) -> HRESULT {
                if (cb) {
                    std::string res =
                        (SUCCEEDED(hr) && result) ? toUtf8(result) : "";
                    cb(res);
                }
                return S_OK;
            })
            .Get());
}

void WebView2Host::setEventCallback(WebViewEventCallback cb) {
    impl_->eventCallback = std::move(cb);
}

void WebView2Host::resize(int width, int height) {
    if (!impl_->controller) return;
    RECT bounds = {0, 0, width, height};
    impl_->controller->put_Bounds(bounds);
}

bool WebView2Host::isReady() const {
    return impl_->ready.load();
}

std::unique_ptr<IWebView> createWebView() {
    return std::make_unique<WebView2Host>();
}

} // namespace termcore

#else // !HAS_WEBVIEW2

#include "termcore/webview.h"

namespace termcore {

std::unique_ptr<IWebView> createWebView() {
    // WebView2 SDK not available at build time
    return nullptr;
}

} // namespace termcore

#endif // HAS_WEBVIEW2

#endif // defined(_WIN32)
