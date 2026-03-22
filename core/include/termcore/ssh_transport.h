#ifndef TERMCORE_SSH_TRANSPORT_H
#define TERMCORE_SSH_TRANSPORT_H

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace termcore {

/// Authentication method for SSH connections.
enum class SshAuthMethod {
    None,
    Password,
    PublicKey,
    Agent,
};

/// Convert an SshAuthMethod to a human-readable string.
inline const char* authMethodName(SshAuthMethod m) {
    switch (m) {
        case SshAuthMethod::None:      return "none";
        case SshAuthMethod::Password:  return "password";
        case SshAuthMethod::PublicKey:  return "publickey";
        case SshAuthMethod::Agent:     return "agent";
    }
    return "unknown";
}

/// Result of a host key verification check.
enum class HostKeyAction {
    Accept,   ///< Key is known and matches.
    Reject,   ///< Key mismatch or user rejected.
    Unknown,  ///< Key not previously seen (first-use).
};

/// Connection state machine for SSH transport.
enum class SshTransportState {
    Disconnected,
    Connecting,       ///< TCP socket in progress
    Handshaking,      ///< SSH handshake in progress
    HostKeyVerify,    ///< Waiting for host key decision
    Authenticating,   ///< Trying auth methods
    Authenticated,    ///< Auth succeeded, ready for channels
    Error,            ///< Unrecoverable error
};

/// Convert transport state to string.
inline const char* transportStateName(SshTransportState s) {
    switch (s) {
        case SshTransportState::Disconnected:    return "disconnected";
        case SshTransportState::Connecting:      return "connecting";
        case SshTransportState::Handshaking:     return "handshaking";
        case SshTransportState::HostKeyVerify:   return "host_key_verify";
        case SshTransportState::Authenticating:  return "authenticating";
        case SshTransportState::Authenticated:   return "authenticated";
        case SshTransportState::Error:           return "error";
    }
    return "unknown";
}

/// Configuration for the SSH transport layer.
struct SshTransportConfig {
    std::string hostname;
    int port = 22;
    std::string username;

    /// Password for password authentication (empty to skip).
    std::string password;

    /// Paths to private key files. If empty, defaults are tried.
    std::vector<std::string> identity_files;

    /// Whether to attempt SSH agent authentication.
    bool try_agent = true;

    /// PTY terminal type.
    std::string term_type = "xterm-256color";

    /// PTY dimensions.
    int pty_cols = 80;
    int pty_rows = 24;

    /// Keepalive interval in seconds (0 = disabled).
    int keepalive_seconds = 30;

    /// Connection timeout in seconds.
    int connect_timeout_seconds = 15;
};

/// Ordered list of auth methods to try, based on config.
std::vector<SshAuthMethod> selectAuthMethods(const SshTransportConfig& config);

/// Resolve default identity file paths (~/.ssh/id_ed25519, id_rsa, etc.).
std::vector<std::string> defaultIdentityFiles();

/// Expand ~ to the user's home directory.
std::string expandHomePath(const std::string& path);

} // namespace termcore

#endif // TERMCORE_SSH_TRANSPORT_H
