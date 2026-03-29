// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_http_module.cpp
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include "lua_http_module.h"

#include <algorithm>
#include <string>

namespace termcore {

// Reject URLs targeting internal/loopback addresses (SSRF prevention)
static bool isBlockedUrl(const std::string& url) {
    // Extract host portion from URL
    auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return true;  // no scheme = blocked

    std::string scheme = url.substr(0, schemeEnd);
    // Only allow http and https
    std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (scheme != "http" && scheme != "https") return true;

    std::string rest = url.substr(schemeEnd + 3);
    // Extract host (before first / or :)
    auto hostEnd = rest.find_first_of(":/");
    std::string host = rest.substr(0, hostEnd);
    std::transform(host.begin(), host.end(), host.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (host.empty()) return true;

    // Block loopback
    if (host == "localhost" || host == "127.0.0.1" || host == "::1" ||
        host == "[::1]" || host == "0.0.0.0") return true;

    // Block 10.x.x.x, 172.16-31.x.x, 192.168.x.x
    if (host.substr(0, 3) == "10." ||
        host.substr(0, 8) == "192.168." ||
        host.substr(0, 4) == "172.") {
        // Simplified: block all 172.x for safety
        return true;
    }

    // Block metadata endpoints (AWS/GCP/Azure)
    if (host == "169.254.169.254" || host == "metadata.google.internal") {
        return true;
    }

    return false;
}

struct LuaHttpModule::Impl {
    struct PendingResponse {
        HttpResponse response;
        std::shared_ptr<sol::protected_function> callback;
    };

    IHttpProvider* provider = nullptr;
    void* luaPtr = nullptr;
    std::mutex pending_mutex;
    std::vector<PendingResponse> pending_responses;
};

LuaHttpModule::LuaHttpModule() : impl_(std::make_unique<Impl>()) {}
LuaHttpModule::~LuaHttpModule() = default;

void LuaHttpModule::setProvider(IHttpProvider* provider) {
    impl_->provider = provider;
}

void LuaHttpModule::registerBindings(void* luaState, void* terminalTable) {
    auto& lua = *static_cast<sol::state*>(luaState);
    (void)lua;
    auto& terminal = *static_cast<sol::table*>(terminalTable);
    impl_->luaPtr = luaState;

    auto http = terminal.create_named("http");

    // Helper: parse options table into HttpRequest fields
    auto parseOptions = [](HttpRequest& req, const sol::table& opts) {
        // headers
        sol::optional<sol::table> hdrs = opts["headers"];
        if (hdrs) {
            for (auto& [k, v] : *hdrs) {
                if (k.get_type() == sol::type::string &&
                    v.get_type() == sol::type::string) {
                    req.headers[k.as<std::string>()] = v.as<std::string>();
                }
            }
        }
        // body
        sol::optional<std::string> body = opts["body"];
        if (body) req.body = *body;
        // timeout
        sol::optional<int> timeout = opts["timeout"];
        if (timeout) req.timeout_ms = *timeout;
    };

    // Shared dispatch logic
    auto* impl = impl_.get();
    auto dispatchRequest = [impl](HttpRequest req,
                                  sol::protected_function callback) {
        // SSRF prevention: block requests to internal addresses
        if (isBlockedUrl(req.url)) {
            HttpResponse resp;
            resp.ok = false;
            resp.status = 0;
            resp.error = "Blocked: requests to internal/loopback addresses are not allowed";
            auto cb = std::make_shared<sol::protected_function>(
                std::move(callback));
            std::lock_guard<std::mutex> lock(impl->pending_mutex);
            impl->pending_responses.push_back({std::move(resp), std::move(cb)});
            return;
        }

        if (!impl->provider) {
            // No provider -- immediately queue error response
            HttpResponse resp;
            resp.ok = false;
            resp.status = 0;
            resp.error = "No HTTP provider configured";
            auto cb = std::make_shared<sol::protected_function>(
                std::move(callback));
            std::lock_guard<std::mutex> lock(impl->pending_mutex);
            impl->pending_responses.push_back({std::move(resp), std::move(cb)});
            return;
        }

        auto cb = std::make_shared<sol::protected_function>(
            std::move(callback));

        impl->provider->execute(req,
            [impl, cb](HttpResponse resp) {
                std::lock_guard<std::mutex> lock(impl->pending_mutex);
                // Limit pending response queue to prevent memory exhaustion
                if (impl->pending_responses.size() >= 256) {
                    return;  // Drop response if queue is full
                }
                impl->pending_responses.push_back(
                    {std::move(resp), cb});
            });
    };

    // terminal.http.get(url, [options,] callback)
    http.set_function("get",
        [dispatchRequest, parseOptions](
            const std::string& url,
            sol::variadic_args va) {
            HttpRequest req;
            req.method = "GET";
            req.url = url;

            sol::protected_function callback;
            if (va.size() >= 2) {
                // get(url, options, callback)
                sol::table opts = va[0];
                parseOptions(req, opts);
                callback = va[1];
            } else if (va.size() == 1) {
                // get(url, callback)
                callback = va[0];
            }

            dispatchRequest(std::move(req), std::move(callback));
        });

    // terminal.http.post(url, [options,] callback)
    http.set_function("post",
        [dispatchRequest, parseOptions](
            const std::string& url,
            sol::variadic_args va) {
            HttpRequest req;
            req.method = "POST";
            req.url = url;

            sol::protected_function callback;
            if (va.size() >= 2) {
                sol::table opts = va[0];
                parseOptions(req, opts);
                callback = va[1];
            } else if (va.size() == 1) {
                callback = va[0];
            }

            dispatchRequest(std::move(req), std::move(callback));
        });

    // terminal.http.request(options, callback)
    http.set_function("request",
        [dispatchRequest, parseOptions](
            sol::table opts, sol::protected_function callback) {
            HttpRequest req;

            // method
            sol::optional<std::string> method = opts["method"];
            req.method = method.value_or("GET");

            // url
            sol::optional<std::string> url = opts["url"];
            if (url) req.url = *url;

            parseOptions(req, opts);
            dispatchRequest(std::move(req), std::move(callback));
        });
}

void LuaHttpModule::processPendingResponses() {
    std::vector<Impl::PendingResponse> batch;
    {
        std::lock_guard<std::mutex> lock(impl_->pending_mutex);
        batch.swap(impl_->pending_responses);
    }

    if (!impl_->luaPtr) return;
    auto& lua = *static_cast<sol::state*>(impl_->luaPtr);

    for (auto& pr : batch) {
        if (!pr.callback) continue;

        // Build response table
        sol::table resp = lua.create_table();
        resp["ok"] = pr.response.ok;
        resp["status"] = pr.response.status;
        resp["body"] = pr.response.body;
        resp["error"] = pr.response.error;

        sol::table hdrs = lua.create_table();
        for (auto& [k, v] : pr.response.headers) {
            hdrs[k] = v;
        }
        resp["headers"] = hdrs;

        auto result = (*pr.callback)(resp);
        (void)result;
    }
}

void LuaHttpModule::clearCallbacks() {
    std::lock_guard<std::mutex> lock(impl_->pending_mutex);
    impl_->pending_responses.clear();
    impl_->luaPtr = nullptr;
}

} // namespace termcore
