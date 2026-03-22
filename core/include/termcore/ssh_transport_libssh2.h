#ifndef TERMCORE_SSH_TRANSPORT_LIBSSH2_H
#define TERMCORE_SSH_TRANSPORT_LIBSSH2_H

// This header is only useful when libssh2 is available.
#if TERMCORE_HAS_LIBSSH2

#include "termcore/ssh_known_hosts.h"
#include "termcore/ssh_transport.h"

#include <libssh2.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
#endif

namespace termcore {

/// A single PTY channel on a libssh2 SSH connection.
struct Libssh2Channel {
    int id = 0;
    LIBSSH2_CHANNEL* channel = nullptr;
    bool active = false;
    std::string read_buffer;
    std::string write_buffer;
};

/// Real SSH transport backed by libssh2.
///
/// Manages: TCP socket, SSH handshake, host key verification (TOFU),
/// authentication, PTY channels, non-blocking I/O, and keepalive.
///
/// Thread safety: all public methods are mutex-protected.
class Libssh2Transport {
public:
    explicit Libssh2Transport(const SshTransportConfig& config);
    ~Libssh2Transport();

    // Non-copyable.
    Libssh2Transport(const Libssh2Transport&) = delete;
    Libssh2Transport& operator=(const Libssh2Transport&) = delete;

    /// Perform the full connection sequence (blocking).
    /// Returns true if authenticated and ready for channels.
    bool connect();

    /// Disconnect and clean up all resources.
    void disconnect();

    /// Current transport state.
    SshTransportState state() const;

    /// Last error message.
    std::string lastError() const;

    /// Open a new PTY channel.  Returns channel id > 0 or -1 on failure.
    int openChannel();

    /// Close a channel by id.
    bool closeChannel(int channel_id);

    /// Write data to a channel (non-blocking, buffers internally).
    bool writeToChannel(int channel_id, const std::string& data);

    /// Read available data from a channel (non-blocking).
    std::string readFromChannel(int channel_id);

    /// Poll all channels: flush writes, read incoming data.
    /// Call periodically from an I/O thread.
    void poll();

    /// Send keepalive if interval has elapsed.
    void sendKeepalive();

    /// Whether the transport is connected and authenticated.
    bool isConnected() const;

    /// Number of active channels.
    int activeChannelCount() const;

    /// Resize a channel's PTY.
    bool resizeChannel(int channel_id, int cols, int rows);

private:
    SshTransportConfig config_;
    SshKnownHosts known_hosts_;

    socket_t socket_ = kInvalidSocket;
    LIBSSH2_SESSION* session_ = nullptr;

    std::atomic<SshTransportState> state_{SshTransportState::Disconnected};
    std::string last_error_;

    int next_channel_id_ = 1;
    std::unordered_map<int, Libssh2Channel> channels_;
    mutable std::mutex mutex_;

    std::chrono::steady_clock::time_point last_keepalive_;

    // Connection steps
    bool connectSocket();
    bool performHandshake();
    bool verifyHostKey();
    bool authenticate();
    bool tryAuthAgent();
    bool tryAuthPublicKey(const std::string& key_path);
    bool tryAuthPassword();

    // Helpers
    void setError(const std::string& msg);
    void setErrorFromSession(const std::string& prefix);
    void closeSocket();
    void cleanupSession();
};

/// One-time libssh2 global init / shutdown (reference counted).
void libssh2GlobalInit();
void libssh2GlobalShutdown();

} // namespace termcore

#endif // TERMCORE_HAS_LIBSSH2
#endif // TERMCORE_SSH_TRANSPORT_LIBSSH2_H
