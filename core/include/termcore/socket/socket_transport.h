#pragma once

#include <memory>
#include <string>

namespace termcore {

/// Abstract transport layer for socket communication.
/// Unix domain sockets on macOS/Linux, named pipes on Windows.
class ISocketTransport {
public:
    virtual ~ISocketTransport() = default;

    /// Create and bind the listening socket. Returns true on success.
    virtual bool listen() = 0;

    /// Block until a client connects. Returns client fd (>=0) or -1 on error/shutdown.
    virtual int acceptClient() = 0;

    /// Read a newline-delimited line from client fd.
    /// Returns 1 on success, 0 on EOF, -1 on error.
    virtual int readLine(int fd, std::string& out) = 0;

    /// Write a newline-terminated line to client fd. Returns true on success.
    virtual bool writeLine(int fd, const std::string& line) = 0;

    /// Close a specific client connection.
    virtual void closeClient(int fd) = 0;

    /// Signal shutdown to unblock acceptClient() and clean up.
    virtual void shutdown() = 0;

    /// Get the socket path (for logging / env var injection).
    virtual std::string socketPath() const = 0;
};

/// Factory: creates platform-appropriate transport.
std::unique_ptr<ISocketTransport> createSocketTransport(const std::string& path);

}  // namespace termcore
