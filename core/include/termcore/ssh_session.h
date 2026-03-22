#ifndef TERMCORE_SSH_SESSION_H
#define TERMCORE_SSH_SESSION_H

#include <string>
#include <vector>

namespace termcore {

/// Configuration for a single SSH host, parsed from ~/.ssh/config or user input.
struct SshConfig {
    std::string host;             // Host pattern or alias from ssh config
    std::string hostname;         // Actual hostname (HostName directive)
    int port = 22;
    std::string user;
    std::string identity_file;
    bool forward_agent = false;
    std::string proxy_command;
    std::string proxy_jump;

    /// Returns a display-friendly label: "user@hostname:port" or just "host"
    std::string displayName() const;
};

/// Manages SSH session creation — builds commands for spawning ssh via PTY.
/// Does NOT implement the SSH protocol; it wraps the system `ssh` binary.
class SshSession {
public:
    /// Parse an OpenSSH config file and return all Host entries.
    /// Supports: Host, HostName, Port, User, IdentityFile, ForwardAgent,
    ///           ProxyCommand, ProxyJump, Include.
    /// Wildcard-only hosts (e.g., Host *) are excluded from the result.
    static std::vector<SshConfig> parseSSHConfig(const std::string& path);

    /// Build the ssh command line from an SshConfig.
    /// Returns a string like: ssh -p 2222 -i ~/.ssh/id_rsa user@hostname
    static std::string buildSshCommand(const SshConfig& config);

    /// Build an argv-style vector for the ssh command.
    static std::vector<std::string> buildSshArgs(const SshConfig& config);

    /// Find an SshConfig matching a given host alias from a list of configs.
    /// Supports simple wildcard matching (e.g., *.example.com).
    static const SshConfig* findHost(const std::vector<SshConfig>& configs,
                                     const std::string& host);

    /// Match a hostname against an SSH Host pattern (supports * and ? wildcards).
    static bool matchHostPattern(const std::string& pattern,
                                 const std::string& hostname);

private:
    /// Parse a single ssh config file (helper for Include support).
    static void parseSSHConfigFile(const std::string& path,
                                   std::vector<SshConfig>& configs,
                                   int depth);
};

} // namespace termcore

#endif // TERMCORE_SSH_SESSION_H
