#include "termcore/socket/jsonrpc.h"

namespace termcore::rpc {

Request parseRequest(const std::string& line, std::string* parseError) {
    Request req;

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(line);
    } catch (const nlohmann::json::parse_error& e) {
        if (parseError) *parseError = std::string("Parse error: ") + e.what();
        return req;  // method="" signals failure
    }

    if (!j.is_object()) {
        if (parseError) *parseError = "Request must be a JSON object";
        return req;
    }

    // jsonrpc field
    if (j.contains("jsonrpc") && j["jsonrpc"].is_string()) {
        req.jsonrpc = j["jsonrpc"].get<std::string>();
    }

    // method (required)
    if (!j.contains("method") || !j["method"].is_string()) {
        if (parseError) *parseError = "Missing or invalid 'method' field";
        return req;  // method="" signals failure
    }
    req.method = j["method"].get<std::string>();

    // params (optional, default empty object)
    if (j.contains("params")) {
        req.params = j["params"];
    } else {
        req.params = nlohmann::json::object();
    }

    // id (optional, null means notification)
    if (j.contains("id")) {
        if (j["id"].is_number_integer()) {
            req.id = j["id"].get<int64_t>();
        } else if (j["id"].is_null()) {
            req.id = std::nullopt;
        } else if (j["id"].is_number()) {
            req.id = static_cast<int64_t>(j["id"].get<double>());
        }
        // String ids are not supported in this implementation
    } else {
        req.id = std::nullopt;
    }

    return req;
}

std::string serializeResponse(const Response& r) {
    nlohmann::json j;
    j["jsonrpc"] = r.jsonrpc;

    if (r.id.has_value()) {
        j["id"] = r.id.value();
    } else {
        j["id"] = nullptr;
    }

    if (r.error.has_value()) {
        nlohmann::json err;
        err["code"] = r.error->code;
        err["message"] = r.error->message;
        if (!r.error->data.is_null()) {
            err["data"] = r.error->data;
        }
        j["error"] = err;
    } else {
        j["result"] = r.result;
    }

    return j.dump();
}

Response makeResult(std::optional<int64_t> id, nlohmann::json result) {
    Response r;
    r.id = id;
    r.result = std::move(result);
    return r;
}

Response makeError(std::optional<int64_t> id, int code, const std::string& message,
                   nlohmann::json data) {
    Response r;
    r.id = id;
    r.error = Error{code, message, std::move(data)};
    return r;
}

}  // namespace termcore::rpc
