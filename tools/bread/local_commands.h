#pragma once

#include "arg_parser.h"

namespace bread {

/// Handle the `bread identify` command.
int cmdIdentify(const ParsedArgs& args);

/// Handle the `bread capabilities` command.
int cmdCapabilities(const ParsedArgs& args);

/// Handle the `bread get-text` command.
/// Connects to the server via socket and requests scrollback lines.
int cmdGetText(const ParsedArgs& args);

}  // namespace bread
