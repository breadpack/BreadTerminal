#pragma once

#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

namespace bread {

/// The type of command: either a JSON-RPC call to the server, or a local command.
enum class CommandType {
    RemoteRPC,      // Requires socket connection to BreadTerminal
    LocalCommand    // Handled entirely by the CLI tool
};

/// Which local command to run.
enum class LocalCmd {
    None,
    HooksInstall,
    Identify,
    Capabilities,
    GetText,
    OscEmit,
    HooksStatus
};

/// Parsed CLI arguments ready for JSON-RPC dispatch.
struct ParsedArgs {
    CommandType type = CommandType::RemoteRPC;
    LocalCmd local_cmd = LocalCmd::None;

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

    /// For local commands that need specific params
    int pane_id = 0;
    int line_count = 50;
    std::string ref_id;  // e.g. "ws:1/tab:1/pane:1"
};

/// Parse command-line arguments into a JSON-RPC request structure.
ParsedArgs parseArgs(int argc, char* argv[]);

/// Build the JSON-RPC request line from parsed args.
std::string buildRequestJson(const ParsedArgs& args);

/// Parse a ref ID like "ws:1/tab:2/pane:3" into JSON params.
/// Returns true on success, filling workspace_id, tab_id, pane_id in params.
bool parseRefId(const std::string& ref, nlohmann::json& params);

}  // namespace bread
