// D:/Projects/BreadTerminal/core/src/lua_bindings/lua_http_module.h
#pragma once

#include "termcore/lua_module.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace termcore {

struct HttpRequest {
    std::string method;  // GET, POST, PUT, DELETE, PATCH
    std::string url;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    int timeout_ms = 10000;
};

struct HttpResponse {
    bool ok = false;       // true if status 200-299
    int status = 0;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    std::string error;     // set if request failed entirely
};

using HttpCallback = std::function<void(HttpResponse)>;

class IHttpProvider {
public:
    virtual ~IHttpProvider() = default;
    // Execute request asynchronously. Callback will be called on completion.
    virtual void execute(const HttpRequest& request, HttpCallback callback) = 0;
};

class LuaHttpModule : public ILuaModule {
public:
    LuaHttpModule();
    ~LuaHttpModule();

    std::string_view moduleName() const override { return "http"; }
    PluginCapability requiredCapability() const override {
        return PluginCapability::Networking;
    }
    void registerBindings(void* luaState, void* terminalTable) override;
    void clearCallbacks() override;

    void setProvider(IHttpProvider* provider);

    // Process completed HTTP responses and call Lua callbacks.
    // Called by host on main thread.
    void processPendingResponses();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termcore
