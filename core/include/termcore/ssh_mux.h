#ifndef TERMCORE_SSH_MUX_H
#define TERMCORE_SSH_MUX_H

#include "termcore/ssh_session.h"
#include "termcore/ssh_transport.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if TERMCORE_HAS_LIBSSH2
#include "termcore/ssh_transport_libssh2.h"
#endif

namespace termcore {

/// Describes one PTY channel on a multiplexed SSH connection.
struct SshChannel {
    int channel_id = 0;
    std::string shell;
    bool active = false;
};

/// A single multiplexed SSH connection that can host many PTY channels.
///
/// When built with TERMCORE_HAS_LIBSSH2, this class delegates to a real
/// libssh2 transport.  Otherwise, the stub implementation is used (buffers
/// data locally without networking).
class SshMuxSession {
public:
    explicit SshMuxSession(const SshConfig& config);
    ~SshMuxSession();

    // Non-copyable, movable.
    SshMuxSession(const SshMuxSession&) = delete;
    SshMuxSession& operator=(const SshMuxSession&) = delete;
    SshMuxSession(SshMuxSession&&) = default;
    SshMuxSession& operator=(SshMuxSession&&) = default;

    /// Explicitly connect the SSH session.  With stubs this is a no-op.
    /// Returns true on success.
    bool connect();

    /// Open a new PTY channel on this SSH connection.
    /// Returns a positive channel_id on success, or -1 on failure.
    int openChannel();

    /// Close an existing channel.  Returns true if the channel existed.
    bool closeChannel(int channel_id);

    /// Write data to a channel.  Returns true on success.
    bool writeToChannel(int channel_id, const std::string& data);

    /// Read pending data from a channel (non-blocking).
    /// Returns the buffered data, or an empty string.
    std::string readFromChannel(int channel_id);

    /// Poll channels for I/O and send keepalive.
    void poll();

    /// List all channels (both active and inactive).
    std::vector<SshChannel> listChannels() const;

    /// Number of currently active channels.
    int activeChannelCount() const;

    /// Whether the underlying SSH connection is (logically) connected.
    bool isConnected() const;

    /// Transport state (always Authenticated for stubs).
    SshTransportState transportState() const;

    /// Last error message from the transport layer.
    std::string lastError() const;

    /// The SshConfig that this session was created from.
    const SshConfig& config() const { return config_; }

    /// Generate a unique session key from an SshConfig.
    /// Format: "user@hostname:port"
    static std::string makeSessionKey(const SshConfig& config);

private:
    SshConfig config_;

#if TERMCORE_HAS_LIBSSH2
    std::unique_ptr<Libssh2Transport> transport_;
#else
    // Stub state
    struct ChannelData {
        SshChannel info;
        std::string read_buffer;
        std::string write_buffer;
    };
    bool connected_ = true;  // stub: always "connected" after construction
    int next_channel_id_ = 1;
    std::unordered_map<int, ChannelData> channels_;
    mutable std::mutex mutex_;
#endif
};

} // namespace termcore

#endif // TERMCORE_SSH_MUX_H
