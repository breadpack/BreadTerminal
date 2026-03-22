#include "local_commands.h"
#include "cli_client.h"
#include "output_formatter.h"

#include <iostream>
#include <nlohmann/json.hpp>

namespace bread {

static constexpr const char* kTerminalName = "BreadTerminal";
static constexpr const char* kTerminalVersion = "0.1.0";

int cmdIdentify(const ParsedArgs& args) {
    if (args.json_output) {
        nlohmann::json out;
        out["name"] = kTerminalName;
        out["version"] = kTerminalVersion;
        out["protocol"] = "jsonrpc-2.0";
        out["socket"] = args.socket_path;
        std::cout << out.dump(2) << "\n";
    } else {
        std::cout << kTerminalName << " " << kTerminalVersion << "\n"
                  << "Protocol: JSON-RPC 2.0\n"
                  << "Socket: " << args.socket_path << "\n";
    }
    return 0;
}

int cmdCapabilities(const ParsedArgs& args) {
    nlohmann::json caps = nlohmann::json::array();
    caps.push_back("osc9_notifications");
    caps.push_back("kitty_graphics");
    caps.push_back("sixel_graphics");
    caps.push_back("iterm2_inline_images");
    caps.push_back("bracketed_paste");
    caps.push_back("mouse_reporting");
    caps.push_back("truecolor");
    caps.push_back("unicode");
    caps.push_back("scrollback_query");
    caps.push_back("agent_tracking");
    caps.push_back("workspace_management");
    caps.push_back("split_panes");
    caps.push_back("webview");
    caps.push_back("lua_plugins");

    if (args.json_output) {
        nlohmann::json out;
        out["terminal"] = kTerminalName;
        out["version"] = kTerminalVersion;
        out["capabilities"] = caps;
        std::cout << out.dump(2) << "\n";
    } else {
        std::cout << kTerminalName << " " << kTerminalVersion << " capabilities:\n";
        for (const auto& cap : caps) {
            std::cout << "  - " << cap.get<std::string>() << "\n";
        }
    }
    return 0;
}

int cmdGetText(const ParsedArgs& args) {
    // Build JSON-RPC request for query.scrollback
    nlohmann::json params;

    // Parse ref ID if provided
    if (!args.ref_id.empty()) {
        if (!parseRefId(args.ref_id, params)) {
            std::cerr << "Error: Invalid ref ID format.\n";
            return 1;
        }
    }

    if (args.pane_id > 0) {
        params["pane_id"] = args.pane_id;
    }
    params["lines"] = args.line_count;

    nlohmann::json req;
    req["jsonrpc"] = "2.0";
    req["method"] = "query.scrollback";
    req["params"] = params;
    req["id"] = 1;

    // Connect and send
    CliClient client;
    if (!client.connect(args.socket_path, args.timeout_ms)) {
        std::cerr << "Error: " << client.lastError() << "\n";
        return 1;
    }

    std::string response_str;
    if (!client.sendRequest(req.dump(), response_str)) {
        std::cerr << "Error: " << client.lastError() << "\n";
        client.close();
        return 1;
    }
    client.close();

    nlohmann::json response;
    try {
        response = nlohmann::json::parse(response_str);
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "Error: Invalid JSON response: " << e.what() << "\n";
        return 1;
    }

    int exit_code = 0;
    std::string output = formatResponse(response, args.json_output, exit_code);

    if (exit_code != 0) {
        std::cerr << output;
    } else {
        std::cout << output;
    }

    return exit_code;
}

}  // namespace bread
