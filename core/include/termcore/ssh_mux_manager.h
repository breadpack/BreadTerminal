#ifndef TERMCORE_SSH_MUX_MANAGER_H
#define TERMCORE_SSH_MUX_MANAGER_H

#include "termcore/ssh_mux.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace termcore {

/// Informational snapshot of an active SSH mux session.
struct SshMuxSessionInfo {
    std::string key;
    std::string display_name;
    int channel_count = 0;
};

/// Manages multiplexed SSH sessions keyed by host:port:user.
///
/// Multiple panes / tabs that connect to the same remote host share a
/// single SshMuxSession (and therefore a single SSH connection).  When the
/// last channel on a session is closed the session is automatically cleaned
/// up.
class SshMuxManager {
public:
    SshMuxManager();
    ~SshMuxManager();

    /// Get an existing session for the host, or create a new one.
    std::shared_ptr<SshMuxSession> getOrCreateSession(const SshConfig& config);

    /// Explicitly close a session by its key, dropping all channels.
    bool closeSession(const std::string& key);

    /// List all sessions that still have active channels.
    std::vector<SshMuxSessionInfo> listSessions() const;

    /// Remove sessions that have zero active channels.
    void cleanup();

    /// Number of managed sessions (including those with 0 channels).
    size_t sessionCount() const;

private:
    std::unordered_map<std::string, std::shared_ptr<SshMuxSession>> sessions_;
    mutable std::mutex mutex_;
};

} // namespace termcore

#endif // TERMCORE_SSH_MUX_MANAGER_H
