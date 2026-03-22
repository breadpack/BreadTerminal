#pragma once

#include <string>

namespace termcore {

/// Generates shell integration scripts and terminfo data for remote SSH deployment.
/// Similar to Kitty's ssh kitten, this automatically copies terminfo and shell
/// integration scripts to remote hosts.
struct SshIntegrationDeploy {
    /// Shell integration script content for each shell type
    static std::string bashIntegration();
    static std::string zshIntegration();
    static std::string fishIntegration();

    /// Terminfo source for breadterminal TERM
    static std::string terminfoSource();

    /// Generate a shell command sequence that:
    /// 1. Creates ~/.terminfo/ and installs breadterminal terminfo
    /// 2. Creates ~/.config/breadterminal/shell-integration/ and installs scripts
    /// 3. Sources the appropriate script for the detected shell
    /// Returns a single string of shell commands to pipe through SSH
    static std::string deployScript(const std::string& remoteShell = "");

    /// Detect remote shell from SHELL env or /etc/passwd
    static std::string detectShellCommand();

    /// Check if integration is already deployed (via marker file)
    static std::string checkDeployedCommand();
};

} // namespace termcore
