#ifndef TERMCORE_TERMINFO_H
#define TERMCORE_TERMINFO_H

#include <string>

namespace termcore {

struct TerminfoInstallResult {
    bool success = false;
    std::string term_name;
    std::string terminfo_dir;
    std::string error;
};

/// Install the BreadTerminal terminfo entry to the user's local terminfo database.
/// Falls back to xterm-256color if installation fails.
TerminfoInstallResult installTerminfo();

/// Returns the TERM name: "xterm-breadterminal"
const char* breadTerminalTermName();

/// Returns the embedded terminfo source (.ti content)
const char* breadTerminalTerminfoSource();

} // namespace termcore

#endif // TERMCORE_TERMINFO_H
