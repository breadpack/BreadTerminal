#include "arg_parser.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace breadterminal {

static void printUsage() {
    std::cerr
        << "Usage: breadterminal [options] <command> [args...]\n"
        << "\nWorkspace/Pane management:\n"
        << "  list-workspaces [--json]              List all workspaces\n"
        << "  new-workspace [--name NAME]           Create workspace\n"
        << "  new-split {left|right|up|down}        Split current pane\n"
        << "      [--workspace ID] [--tab ID] [--pane ID]\n"
        << "  list-panes [--workspace ID] [--json]  List all panes\n"
        << "  focus-pane --pane ID                  Focus a specific pane\n"
        << "  close-pane --pane ID                  Close a pane\n"
        << "\nText I/O:\n"
        << "  send --pane ID \"text\"                 Send text to a pane's PTY\n"
        << "  send-key --pane ID KEY [KEY...]       Send special keys\n"
        << "  read-screen --pane ID [--lines N]     Read terminal screen output\n"
        << "\nStatus/Progress:\n"
        << "  set-status --pane ID KEY VALUE [--icon ICON]\n"
        << "  set-progress --pane ID FLOAT [--label TEXT]\n"
        << "  log --level {info|success|warning|error} MESSAGE\n"
        << "  notify --title TITLE --body BODY\n"
        << "\nUtility:\n"
        << "  ping                                  Health check\n"
        << "  identify [--json]                     Get current workspace/pane IDs\n"
        << "\nLow-level (resource.action):\n"
        << "  workspace  create|list|switch|destroy\n"
        << "  tab        create|list|switch|close\n"
        << "  pane       split|close|focus|list|send-text|send-keys|read-screen\n"
        << "  pane       set-status|set-progress\n"
        << "  agent      log\n"
        << "  notify     send\n"
        << "  browser    open|navigate|snapshot\n"
        << "  query      active-pane|pane-info|agent-state\n"
        << "\nGlobal options:\n"
        << "  --socket <path>    Socket path (or BREADTERMINAL_SOCKET env)\n"
        << "  --token <token>    Auth token (or BREADTERMINAL_TOKEN env)\n"
        << "  --json             Raw JSON output\n"
        << "  --timeout <ms>     Timeout in milliseconds (default: 3000)\n";
}

/// Subcommand alias table: maps shorthand commands to {method, param_rules}.
struct SubcommandDef {
    std::string method;
};

static const std::unordered_map<std::string, SubcommandDef> kSubcommands = {
    {"list-workspaces", {"workspace.list"}},
    {"new-workspace",   {"workspace.create"}},
    {"new-split",       {"pane.split"}},
    {"list-panes",      {"pane.list"}},
    {"focus-pane",      {"pane.focus"}},
    {"close-pane",      {"pane.close"}},
    {"send",            {"pane.send-text"}},
    {"send-key",        {"pane.send-keys"}},
    {"read-screen",     {"pane.read-screen"}},
    {"set-status",      {"pane.set-status"}},
    {"set-progress",    {"pane.set-progress"}},
    {"log",             {"agent.log"}},
    {"notify",          {"notify.send"}},
    {"ping",            {"query.active-pane"}},
    {"identify",        {"query.active-pane"}},
};

/// Direction mapping: CLI direction names -> split direction + IDs
static std::string mapSplitDirection(const std::string& dir) {
    if (dir == "left" || dir == "right" || dir == "horizontal") return "horizontal";
    if (dir == "up" || dir == "down" || dir == "vertical") return "vertical";
    return dir;
}

static void extractGlobalFlags(int argc, char* argv[],
                                 ParsedArgs& result,
                                 std::vector<std::string>& positional,
                                 std::unordered_map<std::string, std::string>& flags) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage();
            return;
        }
        if (arg == "--json") {
            result.json_output = true;
            continue;
        }
        if (arg == "--scrollback") {
            flags["scrollback"] = "true";
            continue;
        }
        if (arg.starts_with("--") && i + 1 < argc) {
            std::string key = arg.substr(2);
            if (key == "socket") { result.socket_path = argv[++i]; continue; }
            if (key == "token")  { result.auth_token = argv[++i]; continue; }
            if (key == "timeout") { result.timeout_ms = std::atoi(argv[++i]); continue; }
            flags[key] = argv[++i];
            continue;
        }
        positional.push_back(arg);
    }
}

static void applyCommonFlags(nlohmann::json& params,
                               const std::unordered_map<std::string, std::string>& flags) {
    auto setInt = [&](const std::string& flag, const std::string& param) {
        if (flags.count(flag)) params[param] = std::atoi(flags.at(flag).c_str());
    };
    auto setStr = [&](const std::string& flag, const std::string& param) {
        if (flags.count(flag)) params[param] = flags.at(flag);
    };

    setInt("workspace", "workspace_id");
    setInt("id", "workspace_id");
    setInt("tab", "tab_id");
    setInt("pane", "pane_id");
    setStr("name", "name");
    setInt("rows", "rows");
    setInt("cols", "cols");
    setStr("title", "title");
    setStr("body", "body");
    setStr("urgency", "urgency");
    setStr("source", "source");
    setStr("url", "url");
    setStr("format", "format");
    setStr("icon", "icon");
    setStr("label", "label");
    setStr("level", "level");
    setInt("lines", "lines");

    if (flags.count("direction")) {
        params["direction"] = mapSplitDirection(flags.at("direction"));
    }
}

