#pragma once

#include <string>

namespace breadterminal {

/// Minimal Unix socket client for JSON-RPC communication.
class CliClient {
public:
    /// Connect to the socket at the given path.
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
    int fd_ = -1;
    std::string error_;
};

}  // namespace breadterminal
