#include "arg_parser.h"
#include "tmux_compat.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace bread {

static void printUsage() {
    std::cerr
        << "Usage: bread [options] <command> [args...]\n"
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
        << "  get-text --pane N --lines M           Read last M lines from pane scrollback\n"
        << "\nStatus/Progress:\n"
        << "  set-status --pane ID KEY VALUE [--icon ICON]\n"
        << "  set-progress --pane ID FLOAT [--label TEXT]\n"
        << "  log --level {info|success|warning|error} MESSAGE\n"
        << "  notify --title TITLE --body BODY\n"
        << "\nLocal commands:\n"
        << "  hooks install                         Install Claude Code hook scripts\n"
        << "  identify [--json]                     Print terminal name and version\n"
        << "  capabilities [--json]                 List supported features\n"
        << "\nLow-level (resource.action):\n"
        << "  workspace  create|list|switch|destroy\n"
        << "  tab        create|list|switch|close\n"
        << "  pane       split|close|focus|list|send-text|send-keys|read-screen\n"
        << "  pane       set-status|set-progress\n"
        << "  agent      log\n"
        << "  notify     send\n"
        << "  browser    open|navigate|snapshot\n"
        << "  query      active-pane|pane-info|agent-state|scrollback\n"
        << "\nGlobal options:\n"
        << "  --socket <path>    Socket path (or BREADTERMINAL_SOCKET env)\n"
        << "  --token <token>    Auth token (or BREADTERMINAL_TOKEN env)\n"
        << "  --json             Raw JSON output\n"
        << "  --ref <ref>        Ref ID (e.g. ws:1/tab:1/pane:1)\n"
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
        // bread new-split {left|right|up|down}
        if (positional.size() >= 2) {
            params["direction"] = mapSplitDirection(positional[1]);
        }
    } else if (subcmd == "send") {
        // bread send --pane ID "text"
        if (flags.count("text")) {
            params["text"] = flags["text"];
        } else if (positional.size() >= 2) {
            params["text"] = positional[1];
        }
    } else if (subcmd == "send-key") {
        // bread send-key --pane ID enter tab ...
        nlohmann::json keys = nlohmann::json::array();
        for (size_t i = 1; i < positional.size(); ++i) {
            keys.push_back(positional[i]);
        }
        if (!keys.empty()) params["keys"] = keys;
    } else if (subcmd == "set-status") {
        // bread set-status --pane ID KEY VALUE
        if (positional.size() >= 2 && !params.contains("key")) {
            params["key"] = positional[1];
        }
        if (positional.size() >= 3 && !params.contains("value")) {
            params["value"] = positional[2];
        }
        if (flags.count("key"))   params["key"] = flags["key"];
        if (flags.count("value")) params["value"] = flags["value"];
    } else if (subcmd == "set-progress") {
        // bread set-progress --pane ID 0.5
        if (positional.size() >= 2 && !params.contains("value")) {
            params["value"] = std::atof(positional[1].c_str());
        }
        if (flags.count("value")) {
            params["value"] = std::atof(flags["value"].c_str());
        }
    } else if (subcmd == "log") {
        // bread log --level info "message..."
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
        // bread notify --title TITLE --body BODY
        // (handled by applyCommonFlags)
    } else if (subcmd == "read-screen") {
        // bread read-screen --pane ID [--lines N] [--scrollback]
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
    // Intercept --tmux mode before any other parsing
    if (argc >= 2 && std::string(argv[1]) == "--tmux") {
        return parseTmuxArgs(argc - 2, argv + 2);
    }

    ParsedArgs result;
    result.valid = false;

    // Resolve defaults from environment
    const char* env_socket = std::getenv("BREADTERMINAL_SOCKET");
#ifdef _WIN32
    result.socket_path = env_socket ? env_socket : "\\\\.\\pipe\\breadterminal";
#else
    result.socket_path = env_socket ? env_socket : "/tmp/breadterminal.sock";
#endif

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

    // Check for hook-event (RemoteRPC, not local)
    if (positional[0] == "hook-event") {
        result.type = CommandType::RemoteRPC;
        result.method = "hook.event";
        // Find --json flag and parse its value from original argv
        for (int i = 2; i < argc; ++i) {
            if (std::string(argv[i]) == "--json" && i + 1 < argc) {
                result.params = nlohmann::json::parse(argv[i + 1], nullptr, false);
                if (result.params.is_discarded()) {
                    result.valid = false;
                    result.error = "Invalid JSON in --json argument";
                    return result;
                }
                break;
            }
        }
        result.valid = true;
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

    // Check if first positional arg is a known subcommand
    const std::string& cmd = positional[0];
    if (kSubcommands.count(cmd)) {
        parseSubcommand(cmd, positional, flags, result);
    } else {
        parseResourceAction(positional, flags, result);
    }

    // If --ref is provided, parse it into params
    if (result.valid && flags.count("ref")) {
        if (!parseRefId(flags["ref"], result.params)) {
            result.error = "Invalid ref ID format. Expected: ws:N/tab:N/pane:N";
            result.valid = false;
            return result;
        }
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

}  // namespace bread
