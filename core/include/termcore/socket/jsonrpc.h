#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>

namespace termcore::rpc {

/// JSON-RPC 2.0 Request
struct Request {
    std::string jsonrpc = "2.0";
    std::string method;
    nlohmann::json params;            // object or array
    std::optional<int64_t> id;        // nullopt = notification (no response expected)
};

/// JSON-RPC 2.0 Error object
struct Error {
    int code = 0;
    std::string message;
    nlohmann::json data;
};

/// JSON-RPC 2.0 Response
struct Response {
    std::string jsonrpc = "2.0";
    std::optional<int64_t> id;
    nlohmann::json result;            // present on success
    std::optional<Error> error;       // present on failure
};

/// Parse a JSON-RPC request from a single JSON line.
/// On parse failure, returns a Request with method="" and sets parseError output.
Request parseRequest(const std::string& line, std::string* parseError = nullptr);

/// Serialize a Response to a single JSON line (no trailing newline).
std::string serializeResponse(const Response& r);

/// Convenience: create a success response.
Response makeResult(std::optional<int64_t> id, nlohmann::json result);

/// Convenience: create an error response.
Response makeError(std::optional<int64_t> id, int code, const std::string& message,
                   nlohmann::json data = nullptr);

// Standard JSON-RPC 2.0 error codes
constexpr int kParseError       = -32700;
constexpr int kInvalidRequest   = -32600;
constexpr int kMethodNotFound   = -32601;
constexpr int kInvalidParams    = -32602;
constexpr int kInternalError    = -32603;

// Application-specific error codes
constexpr int kNotFound         = -32000;
constexpr int kPermissionDenied = -32001;

}  // namespace termcore::rpc
