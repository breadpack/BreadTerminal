#ifndef TERMCORE_SSH_TERMINFO_H
#define TERMCORE_SSH_TERMINFO_H

#include <string>
#include <vector>

namespace termcore {

/// Helper for propagating BreadTerminal's terminfo to remote SSH hosts.
/// Generates shell scripts that check for and install the terminfo entry,
/// then set the appropriate environment variables on the remote side.
class SshTerminfoHelper {
public:
    /// Generate a shell script that installs the BreadTerminal terminfo
    /// on the remote host if not already present.
    /// The script:
    ///   1. Checks if xterm-breadterminal terminfo exists
    ///   2. If not, decodes embedded base64 terminfo source and compiles it
    ///   3. Sets TERM=xterm-breadterminal
    ///   4. Sets TERM_PROGRAM=BreadTerminal and BREADTERMINAL=1
    ///   5. Execs the user's login shell
    static std::string generateTerminfoInstallScript();

    /// Return the terminfo source encoded as base64.
    static std::string getBase64TerminfoSource();

    /// Wrap an ssh command so that it automatically installs the terminfo
    /// on the remote before starting a shell.
    /// Input:  "ssh -p 22 user@host"
    /// Output: "ssh -p 22 user@host 'bash -c \"<install_script>\"'"
    static std::string wrapSshCommand(const std::string& sshCmd);

    /// Build a complete argv vector for SSH with terminfo propagation.
    /// Takes the base SSH args (from SshSession::buildSshArgs) and appends
    /// the remote command that installs terminfo + starts the shell.
    static std::vector<std::string> wrapSshArgs(
        const std::vector<std::string>& baseArgs);

    /// Generate the shell integration installation snippet for the remote.
    /// This sets environment variables and optionally sources shell integration.
    static std::string generateRemoteEnvSetup();

private:
    /// Simple base64 encoder (no external dependency).
    static std::string base64Encode(const std::string& input);
};

} // namespace termcore

#endif // TERMCORE_SSH_TERMINFO_H
