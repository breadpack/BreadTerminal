#pragma once

#include <string>

namespace termcore {

/// Configuration for SSH sessions, including shell integration auto-deployment.
struct SshSessionConfig {
    /// Hostname or IP address of the remote host
    std::string host;

    /// Remote user (empty = use system default)
    std::string user;

    /// SSH port (0 = use default 22)
    int port = 0;

    /// Automatically deploy shell integration and terminfo to the remote host
    /// on first connection (similar to Kitty's ssh kitten).
    bool auto_deploy_integration = true;

    /// TERM value to use if terminfo deployment fails on the remote host.
    std::string fallback_term = "xterm-256color";
};

} // namespace termcore