static ParsedArgs parseSubcommand(const std::string& subcmd,
                                    const std::vector<std::string>& positional,
                                    std::unordered_map<std::string, std::string>& flags,
                                    ParsedArgs& result) {
    auto it = kSubcommands.find(subcmd);
    if (it == kSubcommands.end()) {
        result.error = "Unknown subcommand: " + subcmd;
        return result;
    }

    result.method = it->second.method;
    nlohmann::json params = nlohmann::json::object();
    applyCommonFlags(params, flags);

    // Subcommand-specific positional arg handling
    if (subcmd == "new-split") {
        // breadterminal new-split {left|right|up|down}
        if (positional.size() >= 2) {
            params["direction"] = mapSplitDirection(positional[1]);
        }
    } else if (subcmd == "send") {
        // breadterminal send --pane ID "text"
        if (flags.count("text")) {
            params["text"] = flags["text"];
        } else if (positional.size() >= 2) {
            params["text"] = positional[1];
        }
    } else if (subcmd == "send-key") {
        // breadterminal send-key --pane ID enter tab ...
        nlohmann::json keys = nlohmann::json::array();
        for (size_t i = 1; i < positional.size(); ++i) {
            keys.push_back(positional[i]);
        }
        if (!keys.empty()) params["keys"] = keys;
    } else if (subcmd == "set-status") {
        // breadterminal set-status --pane ID KEY VALUE
        if (positional.size() >= 2 && !params.contains("key")) {
            params["key"] = positional[1];
        }
        if (positional.size() >= 3 && !params.contains("value")) {
            params["value"] = positional[2];
        }
        if (flags.count("key"))   params["key"] = flags["key"];
        if (flags.count("value")) params["value"] = flags["value"];
    } else if (subcmd == "set-progress") {
        // breadterminal set-progress --pane ID 0.5
        if (positional.size() >= 2 && !params.contains("value")) {
            params["value"] = std::atof(positional[1].c_str());
        }
        if (flags.count("value")) {
            params["value"] = std::atof(flags["value"].c_str());
        }
    } else if (subcmd == "log") {
        // breadterminal log --level info "message..."
        if (positional.size() >= 2 && !params.contains("message")) {
            // Join remaining positional args as message
            std::string msg;
            for (size_t i = 1; i < positional.size(); ++i) {
                if (!msg.empty()) msg += " ";
                msg += positional[i];
            }
            params["message"] = msg;
        }
        if (flags.count("message")) params["message"] = flags["message"];
    } else if (subcmd == "notify") {
        // breadterminal notify --title TITLE --body BODY
        // (handled by applyCommonFlags)
    } else if (subcmd == "read-screen") {
        // breadterminal read-screen --pane ID [--lines N] [--scrollback]
        if (flags.count("scrollback")) {
            params["scrollback"] = true;
        }
    }

    result.params = params;
    result.valid = true;
    return result;
}

static ParsedArgs parseResourceAction(const std::vector<std::string>& positional,
                                        std::unordered_map<std::string, std::string>& flags,
                                        ParsedArgs& result) {
    if (positional.size() < 2) {
        result.error = "Expected: <resource> <action> or <subcommand>";
        printUsage();
        return result;
    }

    std::string resource = positional[0];
    std::string action = positional[1];
    result.method = resource + "." + action;

    nlohmann::json params = nlohmann::json::object();
    applyCommonFlags(params, flags);

    // Text param
    if (flags.count("text")) {
        params["text"] = flags["text"];
    } else if (resource == "pane" && action == "send-text" && positional.size() > 2) {
        params["text"] = positional[2];
    }

    // Keys param for send-keys
    if (resource == "pane" && action == "send-keys" && positional.size() > 2) {
        nlohmann::json keys = nlohmann::json::array();
        for (size_t i = 2; i < positional.size(); ++i) {
            keys.push_back(positional[i]);
        }
        params["keys"] = keys;
    }

    // Progress value
    if (resource == "pane" && action == "set-progress") {
        if (flags.count("value")) {
            params["value"] = std::atof(flags["value"].c_str());
        }
    }

    // Status key/value
    if (resource == "pane" && action == "set-status") {
        if (flags.count("key"))   params["key"] = flags["key"];
        if (flags.count("value")) params["value"] = flags["value"];
    }

    // Log message
    if (resource == "agent" && action == "log") {
        if (flags.count("message")) params["message"] = flags["message"];
        if (positional.size() > 2 && !params.contains("message")) {
            std::string msg;
            for (size_t i = 2; i < positional.size(); ++i) {
                if (!msg.empty()) msg += " ";
                msg += positional[i];
            }
            params["message"] = msg;
        }
    }

    result.params = params;
    result.valid = true;
    return result;
}

ParsedArgs parseArgs(int argc, char* argv[]) {
    ParsedArgs result;
    result.valid = false;

    // Resolve defaults from environment
    const char* env_socket = std::getenv("BREADTERMINAL_SOCKET");
    result.socket_path = env_socket ? env_socket : "/tmp/breadterminal.sock";

    const char* env_token = std::getenv("BREADTERMINAL_TOKEN");
    if (env_token) result.auth_token = env_token;

    std::vector<std::string> positional;
    std::unordered_map<std::string, std::string> flags;

    extractGlobalFlags(argc, argv, result, positional, flags);

    if (positional.empty()) {
        result.error = "No command specified";
        printUsage();
        return result;
    }

    // Check if first positional arg is a known subcommand
    const std::string& cmd = positional[0];
    if (kSubcommands.count(cmd)) {
        parseSubcommand(cmd, positional, flags, result);
    } else {
        parseResourceAction(positional, flags, result);
    }

    // Inject auth token if set
    if (result.valid && !result.auth_token.empty()) {
        result.params["_auth"] = result.auth_token;
    }

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

}  // namespace breadterminal
