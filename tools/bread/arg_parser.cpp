#include "arg_parser.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace bread {

static void printUsage() {
    std::cerr << "Usage: bread [options] <resource> <action> [flags...]\n"
              << "\nResources:\n"
              << "  workspace  create|list|switch|destroy\n"
              << "  tab        create|list|switch|close\n"
              << "  pane       split|close|focus|list|send-text|send-keys\n"
              << "  notify     send\n"
              << "  browser    open|navigate|snapshot\n"
              << "  query      active-pane|pane-info|agent-state|scrollback\n"
              << "\nLocal commands:\n"
              << "  hooks      install — install Claude Code hook scripts\n"
              << "  identify   — print terminal name and version\n"
              << "  capabilities — list supported features\n"
              << "  get-text   --pane N --lines M — read last M lines from pane scrollback\n"
              << "\nGlobal options:\n"
              << "  --socket <path>    Socket path\n"
              << "  --token <token>    Auth token\n"
              << "  --json             Raw JSON output\n"
              << "  --ref <ref>        Ref ID (e.g. ws:1/tab:1/pane:1)\n"
              << "  --timeout <ms>     Timeout in milliseconds (default: 3000)\n";
}

bool parseRefId(const std::string& ref, nlohmann::json& params) {
    // Parse "ws:1/tab:2/pane:3" format
    std::istringstream stream(ref);
    std::string segment;
    while (std::getline(stream, segment, '/')) {
        auto colon = segment.find(':');
        if (colon == std::string::npos) return false;
        auto key = segment.substr(0, colon);
        auto val = segment.substr(colon + 1);
        int num = std::atoi(val.c_str());
        if (num <= 0) return false;

        if (key == "ws") {
            params["workspace_id"] = num;
        } else if (key == "tab") {
            params["tab_id"] = num;
        } else if (key == "pane") {
            params["pane_id"] = num;
        } else {
            return false;
        }
    }
    return true;
}

