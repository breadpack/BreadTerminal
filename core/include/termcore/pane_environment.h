#pragma once

#include "termcore/mux.h"

#include <string>
#include <utility>
#include <vector>

namespace termcore {

/// Environment variables injected into each pane's PTY process.
/// These allow agents and child processes to discover their context
/// within BreadTerminal (which workspace, tab, and pane they run in).
struct PaneEnvironment {
    std::string socket_path;
    WorkspaceId workspace_id = kInvalidWorkspace;
    TabId tab_id = kInvalidTab;
    PaneId pane_id = kInvalidPane;
    bool tmux_compat_enabled = true;
    std::string bread_cli_path;

    /// Convert to a list of key=value pairs suitable for PTY spawn.
    std::vector<std::pair<std::string, std::string>> toEnvVars() const {
        std::vector<std::pair<std::string, std::string>> vars;
        vars.reserve(10);

        vars.emplace_back("BREADTERMINAL", "1");
        vars.emplace_back("TERM_PROGRAM", "BreadTerminal");

        if (!socket_path.empty()) {
            vars.emplace_back("BREADTERMINAL_SOCKET", socket_path);
        }
        if (workspace_id != kInvalidWorkspace) {
            vars.emplace_back("BREADTERMINAL_WORKSPACE_ID",
                              std::to_string(workspace_id));
        }
        if (tab_id != kInvalidTab) {
            vars.emplace_back("BREADTERMINAL_TAB_ID",
                              std::to_string(tab_id));
        }
        if (pane_id != kInvalidPane) {
            vars.emplace_back("BREADTERMINAL_PANE_ID",
                              std::to_string(pane_id));
        }

        vars.emplace_back("BREADTERMINAL_OSC_CHANNEL", "7770");
        vars.emplace_back("BREADTERMINAL_VERSION", "0.1.0");

        if (tmux_compat_enabled && !bread_cli_path.empty()) {
            vars.emplace_back("TMUX", "bread//" + std::to_string(pane_id));
            vars.emplace_back("TMUX_PROGRAM", bread_cli_path + " --tmux");
        }

        return vars;
    }
};

}  // namespace termcore
