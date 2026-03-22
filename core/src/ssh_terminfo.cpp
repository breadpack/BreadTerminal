#include "termcore/ssh_terminfo.h"
#include "termcore/terminfo.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace termcore {

// ---------------------------------------------------------------------------
// Base64 encoder
// ---------------------------------------------------------------------------

static const char kBase64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string SshTerminfoHelper::base64Encode(const std::string& input) {
    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);

    for (size_t i = 0; i < input.size(); i += 3) {
        uint32_t octet_a = static_cast<uint8_t>(input[i]);
        uint32_t octet_b = (i + 1 < input.size())
                               ? static_cast<uint8_t>(input[i + 1]) : 0;
        uint32_t octet_c = (i + 2 < input.size())
                               ? static_cast<uint8_t>(input[i + 2]) : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        output += kBase64Chars[(triple >> 18) & 0x3F];
        output += kBase64Chars[(triple >> 12) & 0x3F];
        output += (i + 1 < input.size()) ? kBase64Chars[(triple >> 6) & 0x3F] : '=';
        output += (i + 2 < input.size()) ? kBase64Chars[triple & 0x3F] : '=';
    }

    return output;
}

// ---------------------------------------------------------------------------
// Terminfo source as base64
// ---------------------------------------------------------------------------

std::string SshTerminfoHelper::getBase64TerminfoSource() {
    return base64Encode(breadTerminalTerminfoSource());
}

// ---------------------------------------------------------------------------
// Remote environment setup
// ---------------------------------------------------------------------------

std::string SshTerminfoHelper::generateRemoteEnvSetup() {
    return
        "export TERM_PROGRAM=BreadTerminal\n"
        "export BREADTERMINAL=1\n";
}

// ---------------------------------------------------------------------------
// Terminfo install script
// ---------------------------------------------------------------------------

std::string SshTerminfoHelper::generateTerminfoInstallScript() {
    std::string b64 = getBase64TerminfoSource();

    // The script is a self-contained POSIX shell snippet that:
    //  1. Checks if the terminfo entry already exists
    //  2. If not, decodes the base64 terminfo source and compiles with tic
    //  3. Sets environment variables
    //  4. Execs the user's login shell

    std::ostringstream ss;
    ss << R"SH(#!/bin/sh
# BreadTerminal SSH terminfo propagation script
_bt_term_name="xterm-breadterminal"
_bt_terminfo_dir="$HOME/.terminfo"

# Check if terminfo entry already exists
_bt_need_install=1
for _bt_dir in "$_bt_terminfo_dir" /usr/share/terminfo /usr/lib/terminfo /etc/terminfo; do
    if [ -f "$_bt_dir/x/$_bt_term_name" ] || [ -f "$_bt_dir/78/$_bt_term_name" ]; then
        _bt_need_install=0
        break
    fi
done

if [ "$_bt_need_install" = "1" ]; then
    # Decode and install terminfo
    _bt_ti_source=$(echo ')SH";

    ss << b64;

    ss << R"SH(' | base64 -d 2>/dev/null || echo ')SH";
    ss << b64;
    ss << R"SH(' | base64 -D 2>/dev/null)
    if [ -n "$_bt_ti_source" ] && command -v tic >/dev/null 2>&1; then
        mkdir -p "$_bt_terminfo_dir" 2>/dev/null
        _bt_tmp="$_bt_terminfo_dir/.breadterminal.ti.tmp"
        printf '%s' "$_bt_ti_source" > "$_bt_tmp"
        tic -x -o "$_bt_terminfo_dir" "$_bt_tmp" 2>/dev/null
        rm -f "$_bt_tmp"
        # Verify installation
        if [ ! -f "$_bt_terminfo_dir/x/$_bt_term_name" ] && \
           [ ! -f "$_bt_terminfo_dir/78/$_bt_term_name" ]; then
            _bt_need_install=2  # failed
        fi
    else
        _bt_need_install=2  # no tic or decode failed
    fi
fi

# Set TERM if terminfo is available, otherwise fall back
if [ "$_bt_need_install" != "2" ]; then
    export TERM="$_bt_term_name"
    export TERMINFO="$_bt_terminfo_dir"
else
    export TERM="xterm-256color"
fi

# BreadTerminal environment variables
export TERM_PROGRAM=BreadTerminal
export BREADTERMINAL=1

# Clean up temporary variables
unset _bt_term_name _bt_terminfo_dir _bt_need_install _bt_ti_source _bt_tmp _bt_dir

# Exec user's shell
exec "$SHELL" -l
)SH";

    return ss.str();
}

// ---------------------------------------------------------------------------
// SSH command wrapping
// ---------------------------------------------------------------------------

std::string SshTerminfoHelper::wrapSshCommand(const std::string& sshCmd) {
    std::string script = generateTerminfoInstallScript();

    // Escape single quotes in the script for embedding in a shell string
    std::string escaped;
    escaped.reserve(script.size() + 64);
    for (char c : script) {
        if (c == '\'') {
            escaped += "'\"'\"'";
        } else {
            escaped += c;
        }
    }

    return sshCmd + " -- sh -c '" + escaped + "'";
}

std::vector<std::string> SshTerminfoHelper::wrapSshArgs(
    const std::vector<std::string>& baseArgs) {
    std::vector<std::string> args = baseArgs;
    args.push_back("--");
    args.push_back("sh");
    args.push_back("-c");
    args.push_back(generateTerminfoInstallScript());
    return args;
}

} // namespace termcore
