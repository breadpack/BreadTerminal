#include "arg_parser.h"
#include "cli_client.h"
#include "output_formatter.h"

#include <iostream>
#include <nlohmann/json.hpp>

int main(int argc, char* argv[]) {
    auto args = breadterminal::parseArgs(argc, argv);
    if (!args.valid) {
        if (!args.error.empty()) {
            std::cerr << "Error: " << args.error << "\n";
        }
        return 1;
    }

    // Build JSON-RPC request
    std::string request_json = breadterminal::buildRequestJson(args);

    // Connect to server
    breadterminal::CliClient client;
    if (!client.connect(args.socket_path, args.timeout_ms)) {
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
    std::string output = breadterminal::formatResponse(response, args.json_output, exit_code);

    if (exit_code != 0) {
        std::cerr << output;
    } else {
        std::cout << output;
    }

    return exit_code;
}
