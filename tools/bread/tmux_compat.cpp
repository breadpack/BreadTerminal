#include "tmux_compat.h"

#include <string>
#include <vector>

namespace bread {

/// Parse -t target (e.g., "%3" or "3") and return the pane ID.
static int parseTarget(const std::string& target) {
    if (!target.empty() && target[0] == '%')
        return std::stoi(target.substr(1));
    return std::stoi(target);
}

ParsedArgs parseTmuxArgs(int argc, char* argv[]) {
    ParsedArgs args;
    args.type = CommandType::RemoteRPC;

    if (argc < 1) {
        args.valid = false;
        args.error = "No tmux command specified. " + supportedTmuxCommands();
        return args;
    }

    std::string cmd = argv[0];

    if (cmd == "split-window") {
        args.method = "pane.split";
        std::string direction = "vertical";  // tmux default
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "-h") direction = "horizontal";
            else if (std::string(argv[i]) == "-v") direction = "vertical";
        }
        args.params = {{"direction", direction}};
        args.valid = true;
    }
    else if (cmd == "send-keys") {
        args.method = "pane.sendKeys";
        int pane_id = 0;
        std::string keys;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "-t" && i + 1 < argc) {
                std::string target = argv[++i];
                pane_id = parseTarget(target);
            } else {
                // Special key translation
                if (a == "Enter") keys += "\n";
                else if (a == "C-c") keys += "\x03";
                else if (a == "C-d") keys += "\x04";
                else if (a == "Escape") keys += "\x1b";
                else if (a == "Space") keys += " ";
                else if (a == "Tab") keys += "\t";
                else keys += a;
            }
        }
        args.params = {{"pane_id", pane_id}, {"keys", keys}};
        args.valid = true;
    }
    else if (cmd == "select-pane") {
        args.method = "pane.focus";
        int pane_id = 0;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "-t" && i + 1 < argc) {
                std::string target = argv[++i];
                pane_id = parseTarget(target);
            }
        }
        args.params = {{"pane_id", pane_id}};
        args.valid = true;
    }
    else if (cmd == "list-panes") {
        args.method = "pane.list";
        args.params = nlohmann::json::object();
        args.valid = true;
    }
    else if (cmd == "kill-pane") {
        args.method = "pane.close";
        int pane_id = 0;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "-t" && i + 1 < argc) {
                std::string target = argv[++i];
                pane_id = parseTarget(target);
            }
        }
        args.params = {{"pane_id", pane_id}};
        args.valid = true;
    }
    else if (cmd == "display-message") {
        args.method = "query.activePane";
        args.params = nlohmann::json::object();
        args.valid = true;
    }
    else {
        args.valid = false;
        args.error = "Unsupported tmux command: '" + cmd + "'. " + supportedTmuxCommands();
    }

    return args;
}

std::string supportedTmuxCommands() {
    return "Supported: split-window, send-keys, select-pane, list-panes, kill-pane, display-message";
}

}  // namespace bread
