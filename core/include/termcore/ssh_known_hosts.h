#ifndef TERMCORE_SSH_KNOWN_HOSTS_H
#define TERMCORE_SSH_KNOWN_HOSTS_H

#include <mutex>
#include <string>

namespace termcore {

/// Result of checking a host key against known_hosts.
enum class KnownHostResult {
    Match,     ///< Key matches a known entry.
    Mismatch,  ///< Key does NOT match the stored entry.
    NotFound,  ///< Host not in known_hosts file.
    Error,     ///< File could not be read/parsed.
};

/// Manages the known_hosts file for accept-on-first-use (TOFU) host key
/// verification.  Thread-safe.
///
/// File format is a simplified OpenSSH known_hosts:
///   hostname:port keytype base64key
class SshKnownHosts {
public:
    /// Construct with a path to the known_hosts file.
    /// If empty, defaults to ~/.ssh/known_hosts.
    explicit SshKnownHosts(const std::string& path = "");

    /// Check whether a host key is known.
    KnownHostResult check(const std::string& hostname, int port,
                          const std::string& key_type,
                          const std::string& key_base64) const;

    /// Add a new host key entry (accept on first use).
    /// Returns true on success.
    bool addEntry(const std::string& hostname, int port,
                  const std::string& key_type,
                  const std::string& key_base64);

    /// The resolved file path.
    const std::string& filePath() const { return path_; }

private:
    std::string path_;
    mutable std::mutex mutex_;

    /// Build the lookup key: "hostname:port" or "[hostname]:port" for
    /// non-standard ports.
    static std::string makeHostKey(const std::string& hostname, int port);
};

} // namespace termcore

#endif // TERMCORE_SSH_KNOWN_HOSTS_H
