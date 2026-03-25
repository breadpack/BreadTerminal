#include "arg_parser.h"
#include "cli_client.h"
#include "hooks_installer.h"
#include "local_commands.h"
#include "output_formatter.h"

#include <iostream>
#include <nlohmann/json.hpp>

int main(int argc, char* argv[]) {
    auto args = bread::parseArgs(argc, argv);
    if (!args.valid) {
        if (!args.error.empty()) {
            std::cerr << "Error: " << args.error << "\n";
        }
        return 1;
    }

    // Handle local commands that don't need server connection
    if (args.type == bread::CommandType::LocalCommand) {
        switch (args.local_cmd) {
            case bread::LocalCmd::HooksInstall:
                return bread::installHooks();
            case bread::LocalCmd::Identify:
                return bread::cmdIdentify(args);
            case bread::LocalCmd::Capabilities:
                return bread::cmdCapabilities(args);
            case bread::LocalCmd::GetText:
                return bread::cmdGetText(args);
            case bread::LocalCmd::None:
                break;
        }
        return 0;
    }

    // Remote RPC command: build JSON-RPC request
    std::string request_json = bread::buildRequestJson(args);

    // Connect to server
    bread::CliClient client;
    if (!client.connect(args.socket_path, args.timeout_ms)) {
        if (args.method == "hook.event") {
            std::cerr << "Warning: BreadTerminal not running, hook event skipped\n";
            return 0;  // Don't break hook chain
        }
        std::cerr << "Error: " << client.lastError() << "\n";
        return 1;
    }

    // Send request and receive response
    std::string response_str;
    if (!client.sendRequest(request_json, response_str)) {
        std::cerr << "Error: " << client.lastError() << "\n";
        client.close();
        return 1;
    }

    client.close();

    // Parse response JSON
    nlohmann::json response;
    try {
        response = nlohmann::json::parse(response_str);
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "Error: Invalid JSON response from server: " << e.what() << "\n";
        return 1;
    }

    // Format and print
    int exit_code = 0;
    std::string output = bread::formatResponse(response, args.json_output, exit_code);

    if (exit_code != 0) {
        std::cerr << output;
    } else {
        std::cout << output;
    }

    return exit_code;
}
