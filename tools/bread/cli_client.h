#pragma once

#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace bread {

/// Minimal socket/pipe client for JSON-RPC communication.
/// Uses Unix domain sockets on macOS/Linux, named pipes on Windows.
class CliClient {
public:
    /// Connect to the socket/pipe at the given path.
    /// Returns true on success; sets error message on failure.
    bool connect(const std::string& socket_path, int timeout_ms = 3000);

    /// Send a JSON-RPC request line and receive a response line.
    /// Returns true on success.
    bool sendRequest(const std::string& json_line, std::string& response);

    /// Close the connection.
    void close();

    /// Get the last error message.
    const std::string& lastError() const { return error_; }

private:
#ifdef _WIN32
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
#else
    int fd_ = -1;
#endif
    std::string error_;
};

}  // namespace bread