ParsedArgs parseArgs(int argc, char* argv[]) {
    ParsedArgs result;
    result.valid = false;

    // Resolve defaults from environment
    const char* env_socket = std::getenv("BREADTERMINAL_SOCKET");
    result.socket_path = env_socket ? env_socket : "/tmp/breadterminal.sock";

    const char* env_token = std::getenv("BREADTERMINAL_TOKEN");
    if (env_token) result.auth_token = env_token;

    // Separate global flags from positional args
    std::vector<std::string> positional;
    std::unordered_map<std::string, std::string> flags;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage();
            return result;
        }

        if (arg == "--json") {
            result.json_output = true;
            continue;
        }

        if (arg.starts_with("--") && i + 1 < argc) {
            std::string key = arg.substr(2);
            if (key == "socket") {
                result.socket_path = argv[++i];
                continue;
            }
            if (key == "token") {
                result.auth_token = argv[++i];
                continue;
            }
            if (key == "timeout") {
                result.timeout_ms = std::atoi(argv[++i]);
                continue;
            }
            // Store as a command flag
            flags[key] = argv[++i];
            continue;
        }

        // Positional argument (could be resource, action, or trailing text)
        positional.push_back(arg);
    }

    if (positional.empty()) {
        result.error = "Expected: <resource> <action>";
        printUsage();
        return result;
    }

    // Check for local commands first
    if (positional[0] == "hooks") {
        if (positional.size() >= 2 && positional[1] == "install") {
            result.type = CommandType::LocalCommand;
            result.local_cmd = LocalCmd::HooksInstall;
            result.valid = true;
            return result;
        }
        result.error = "Unknown hooks subcommand. Try: bread hooks install";
        return result;
    }

    if (positional[0] == "identify") {
        result.type = CommandType::LocalCommand;
        result.local_cmd = LocalCmd::Identify;
        result.json_output = flags.count("json") || result.json_output;
        result.valid = true;
        return result;
    }

    if (positional[0] == "capabilities") {
        result.type = CommandType::LocalCommand;
        result.local_cmd = LocalCmd::Capabilities;
        result.json_output = flags.count("json") || result.json_output;
        result.valid = true;
        return result;
    }

    if (positional[0] == "get-text") {
        result.type = CommandType::LocalCommand;
        result.local_cmd = LocalCmd::GetText;
        if (flags.count("pane")) {
            result.pane_id = std::atoi(flags["pane"].c_str());
        }
        if (flags.count("lines")) {
            result.line_count = std::atoi(flags["lines"].c_str());
        }
        if (flags.count("ref")) {
            result.ref_id = flags["ref"];
        }
        result.valid = true;
        return result;
    }

    // Remote RPC commands require at least 2 positional args
    if (positional.size() < 2) {
        result.error = "Expected: <resource> <action>";
        printUsage();
        return result;
    }

    // Build method: resource.action
    std::string resource = positional[0];
    std::string action = positional[1];

    // Special case: "notify" resource uses "notify.send" method
    if (resource == "notify" && action == "send") {
        result.method = "notify.send";
    } else {
        result.method = resource + "." + action;
    }

    // Build params from flags
    nlohmann::json params = nlohmann::json::object();

    // If --ref is provided, parse it into params first
    if (flags.count("ref")) {
        if (!parseRefId(flags["ref"], params)) {
            result.error = "Invalid ref ID format. Expected: ws:N/tab:N/pane:N";
            return result;
        }
    }

    // Map CLI flags to JSON params based on method
    auto setInt = [&](const std::string& flag, const std::string& param) {
        if (flags.count(flag)) {
            params[param] = std::atoi(flags[flag].c_str());
        }
    };

    auto setStr = [&](const std::string& flag, const std::string& param) {
        if (flags.count(flag)) {
            params[param] = flags[flag];
        }
    };

    // Common ID params (only set if not already set by --ref)
    if (!params.contains("workspace_id")) {
        setInt("workspace", "workspace_id");
        setInt("id", "workspace_id");
    }
    if (!params.contains("tab_id")) {
        setInt("tab", "tab_id");
    }
    if (!params.contains("pane_id")) {
        setInt("pane", "pane_id");
    }
    setStr("name", "name");
    setInt("rows", "rows");
    setInt("cols", "cols");
    setInt("lines", "lines");

    // Direction handling
    if (flags.count("direction")) {
        auto d = flags["direction"];
        // CLI "right" -> "horizontal", "down" -> "vertical"
        if (d == "right" || d == "horizontal") {
            params["direction"] = "horizontal";
        } else if (d == "down" || d == "vertical") {
            params["direction"] = "vertical";
        } else {
            params["direction"] = d;
        }
    }

    // Notification params
    setStr("title", "title");
    setStr("body", "body");
    setStr("urgency", "urgency");
    setStr("source", "source");

    // Browser params
    setStr("url", "url");
    setStr("action", "action");
    setStr("format", "format");

    // Text param: either from --text flag or trailing positional arg
    if (flags.count("text")) {
        params["text"] = flags["text"];
    } else if (resource == "pane" && action == "send-text" && positional.size() > 2) {
        params["text"] = positional[2];
    }

    // Keys param for send-keys: trailing positional args
    if (resource == "pane" && action == "send-keys" && positional.size() > 2) {
        nlohmann::json keys = nlohmann::json::array();
        for (size_t i = 2; i < positional.size(); ++i) {
            keys.push_back(positional[i]);
        }
        params["keys"] = keys;
    }

    // Auth token injection
    if (!result.auth_token.empty()) {
        params["_auth"] = result.auth_token;
    }

    result.params = params;
    result.valid = true;
    return result;
}

std::string buildRequestJson(const ParsedArgs& args) {
    nlohmann::json req;
    req["jsonrpc"] = "2.0";
    req["method"] = args.method;
    req["params"] = args.params;
    req["id"] = args.request_id;
    return req.dump();
}

}  // namespace bread
