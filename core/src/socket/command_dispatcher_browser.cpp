#include "termcore/socket/command_dispatcher.h"

namespace termcore {

// ---------------------------------------------------------------------------
// browser.navigate — navigate to a URL or perform navigation action
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleBrowserNavigate(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!webview_cb_) {
        return rpc::makeError(id, rpc::kNotFound, "No active WebView");
    }
    if (p.contains("url") && p["url"].is_string()) {
        webview_cb_("navigate", {{"url", p["url"].get<std::string>()}});
    } else if (p.contains("action") && p["action"].is_string()) {
        auto action = p["action"].get<std::string>();
        if (action == "back" || action == "forward" ||
            action == "reload" || action == "stop") {
            webview_cb_(action, {});
        } else {
            return rpc::makeError(id, rpc::kInvalidParams,
                                  "Invalid action: " + action);
        }
    } else {
        return rpc::makeError(id, rpc::kInvalidParams, "url or action required");
    }
    return rpc::makeResult(id, {{"success", true}});
}

// ---------------------------------------------------------------------------
// browser.executeJS — execute JavaScript and return the result
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleBrowserExecuteJS(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!webview_cb_) {
        return rpc::makeError(id, rpc::kNotFound, "No active WebView");
    }
    if (!p.contains("script") || !p["script"].is_string()) {
        return rpc::makeError(id, rpc::kInvalidParams, "script required");
    }
    auto script = p["script"].get<std::string>();

    // The webview callback dispatches "executeJS" with a script parameter.
    // The host side is expected to execute the script and, if the request had
    // an id, send back the result through the response callback.
    // Because the actual execution is async (WebView2 COM), we issue the
    // command and rely on the host to fill in the result.
    webview_cb_("executeJS", {{"script", script}});

    // For now return acknowledgement. Full async result delivery requires
    // the host to post the result back via the socket (future enhancement).
    return rpc::makeResult(id, {{"success", true}, {"note", "result delivered asynchronously"}});
}

// ---------------------------------------------------------------------------
// browser.snapshot — get accessibility tree snapshot as JSON
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleBrowserSnapshot(
    std::optional<int64_t> id, const nlohmann::json& /*p*/) {
    if (!webview_cb_) {
        return rpc::makeError(id, rpc::kNotFound, "No active WebView");
    }
    // Ask the host for a snapshot. The callback "snapshot" triggers the host
    // to run the DOM-walking JS. A synchronous result is available when the
    // host implements getAccessibilitySnapshot() with message-pump blocking.
    //
    // We use a special synchronous callback path: the host stores the snapshot
    // result in a shared location and we read it here.
    // Because the WebViewCallback is fire-and-forget, we need a richer
    // mechanism. For now we signal the host and return a placeholder that the
    // host will replace with actual data if it intercepts "snapshot".
    webview_cb_("snapshot", {});
    return rpc::makeResult(id, {{"success", true},
                                 {"note", "snapshot requested; host will deliver result"}});
}

// ---------------------------------------------------------------------------
// browser.show — show the browser panel at given bounds
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleBrowserShow(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!webview_cb_) {
        return rpc::makeError(id, rpc::kNotFound, "No active WebView");
    }
    int x      = p.value("x", 0);
    int y      = p.value("y", 0);
    int width  = p.value("width", 800);
    int height = p.value("height", 600);
    webview_cb_("show", {{"x", x}, {"y", y}, {"width", width}, {"height", height}});
    return rpc::makeResult(id, {{"success", true}});
}

// ---------------------------------------------------------------------------
// browser.hide — hide the browser panel
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleBrowserHide(
    std::optional<int64_t> id, const nlohmann::json& /*p*/) {
    if (!webview_cb_) {
        return rpc::makeError(id, rpc::kNotFound, "No active WebView");
    }
    webview_cb_("hide", {});
    return rpc::makeResult(id, {{"success", true}});
}

// ---------------------------------------------------------------------------
// browser.click — click an element by CSS selector
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleBrowserClick(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!webview_cb_) {
        return rpc::makeError(id, rpc::kNotFound, "No active WebView");
    }
    if (!p.contains("selector") || !p["selector"].is_string()) {
        return rpc::makeError(id, rpc::kInvalidParams, "selector required");
    }
    auto selector = p["selector"].get<std::string>();

    // Build JS that clicks the element. Escape single quotes in the selector.
    std::string escaped;
    for (char c : selector) {
        if (c == '\'') escaped += "\\'";
        else escaped += c;
    }
    std::string script =
        "(() => { var el = document.querySelector('" + escaped +
        "'); if (el) { el.click(); return 'clicked'; } return 'not_found'; })()";

    webview_cb_("executeJS", {{"script", script}});
    return rpc::makeResult(id, {{"success", true}});
}

// ---------------------------------------------------------------------------
// browser.fill — fill a form field by CSS selector
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleBrowserFill(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!webview_cb_) {
        return rpc::makeError(id, rpc::kNotFound, "No active WebView");
    }
    if (!p.contains("selector") || !p["selector"].is_string()) {
        return rpc::makeError(id, rpc::kInvalidParams, "selector required");
    }
    if (!p.contains("value") || !p["value"].is_string()) {
        return rpc::makeError(id, rpc::kInvalidParams, "value required");
    }
    auto selector = p["selector"].get<std::string>();
    auto value    = p["value"].get<std::string>();

    // Escape single quotes in both selector and value.
    auto escapeSQ = [](const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '\'') out += "\\'";
            else if (c == '\\') out += "\\\\";
            else out += c;
        }
        return out;
    };

    std::string script =
        "(() => { var el = document.querySelector('" + escapeSQ(selector) +
        "'); if (!el) return 'not_found';"
        " var nativeSetter = Object.getOwnPropertyDescriptor("
        "window.HTMLInputElement.prototype, 'value').set;"
        " nativeSetter.call(el, '" + escapeSQ(value) + "');"
        " el.dispatchEvent(new Event('input', {bubbles:true}));"
        " el.dispatchEvent(new Event('change', {bubbles:true}));"
        " return 'filled'; })()";

    webview_cb_("executeJS", {{"script", script}});
    return rpc::makeResult(id, {{"success", true}});
}

// ---------------------------------------------------------------------------
// browser.open — (legacy) navigate to URL (alias)
// ---------------------------------------------------------------------------

rpc::Response CommandDispatcher::handleBrowserOpen(
    std::optional<int64_t> id, const nlohmann::json& p) {
    if (!p.contains("url") || !p["url"].is_string()) {
        return rpc::makeError(id, rpc::kInvalidParams, "url required");
    }
    if (!webview_cb_) {
        return rpc::makeError(id, rpc::kNotFound, "No active WebView");
    }
    webview_cb_("navigate", {{"url", p["url"].get<std::string>()}});
    return rpc::makeResult(id, {{"success", true}});
}

}  // namespace termcore
