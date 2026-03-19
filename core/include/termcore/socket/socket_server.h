#pragma once

#include "termcore/socket/socket_transport.h"
#include "termcore/socket/command_dispatcher.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace termcore {

/// Socket server that accepts connections and dispatches JSON-RPC commands.
class SocketServer {
public:
    SocketServer(std::unique_ptr<ISocketTransport> transport,
                 std::shared_ptr<CommandDispatcher> dispatcher);
    ~SocketServer();

    /// Start the accept thread. Returns true on success.
    bool start();

    /// Stop the server and join all threads.
    void stop();

    /// Check if the server is running.
    bool isRunning() const { return running_.load(); }

    /// Get the socket path.
    std::string socketPath() const { return transport_->socketPath(); }

    /// Set an auth token. When set, requests must include _auth in params.
    void setAuthToken(std::string token);

    /// Drain the main-thread dispatch queue.
    /// Call from the main thread periodically (e.g., every 16ms).
    void drainMainThreadQueue();

private:
    void acceptLoop();
    void clientLoop(int fd);

    std::unique_ptr<ISocketTransport> transport_;
    std::shared_ptr<CommandDispatcher> dispatcher_;
    std::thread accept_thread_;
    std::atomic<bool> running_{false};

    std::mutex auth_mutex_;
    std::string auth_token_;

    // Serializes dispatcher calls from multiple client threads
    std::mutex dispatch_mutex_;

    // Main-thread dispatch queue
    std::mutex queue_mutex_;
    std::vector<std::function<void()>> pending_;
};

/// Resolve socket path from BREADTERMINAL_SOCKET env var or default.
std::string resolveSocketPath();

}  // namespace termcore
