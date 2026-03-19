#if defined(_WIN32)

#include "WebView2Host.h"

#include <nlohmann/json.hpp>
#include <shlwapi.h>
#include <wrl/event.h>

#include <condition_variable>
#include <mutex>

using Microsoft::WRL::Callback;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

WebView2Host::~WebView2Host() {
    cleanup();
}

void WebView2Host::initialize(HWND parentHwnd, std::function<void()> onReady) {
    parentHwnd_ = parentHwnd;
    onReady_ = std::move(onReady);

    // Create the WebView2 environment (uses the installed Edge runtime).
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, // browserExecutableFolder  – use default Edge
        nullptr, // userDataFolder           – use default
        nullptr, // environmentOptions       – defaults
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) return result;
                environment_ = env;

                // Create the controller (which owns the visual).
                env->CreateCoreWebView2Controller(
                    parentHwnd_,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this](HRESULT res, ICoreWebView2Controller* ctrl) -> HRESULT {
                            if (FAILED(res) || !ctrl) return res;
                            controller_ = ctrl;
                            controller_->get_CoreWebView2(&webview_);

                            // Start hidden until show() is called.
                            controller_->put_IsVisible(FALSE);
                            visible_ = false;

                            // Apply initial bounds (may be zero rect).
                            applyBounds();

                            ready_ = true;
                            if (onReady_) onReady_();
                            return S_OK;
                        })
                        .Get());
                return S_OK;
            })
            .Get());

    if (FAILED(hr)) {
        // Edge WebView2 runtime not installed or other failure.
        ready_ = false;
    }
}

void WebView2Host::cleanup() {
    if (controller_) {
        controller_->Close();
        controller_.Reset();
    }
    webview_.Reset();
    environment_.Reset();
    ready_ = false;
    visible_ = false;
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void WebView2Host::navigate(const std::string& url) {
    if (!ready_ || !webview_) return;

    // Convert UTF-8 to wide string.
    int wlen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(),
                                    static_cast<int>(url.size()), nullptr, 0);
    std::wstring wurl(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, url.c_str(),
                        static_cast<int>(url.size()), wurl.data(), wlen);

    webview_->Navigate(wurl.c_str());
}

// ---------------------------------------------------------------------------
// JavaScript execution
// ---------------------------------------------------------------------------

void WebView2Host::executeJS(const std::string& script,
                              std::function<void(const std::string&)> callback) {
    if (!ready_ || !webview_) {
        if (callback) callback("null");
        return;
    }

    int wlen = MultiByteToWideChar(CP_UTF8, 0, script.c_str(),
                                    static_cast<int>(script.size()), nullptr, 0);
    std::wstring wscript(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, script.c_str(),
                        static_cast<int>(script.size()), wscript.data(), wlen);

    webview_->ExecuteScript(
        wscript.c_str(),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [callback](HRESULT hr, LPCWSTR resultJson) -> HRESULT {
                if (!callback) return S_OK;
                if (FAILED(hr) || !resultJson) {
                    callback("null");
                    return S_OK;
                }
                // Convert wide result to UTF-8.
                int len = WideCharToMultiByte(CP_UTF8, 0, resultJson, -1,
                                              nullptr, 0, nullptr, nullptr);
                std::string utf8(static_cast<size_t>(len - 1), '\0');
                WideCharToMultiByte(CP_UTF8, 0, resultJson, -1,
                                    utf8.data(), len, nullptr, nullptr);
                callback(utf8);
                return S_OK;
            })
            .Get());
}

// ---------------------------------------------------------------------------
// Accessibility snapshot
// ---------------------------------------------------------------------------

std::string WebView2Host::getAccessibilitySnapshot() {
    if (!ready_ || !webview_) {
        return R"({"error":"webview not ready"})";
    }

    // JavaScript that builds a lightweight accessibility tree.
    static const std::string kSnapshotJS = R"JS(
(function() {
    function walk(el) {
        var node = {};
        node.tag = el.tagName ? el.tagName.toLowerCase() : '';
        if (el.getAttribute) {
            var role = el.getAttribute('role');
            if (role) node.role = role;
            var ariaLabel = el.getAttribute('aria-label');
            if (ariaLabel) node.name = ariaLabel;
            var alt = el.getAttribute('alt');
            if (alt) node.alt = alt;
            var href = el.getAttribute('href');
            if (href) node.href = href;
            var type = el.getAttribute('type');
            if (type) node.type = type;
            var id = el.id;
            if (id) node.id = id;
            var value = el.value;
            if (value !== undefined && value !== '') node.value = String(value);
        }
        var text = '';
        for (var i = 0; i < el.childNodes.length; ++i) {
            if (el.childNodes[i].nodeType === 3) text += el.childNodes[i].textContent;
        }
        text = text.trim();
        if (text) node.text = text.substring(0, 200);
        var kids = [];
        var children = el.children || [];
        for (var j = 0; j < children.length; ++j) {
            kids.push(walk(children[j]));
        }
        if (kids.length) node.children = kids;
        return node;
    }
    return JSON.stringify({
        title: document.title,
        url: location.href,
        tree: document.body ? walk(document.body) : {}
    });
})()
)JS";

    // Block until the async JS call completes. This is acceptable because
    // it is called from the UI thread which also pumps messages, so we
    // pump messages while waiting.
    std::string result;
    bool done = false;

    executeJS(kSnapshotJS, [&](const std::string& r) {
        result = r;
        done = true;
    });

    // Pump the Win32 message loop until the callback fires (with timeout).
    MSG msg;
    auto deadline = GetTickCount64() + 5000; // 5-second timeout
    while (!done && GetTickCount64() < deadline) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (!done) {
        return R"({"error":"snapshot timed out"})";
    }

    // ExecuteScript returns a JSON-encoded string (i.e. the result is
    // itself a JSON string with escaped quotes). Unwrap one level.
    // If result starts with '"', it is a JSON string that we should parse.
    if (!result.empty() && result.front() == '"') {
        try {
            auto parsed = nlohmann::json::parse(result);
            if (parsed.is_string()) {
                return parsed.get<std::string>();
            }
        } catch (...) {
            // fall through — return raw
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Visibility & bounds
// ---------------------------------------------------------------------------

void WebView2Host::show(int x, int y, int width, int height) {
    bounds_ = {x, y, x + width, y + height};
    if (controller_) {
        applyBounds();
        controller_->put_IsVisible(TRUE);
    }
    visible_ = true;
}

void WebView2Host::hide() {
    if (controller_) {
        controller_->put_IsVisible(FALSE);
    }
    visible_ = false;
}

void WebView2Host::resize(int x, int y, int width, int height) {
    bounds_ = {x, y, x + width, y + height};
    if (controller_) {
        applyBounds();
    }
}

bool WebView2Host::isVisible() const {
    return visible_.load();
}

void WebView2Host::applyBounds() {
    if (!controller_) return;
    controller_->put_Bounds(bounds_);
}

#endif // _WIN32
