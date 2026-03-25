#pragma once

#include "arg_parser.h"
#include <string>

namespace bread {

/// Parse tmux-style arguments and convert to BreadTerminal JSON-RPC ParsedArgs.
/// argv[0] should be the tmux subcommand (e.g., "split-window").
ParsedArgs parseTmuxArgs(int argc, char* argv[]);

/// List of supported tmux commands for error messages.
std::string supportedTmuxCommands();

}  // namespace bread
