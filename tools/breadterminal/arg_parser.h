#pragma once

#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

namespace breadterminal {

/// Parsed CLI arguments ready for JSON-RPC dispatch.
struct ParsedArgs {
    std::string method;
    nlohmann::json params;
    std::string socket_path;
    std::string auth_token;
    bool json_output = false;
    int timeout_ms = 3000;
    int64_t request_id = 1;

    /// Whether parsing succeeded.
    bool valid = false;
    std::string error;
};

/// Parse command-line arguments into a JSON-RPC request structure.
ParsedArgs parseArgs(int argc, char* argv[]);

/// Build the JSON-RPC request line from parsed args.
std::string buildRequestJson(const ParsedArgs& args);

}  // namespace breadterminal
